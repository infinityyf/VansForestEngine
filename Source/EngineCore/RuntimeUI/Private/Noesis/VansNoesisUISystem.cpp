#include "VansNoesisUISystem.h"
#include "VansNoesisDocument.h"
#include "VansNoesisRenderDevice.h"
#include "VansNoesisProviders.h"
#include "VansNoesisInputAdapter.h"

#include "../../../ProjectSystem/VansProjectManager.h"
#include "../../../Util/VansLog.h"

#include <NsCore/Log.h>
#include <NsCore/Error.h>
#include <NsGui/IntegrationAPI.h>
#include <NsGui/FrameworkElement.h>
#include <NsGui/ResourceDictionary.h>
#include <NsGui/FontProperties.h>

#include <cassert>

namespace VansRuntime
{

VansNoesisUISystem::VansNoesisUISystem(VansGraphics::VansVKDevice* device)
    : m_Device(device)
{
    assert(device != nullptr && "VansNoesisUISystem: device 不能为空");
}

VansNoesisUISystem::~VansNoesisUISystem()
{
    if (m_Initialized)
    {
        Shutdown();
    }
}

bool VansNoesisUISystem::Initialize(const VansUIInitDesc& desc)
{
    if (m_Initialized)
    {
        return true;
    }

    m_ScreenWidth  = desc.m_Width;
    m_ScreenHeight = desc.m_Height;

    // 1. 日志 / 错误回调（必须在 Init 前注册）
    SetupLogHandler();
    SetupErrorHandler();

    // 2. License
    SetupLicense(desc);

    // 3. 可选功能开关
    if (!desc.m_EnableHotReload)
    {
        Noesis::GUI::DisableHotReload();
    }
    if (!desc.m_EnableInspector)
    {
        Noesis::GUI::DisableInspector();
    }

    // 4. 核心初始化
    Noesis::GUI::Init();

    // 5. 创建 Vulkan RenderDevice
    m_RenderDevice = std::make_unique<VansNoesisRenderDevice>(m_Device);
    if (!m_RenderDevice->Initialize(/*sRGB=*/false))
    {
        return false;
    }

    // 6. 安装资源 Provider
    InstallProviders();

    // 7. 注册项目自定义类型（Type Registry）
    RegisterTypes();

    // 8. 创建输入适配器
    m_InputAdapter = std::make_unique<VansNoesisInputAdapter>();
    m_InputAdapter->Initialize();

    // 9. 加载引擎全局基础主题
    LoadGlobalTheme();

    m_Initialized = true;
    VANS_LOG("[NoesisUI] Initialized OK  screen=" << m_ScreenWidth << "x" << m_ScreenHeight);
    return true;
}

void VansNoesisUISystem::SetSceneViewport(float screenX, float screenY,
                                            float screenW, float screenH)
{
    if (m_InputAdapter)
    {
        m_InputAdapter->SetSceneViewport(screenX, screenY, screenW, screenH,
                                          static_cast<float>(m_ScreenWidth),
                                          static_cast<float>(m_ScreenHeight));
    }
}

void VansNoesisUISystem::Shutdown()
{
    if (!m_Initialized)
    {
        return;
    }

    // 首先销毁所有 Document，确保 Noesis View/Renderer 先释放
    m_Documents.clear();

    // 关闭输入适配器
    if (m_InputAdapter)
    {
        m_InputAdapter->Shutdown();
        m_InputAdapter.reset();
    }

    // 释放 Provider
    m_XamlProvider    = nullptr;
    m_TextureProvider = nullptr;
    m_FontProvider    = nullptr;

    // 释放 RenderDevice（在 Noesis::Shutdown 前）
    m_RenderDevice.reset();

    // Noesis 关闭
    Noesis::GUI::Shutdown();

    m_Initialized = false;
}

void VansNoesisUISystem::Update(float deltaTime)
{
    if (!m_Initialized)
    {
        return;
    }

    m_TotalTimeSeconds += static_cast<double>(deltaTime);

    // Update input adapter (flushes scroll events)
    if (m_InputAdapter)
    {
        m_InputAdapter->Update();
    }

    for (auto& doc : m_Documents)
    {
        if (doc && doc->IsVisible())
        {
            doc->Update(m_TotalTimeSeconds);
        }
    }
}

void VansNoesisUISystem::RenderOffscreenPass(VkCommandBuffer cmd)
{
    if (!m_Initialized || m_Documents.empty())
    {
        return;
    }

    // 每帧递增帧号，通知 Noesis 可回收 safeFrame 之前的 GPU 资源
    ++m_FrameNumber;
    const uint64_t safeFrame = (m_FrameNumber > k_MaxFramesInFlight)
                                   ? (m_FrameNumber - k_MaxFramesInFlight)
                                   : 0;
    m_RenderDevice->SetActiveCommandBuffer(cmd, m_FrameNumber, safeFrame);

    // 离屏渲染（渐变、效果等）必须在 BeginRenderPass 之前完成
    for (auto& doc : m_Documents)
    {
        if (doc && doc->IsVisible())
        {
            doc->RenderOffscreen();
        }
    }
}

void VansNoesisUISystem::RenderDocumentsPass(VkRenderPass renderPass, uint32_t sampleCount)
{
    if (!m_Initialized || m_Documents.empty())
    {
        return;
    }

    // 通知 Noesis 当前激活的 RenderPass（用于懒编译 PSO）
    m_RenderDevice->SetActiveRenderPass(renderPass, sampleCount);

    // 正式上屏渲染，必须在 vkCmdBeginRenderPass 之后调用
    for (auto& doc : m_Documents)
    {
        if (doc && doc->IsVisible())
        {
            doc->Render();
        }
    }
}

void VansNoesisUISystem::SetScreenSize(uint32_t width, uint32_t height)
{
    m_ScreenWidth  = width;
    m_ScreenHeight = height;

    for (auto& doc : m_Documents)
    {
        if (doc)
        {
            doc->SetSize(width, height);
        }
    }
}

std::shared_ptr<VansNoesisDocument> VansNoesisUISystem::LoadDocument(const std::string& xamlPath)
{
    assert(m_Initialized && "VansNoesisUISystem: 调用 LoadDocument 前必须先 Initialize");

    // Load XAML root element
    Noesis::Ptr<Noesis::FrameworkElement> content =
        Noesis::GUI::LoadXaml<Noesis::FrameworkElement>(xamlPath.c_str());

    if (!content)
    {
        VANS_LOG_ERROR("[NoesisUI] LoadXaml returned null for: " << xamlPath);
        return nullptr;
    }

    // Create the Noesis IView
    Noesis::Ptr<Noesis::IView> view = Noesis::GUI::CreateView(content);
    if (!view)
    {
        return nullptr;
    }

    // Configure view size and renderer
    view->SetSize(m_ScreenWidth, m_ScreenHeight);
    view->GetRenderer()->Init(m_RenderDevice->GetNoesisDevice());

    // Construct the document (which registers the view with the input adapter)
    auto doc = std::make_shared<VansNoesisDocument>(
        std::move(view),
        std::move(content),
        xamlPath,
        m_InputAdapter.get());

    m_Documents.push_back(doc);
    return doc;
}

void VansNoesisUISystem::UnloadDocument(const std::shared_ptr<VansNoesisDocument>& doc)
{
    auto it = std::find(m_Documents.begin(), m_Documents.end(), doc);
    if (it != m_Documents.end())
    {
        m_Documents.erase(it);
    }
}

bool VansNoesisUISystem::WantsMouse() const
{
    return m_InputAdapter ? m_InputAdapter->WantsMouse() : false;
}

bool VansNoesisUISystem::WantsKeyboard() const
{
    return m_InputAdapter ? m_InputAdapter->WantsKeyboard() : false;
}

// ── 私有辅助 ──────────────────────────────────────────────────────────

void VansNoesisUISystem::SetupLogHandler()
{
    Noesis::SetLogHandler([](const char* /*filename*/, uint32_t /*line*/,
                             uint32_t level, const char* /*channel*/,
                             const char* message)
    {
        switch (level)
        {
            case 0: break; // Trace — 忽略
            case 1: VANS_LOG("[Noesis DEBUG] " << message); break;
            case 2: VANS_LOG("[Noesis INFO]  " << message); break;
            case 3: VANS_LOG_WARN("[Noesis WARN]  " << message); break;
            case 4: VANS_LOG_ERROR("[Noesis ERROR] " << message); break;
            default: break;
        }
    });
}

void VansNoesisUISystem::SetupErrorHandler()
{
    Noesis::SetErrorHandler([](const char* filename, uint32_t line,
                                const char* desc, bool fatal)
    {
        VANS_LOG_ERROR("[Noesis ASSERT] " << (filename ? filename : "?") << ":" << line
                       << "  " << (desc ? desc : "") << (fatal ? "  [FATAL]" : ""));
    });
}

void VansNoesisUISystem::SetupLicense(const VansUIInitDesc& desc)
{
    if (desc.m_LicenseName && desc.m_LicenseName[0] != '\0')
    {
        Noesis::SetLicense(desc.m_LicenseName, desc.m_LicenseKey);
    }
}

void VansNoesisUISystem::InstallProviders()
{
    auto* projectManager = &Vans::VansProjectManager::Get();
    auto* pathResolver   = &projectManager->GetPathResolver();

    // XAML Provider
    auto* xamlProvider    = new VansNoesisXamlProvider(projectManager, pathResolver);
    m_XamlProvider        = Noesis::Ptr<VansNoesisXamlProvider>(*xamlProvider);
    Noesis::GUI::SetXamlProvider(m_XamlProvider);

    // Texture Provider
    auto* texProvider     = new VansNoesisTextureProvider(projectManager, pathResolver,
                                                          m_RenderDevice.get());
    m_TextureProvider     = Noesis::Ptr<VansNoesisTextureProvider>(*texProvider);
    Noesis::GUI::SetTextureProvider(m_TextureProvider);

    // Font Provider
    auto* fontProvider    = new VansNoesisFontProvider(projectManager, pathResolver);
    m_FontProvider        = Noesis::Ptr<VansNoesisFontProvider>(*fontProvider);
    Noesis::GUI::SetFontProvider(m_FontProvider);

    // 字体回退链：Noesis 在 FontFamily 缺少字形时按此顺序查找
    // 顺序：Arial（各平台最广泛）→ Segoe UI（Windows 默认）
    const char* fontFallbacks[] = { "Arial", "Segoe UI" };
    Noesis::GUI::SetFontFallbacks(fontFallbacks, 2);

    // 默认字体属性：大小 15pt，Normal weight/stretch/style
    Noesis::GUI::SetFontDefaultProperties(
        15.0f,
        Noesis::FontWeight_Normal,
        Noesis::FontStretch_Normal,
        Noesis::FontStyle_Normal);
}

void VansNoesisUISystem::RegisterTypes()
{
    // 用于注册项目层 ViewModel 的 Noesis Reflection
    // 实际注册在 VansNoesisTypeRegistry.cpp 中通过 NS_REGISTER_COMPONENT 宏完成
    // 该函数目前为占位接口，未来可扫描注册表自动调用
}

void VansNoesisUISystem::LoadGlobalTheme()
{
    // 加载引擎内置基础主题（engine:// 协议指向 EngineAssets/UI/Themes/）
    // 若文件不存在则跳过，不阻断初始化流程
    const char* baseThemeUri = "engine://UI/Themes/BaseTheme.xaml";

    Noesis::Ptr<Noesis::ResourceDictionary> theme =
        Noesis::GUI::LoadXaml<Noesis::ResourceDictionary>(baseThemeUri);

    if (theme)
    {
        Noesis::GUI::SetApplicationResources(theme);
    }
}

} // namespace VansRuntime
