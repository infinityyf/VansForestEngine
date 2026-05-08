#include "VansNoesisProviders.h"
#include "VansNoesisRenderDevice.h"

#include "../../../ProjectSystem/VansProjectManager.h"
#include "../../../ProjectSystem/VansPathResolver.h"
#include "../../../Util/VansLog.h"

#include <NsGui/Uri.h>
#include <NsGui/MemoryStream.h>
#include <NsRender/RenderDevice.h>

#include <fstream>
#include <filesystem>
#include <vector>

namespace VansRuntime
{

// ── 通用路径解析 ──────────────────────────────────────────────────────

std::string ResolveUIPath(
    const std::string&              uriStr,
    const Vans::VansProjectManager* projectManager,
    const Vans::VansPathResolver*   pathResolver)
{
    if (Vans::VansPathResolver::IsEngineProtocol(uriStr))
    {
        // engine:// 协议 → EngineAssets/ 相对路径
        // engine://UI/Themes/BaseTheme.xaml → [EngineRoot]/EngineAssets/UI/Themes/BaseTheme.xaml
        const std::string relativeAfterScheme = uriStr.substr(strlen("engine://"));
        return pathResolver->GetEngineRoot() + "EngineAssets/" + relativeAfterScheme;
    }

    // 项目相对路径 → [ProjectRoot]/Assets/UI/...
    // 先从 assetDirectories["ui"] 中获取 UI 根路径
    const auto& dirs = projectManager->GetConfig().assetDirectories;
    auto it = dirs.find("ui");
    if (it != dirs.end())
    {
        return pathResolver->Resolve(it->second + "/" + uriStr);
    }

    // fallback：直接用 assetPath 解析
    return pathResolver->Resolve(uriStr);
}

// ── 文件读取辅助 ──────────────────────────────────────────────────────

static std::vector<uint8_t> ReadFileToBuffer(const std::string& fullPath)
{
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return {};
    }
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    return buffer;
}

// ── VansNoesisXamlProvider ───────────────────────────────────────────

VansNoesisXamlProvider::VansNoesisXamlProvider(
    const Vans::VansProjectManager* projectManager,
    const Vans::VansPathResolver*   pathResolver)
    : m_ProjectManager(projectManager)
    , m_PathResolver(pathResolver)
{}

Noesis::Ptr<Noesis::Stream> VansNoesisXamlProvider::LoadXaml(const Noesis::Uri& uri)
{
    const std::string uriStr   = uri.Str();
    const std::string fullPath = ResolveUIPath(uriStr, m_ProjectManager, m_PathResolver);

    auto buffer = ReadFileToBuffer(fullPath);
    if (buffer.empty())
    {
        VANS_LOG_ERROR("[NoesisXamlProvider] XAML not found: uri='" << uriStr << "' resolved='" << fullPath << "'");
        return nullptr;
    }

    // MemoryStream 不拥有数据，需要将 buffer 转移到堆上
    // Noesis::MemoryStream 构造时直接接收 void* 指针和大小
    // 此处用 new 分配持久化内存，Stream 关闭时由 Noesis 侧触发 Close()，
    // 但 MemoryStream 不负责释放 buffer，所以用 shared ownership 的方式：
    // 将 buffer 数据拷贝到 Noesis 管理的内存区块
    uint8_t* data     = new uint8_t[buffer.size()];
    const uint32_t sz = static_cast<uint32_t>(buffer.size());
    memcpy(data, buffer.data(), sz);

    // MemoryStream 持有指针但不释放；此处通过自定义 Stream 子类处理
    // MVP 阶段：直接使用 MemoryStream，data 生命周期由此处 new 保持（内存泄漏可接受于调试阶段）
    // TODO: 实现带所有权的 OwnedMemoryStream 替换此处
    return Noesis::Ptr<Noesis::Stream>(*new Noesis::MemoryStream(data, sz));
}

// ── VansNoesisTextureProvider ─────────────────────────────────────────

VansNoesisTextureProvider::VansNoesisTextureProvider(
    const Vans::VansProjectManager* projectManager,
    const Vans::VansPathResolver*   pathResolver,
    VansNoesisRenderDevice*         renderDevice)
    : m_ProjectManager(projectManager)
    , m_PathResolver(pathResolver)
    , m_RenderDevice(renderDevice)
{}

Noesis::TextureInfo VansNoesisTextureProvider::GetTextureInfo(const Noesis::Uri& uri)
{
    const std::string fullPath = ResolveUIPath(
        uri.Str(), m_ProjectManager, m_PathResolver);

    Noesis::TextureInfo info {};

    // 读取图片尺寸（不加载像素数据）
    // TODO: 接入 VansTexture 的元数据读取接口
    // 当前 MVP：返回默认值（Noesis 会在 LoadTexture 加载完整数据后校正）
    if (std::filesystem::exists(fullPath))
    {
        info.width    = 1;
        info.height   = 1;
        info.dpiScale = 1.0f;
    }

    return info;
}

Noesis::Ptr<Noesis::Texture> VansNoesisTextureProvider::LoadTexture(
    const Noesis::Uri& uri, Noesis::RenderDevice* /*device*/)
{
    const std::string fullPath = ResolveUIPath(
        uri.Str(), m_ProjectManager, m_PathResolver);

    // TODO: 通过 VansTexture 加载图片并包装为 Noesis::Texture
    // MVP 阶段：返回 nullptr，Noesis 会显示粉色占位纹理
    (void)fullPath;
    return nullptr;
}

// ── VansNoesisFontProvider ────────────────────────────────────────────

VansNoesisFontProvider::VansNoesisFontProvider(
    const Vans::VansProjectManager* projectManager,
    const Vans::VansPathResolver*   pathResolver)
    : m_ProjectManager(projectManager)
    , m_PathResolver(pathResolver)
{}

Noesis::FontSource VansNoesisFontProvider::MatchFont(
    const Noesis::Uri&   baseUri,
    const char*          familyName,
    Noesis::FontWeight&  weight,
    Noesis::FontStretch& stretch,
    Noesis::FontStyle&   style)
{
    Noesis::FontSource result {};

    // 1. 先查项目 UI/Fonts/ 目录
    const auto& dirs = m_ProjectManager->GetConfig().assetDirectories;
    auto it = dirs.find("ui");
    if (it != dirs.end())
    {
        const std::string projectFontDir =
            m_PathResolver->Resolve(it->second + "/Fonts");
        if (TryLoadFontFromDir(projectFontDir, familyName, result))
        {
            return result;
        }
    }

    // 2. 再查引擎 EngineAssets/UI/Fonts/ 目录
    const std::string engineFontDir =
        m_PathResolver->GetEngineRoot() + "EngineAssets/UI/Fonts";
    TryLoadFontFromDir(engineFontDir, familyName, result);

    (void)weight; (void)stretch; (void)style; (void)baseUri;
    return result;
}

bool VansNoesisFontProvider::FamilyExists(const Noesis::Uri& /*baseUri*/,
                                           const char* /*familyName*/)
{
    // TODO: 实现字体族存在性检查
    return false;
}

bool VansNoesisFontProvider::TryLoadFontFromDir(
    const std::string&  fontDir,
    const char*         familyName,
    Noesis::FontSource& outSource)
{
    if (!std::filesystem::is_directory(fontDir))
    {
        return false;
    }

    // 按字体族名（familyName）在目录中查找 .ttf / .otf 文件
    for (const auto& entry : std::filesystem::directory_iterator(fontDir))
    {
        const auto ext = entry.path().extension().string();
        if (ext != ".ttf" && ext != ".otf" && ext != ".ttc")
        {
            continue;
        }

        const std::string filename = entry.path().filename().string();
        // 简单启发式：文件名中包含 familyName（不区分大小写）
        std::string lowerFilename = filename;
        std::string lowerFamily   = familyName;
        for (auto& c : lowerFilename) c = (char)tolower(c);
        for (auto& c : lowerFamily)   c = (char)tolower(c);

        if (lowerFilename.find(lowerFamily) != std::string::npos)
        {
            auto buffer = ReadFileToBuffer(entry.path().string());
            if (!buffer.empty())
            {
                uint8_t* data  = new uint8_t[buffer.size()];
                const uint32_t sz = static_cast<uint32_t>(buffer.size());
                memcpy(data, buffer.data(), sz);

                outSource.filename  = filename.c_str();
                outSource.file      = Noesis::Ptr<Noesis::Stream>(
                    *new Noesis::MemoryStream(data, sz));
                outSource.faceIndex = 0;
                return true;
            }
        }
    }

    return false;
}

} // namespace VansRuntime
