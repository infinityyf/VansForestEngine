#include "VansNoesisUISystem.h"
#include "VansNoesisDocument.h"
#include "VansNoesisRenderDevice.h"
#include "VansNoesisProviders.h"
#include "VansNoesisInputAdapter.h"
#include "../../VansUIAssetResolver.h"

#include "../../../AssetCore/VansBuiltInAssetCatalog.h"
#include "../../../ProjectSystem/VansProjectManager.h"
#include "../../../Util/VansLog.h"
#include "../../../RuntimeCore/VansThreadContract.h"

#include <NsCore/Log.h>
#include <NsCore/Error.h>
#include <NsGui/IntegrationAPI.h>
#include <NsGui/FrameworkElement.h>
#include <NsGui/ResourceDictionary.h>
#include <NsGui/FontProperties.h>

#include <algorithm>
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
    m_InputAdapter->SetSceneViewport(
        0.0f,
        0.0f,
        static_cast<float>(m_ScreenWidth),
        static_cast<float>(m_ScreenHeight),
        static_cast<float>(m_ScreenWidth),
        static_cast<float>(m_ScreenHeight));

    m_Initialized = true;
    VANS_LOG("[NoesisUI] Initialized OK  screen=" << m_ScreenWidth << "x" << m_ScreenHeight);
    return true;
}

bool VansNoesisUISystem::ApplyGlobalThemeFromMemory(std::string& error)
{
    error.clear();
    if (!m_Initialized)
    {
        error = "Noesis UI is not initialized";
        return false;
    }

    std::string baseThemeUri;
    if (!VansUIAssetResolver::ResolveXamlUri(
        std::string(Vans::VansBuiltInAssetCatalog::BaseThemeGuid),
        baseThemeUri,
        error))
    {
        return false;
    }

    Noesis::Ptr<Noesis::ResourceDictionary> theme =
        Noesis::GUI::LoadXaml<Noesis::ResourceDictionary>(baseThemeUri.c_str());
    if (!theme)
    {
        error = "Noesis failed to instantiate the global theme from memory asset " +
            std::string(Vans::VansBuiltInAssetCatalog::BaseThemeGuid);
        return false;
    }

    Noesis::GUI::SetApplicationResources(theme);
    VANS_LOG("[NoesisUI] Applied global theme from memory asset "
        << Vans::VansBuiltInAssetCatalog::BaseThemeGuid);
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

bool VansNoesisUISystem::TransformMouseToView(double rawX, double rawY,
                                               double& outX, double& outY) const
{
    if (!m_InputAdapter)
        return false;

    m_InputAdapter->TransformMouseToView(rawX, rawY, outX, outY);
    return true;
}

bool VansNoesisUISystem::GetViewSize(double& outW, double& outH) const
{
    if (!m_InputAdapter)
        return false;

    m_InputAdapter->GetViewSize(outW, outH);
    return true;
}

void VansNoesisUISystem::Shutdown()
{
    VANS_ASSERT_MAIN_THREAD();
    if (!m_Initialized)
    {
        return;
    }

    // RT has already shut down every IRenderer. Main now destroys the Views,
    // dependency objects, providers and process-global Noesis state.
    {
        std::lock_guard<std::mutex> lock(m_DocumentsMutex);
        m_Documents.clear();
        m_RetiredDocuments.clear();
    }
    m_RenderFrameDocuments.clear();

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

void VansNoesisUISystem::ShutdownRendering()
{
    VANS_ASSERT_RENDER_THREAD();
    if (!m_Initialized)
        return;

    std::vector<std::shared_ptr<VansNoesisDocument>> documents;
    {
        std::lock_guard<std::mutex> lock(m_DocumentsMutex);
        documents.reserve(m_Documents.size() + m_RetiredDocuments.size());
        documents.insert(documents.end(), m_Documents.begin(), m_Documents.end());
        documents.insert(
            documents.end(), m_RetiredDocuments.begin(), m_RetiredDocuments.end());
    }
    documents.insert(
        documents.end(), m_RenderFrameDocuments.begin(), m_RenderFrameDocuments.end());
    std::sort(documents.begin(), documents.end());
    documents.erase(std::unique(documents.begin(), documents.end()), documents.end());
    for (const auto& document : documents)
    {
        if (document)
            document->ShutdownRenderer();
    }
    m_RenderFrameDocuments.clear();
}

void VansNoesisUISystem::Update(float deltaTime)
{
    VANS_ASSERT_MAIN_THREAD();
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
    VANS_ASSERT_RENDER_THREAD();
    m_RenderFrameDocuments = SnapshotActiveDocuments();
    if (!m_Initialized || m_RenderFrameDocuments.empty())
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
    for (const auto& doc : m_RenderFrameDocuments)
    {
        if (doc && doc->IsVisible() &&
            doc->InitializeRenderer(m_RenderDevice->GetNoesisDevice()))
        {
            doc->RenderOffscreen();
        }
    }
}

void VansNoesisUISystem::RenderDocumentsPass(VkRenderPass renderPass, uint32_t sampleCount)
{
    VANS_ASSERT_RENDER_THREAD();
    if (!m_Initialized || m_RenderFrameDocuments.empty())
    {
        return;
    }

    // 通知 Noesis 当前激活的 RenderPass（用于懒编译 PSO）
    m_RenderDevice->SetActiveRenderPass(renderPass, sampleCount);

    // 正式上屏渲染，必须在 vkCmdBeginRenderPass 之后调用
    for (const auto& doc : m_RenderFrameDocuments)
    {
        if (doc && doc->IsVisible())
        {
            doc->Render();
        }
    }
    m_RenderFrameDocuments.clear();
}

bool VansNoesisUISystem::PrepareDocumentPreview(
    const std::shared_ptr<VansUIDocument>& document,
    VkCommandBuffer cmd,
    double totalTimeSeconds)
{
    VANS_ASSERT_RENDER_THREAD();
    if (!m_Initialized || !document || cmd == VK_NULL_HANDLE)
        return false;

    ++m_FrameNumber;
    const uint64_t safeFrame = (m_FrameNumber > k_MaxFramesInFlight)
        ? (m_FrameNumber - k_MaxFramesInFlight)
        : 0;
    m_RenderDevice->SetActiveCommandBuffer(cmd, m_FrameNumber, safeFrame);

    auto noesisDocument = std::dynamic_pointer_cast<VansNoesisDocument>(document);
    if (!noesisDocument ||
        !noesisDocument->InitializeRenderer(m_RenderDevice->GetNoesisDevice()))
        return false;
    // IView::Update belongs to Main. The render request only consumes the
    // latest snapshot published by that update.
    (void)totalTimeSeconds;
    noesisDocument->RenderOffscreen();
    return true;
}

bool VansNoesisUISystem::RenderDocumentPreviewPass(
    const std::shared_ptr<VansUIDocument>& document,
    VkRenderPass renderPass,
    uint32_t sampleCount)
{
    VANS_ASSERT_RENDER_THREAD();
    if (!m_Initialized || !document || renderPass == VK_NULL_HANDLE)
        return false;

    m_RenderDevice->SetActiveRenderPass(renderPass, sampleCount);
    document->Render();
    return true;
}

void VansNoesisUISystem::SetScreenSize(uint32_t width, uint32_t height)
{
    VANS_ASSERT_MAIN_THREAD();
    m_ScreenWidth  = width;
    m_ScreenHeight = height;
    if (m_InputAdapter)
    {
        m_InputAdapter->SetSceneViewport(
            0.0f,
            0.0f,
            static_cast<float>(m_ScreenWidth),
            static_cast<float>(m_ScreenHeight),
            static_cast<float>(m_ScreenWidth),
            static_cast<float>(m_ScreenHeight));
    }

    for (const auto& doc : SnapshotActiveDocuments())
    {
        if (doc)
        {
            doc->SetSize(width, height);
        }
    }
}

std::shared_ptr<VansNoesisDocument> VansNoesisUISystem::LoadDocument(const std::string& xamlUri)
{
    VANS_ASSERT_MAIN_THREAD();
    assert(m_Initialized && "VansNoesisUISystem: 调用 LoadDocument 前必须先 Initialize");

    // Load XAML root element
    Noesis::Ptr<Noesis::FrameworkElement> content =
        Noesis::GUI::LoadXaml<Noesis::FrameworkElement>(xamlUri.c_str());

    if (!content)
    {
        VANS_LOG_ERROR("[NoesisUI] LoadXaml returned null for: " << xamlUri);
        return nullptr;
    }

    // Create the Noesis IView
    Noesis::Ptr<Noesis::IView> view = Noesis::GUI::CreateView(content);
    if (!view)
    {
        return nullptr;
    }

    // Configure Main-owned view. IRenderer::Init is deferred until RT first
    // consumes the document.
    view->SetSize(m_ScreenWidth, m_ScreenHeight);

    // Construct the document (which registers the view with the input adapter)
    auto doc = std::make_shared<VansNoesisDocument>(
        std::move(view),
        std::move(content),
        xamlUri,
        m_InputAdapter.get());

    {
        std::lock_guard<std::mutex> lock(m_DocumentsMutex);
        m_Documents.push_back(doc);
    }
    return doc;
}

void VansNoesisUISystem::UnloadDocument(const std::shared_ptr<VansNoesisDocument>& doc)
{
    VANS_ASSERT_MAIN_THREAD();
    std::lock_guard<std::mutex> lock(m_DocumentsMutex);
    auto it = std::find(m_Documents.begin(), m_Documents.end(), doc);
    if (it != m_Documents.end())
    {
        // Keep the View alive until RT has called IRenderer::Shutdown. This
        // protects an N-1 render snapshot while Main unloads frame N.
        m_RetiredDocuments.push_back(*it);
        m_Documents.erase(it);
    }
}

std::vector<std::shared_ptr<VansNoesisDocument>>
VansNoesisUISystem::SnapshotActiveDocuments() const
{
    std::lock_guard<std::mutex> lock(m_DocumentsMutex);
    return m_Documents;
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
    const char* fontFallbacks[] = {
        "Arial",
        "Microsoft YaHei",
        "SimSun",
        "SimHei",
        "Segoe UI",
        "Noto Sans CJK SC",
        "Source Han Sans SC"
    };
    Noesis::GUI::SetFontFallbacks(fontFallbacks,
        static_cast<uint32_t>(sizeof(fontFallbacks) / sizeof(fontFallbacks[0])));

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

} // namespace VansRuntime
