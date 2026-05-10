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
#include <algorithm>
#include <unordered_map>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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

// ── static 工具 ───────────────────────────────────────────────────────

// compact stem: 去扩展名 → 小写 → 去空格
// "Segoe UI Bold.ttf" → "segoeuibold"
std::string VansNoesisFontProvider::MakeCompactStem(const std::string& filename)
{
    auto dotPos  = filename.rfind('.');
    std::string stem = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
    std::string out;
    out.reserve(stem.size());
    for (char c : stem) if (c != ' ') out += (char)tolower(c);
    return out;
}

// 生成 family 查找键列表（原小写 + 紧凑形式）
std::vector<std::string> VansNoesisFontProvider::MakeFamilyKeys(const char* familyName)
{
    std::string lower = familyName;
    for (auto& c : lower) c = (char)tolower(c);

    std::string compact;
    compact.reserve(lower.size());
    for (char c : lower) if (c != ' ') compact += c;

    std::vector<std::string> keys;
    keys.push_back(lower);
    if (compact != lower) keys.push_back(compact);
    return keys;
}

// ── 目录扫描（懒加载，每目录扫描一次）────────────────────────────────

void VansNoesisFontProvider::ScanDirIfNeeded(const std::string& dir) const
{
    auto& cache = m_DirCache[dir];
    if (cache.scanned) return;
    cache.scanned = true;

    if (!std::filesystem::is_directory(dir)) return;

    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        const auto ext = entry.path().extension().string();
        if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;

        const std::string filename = entry.path().filename().string();
        const std::string stem     = MakeCompactStem(filename);
        cache.stemToPath[stem]     = entry.path().string();
    }
}

std::vector<std::string> VansNoesisFontProvider::BuildSearchDirs() const
{
    std::vector<std::string> dirs;

    const auto& assetDirs = m_ProjectManager->GetConfig().assetDirectories;
    auto it = assetDirs.find("ui");
    if (it != assetDirs.end())
        dirs.push_back(m_PathResolver->Resolve(it->second + "/Fonts"));

    dirs.push_back(m_PathResolver->GetEngineRoot() + "EngineAssets/UI/Fonts");

#if defined(_WIN32)
    wchar_t winDir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(winDir, MAX_PATH) > 0)
    {
        const std::filesystem::path sysFontDir =
            std::filesystem::path(winDir) / L"Fonts";
        dirs.push_back(sysFontDir.string());
    }
#endif

    return dirs;
}

const std::string& VansNoesisFontProvider::ResolveFamilyPath(const char* familyName) const
{
    std::string lowerFamily = familyName;
    for (auto& c : lowerFamily) c = (char)tolower(c);

    auto cached = m_FamilyPathCache.find(lowerFamily);
    if (cached != m_FamilyPathCache.end()) return cached->second;

    const auto keys = MakeFamilyKeys(familyName);
    const auto dirs = BuildSearchDirs();

    for (const auto& dir : dirs)
    {
        ScanDirIfNeeded(dir);

        const auto& dirCache = m_DirCache[dir];

        for (const auto& key : keys)
        {
            // 精确 compact-stem 命中
            auto exactIt = dirCache.stemToPath.find(key);
            if (exactIt != dirCache.stemToPath.end())
                return m_FamilyPathCache[lowerFamily] = exactIt->second;

            // 前缀命中（处理 Bold/Italic 变体，如 "segoeui" 匹配 "segoeuibold"）
            for (const auto& [stem, path] : dirCache.stemToPath)
            {
                if (stem.size() >= key.size() &&
                    stem.compare(0, key.size(), key) == 0)
                {
                    return m_FamilyPathCache[lowerFamily] = path;
                }
            }
        }
    }

    return m_FamilyPathCache[lowerFamily] = std::string{};
}

// ── Noesis FontProvider 接口 ──────────────────────────────────────────

Noesis::FontSource VansNoesisFontProvider::MatchFont(
    const Noesis::Uri&   /*baseUri*/,
    const char*          familyName,
    Noesis::FontWeight&  /*weight*/,
    Noesis::FontStretch& /*stretch*/,
    Noesis::FontStyle&   /*style*/)
{
    Noesis::FontSource result{};

    const std::string& path = ResolveFamilyPath(familyName);
    if (path.empty()) return result;

    // 字体数据缓存：避免每次 MatchFont 重新读磁盘
    auto& data = m_FontDataCache[path];
    if (data.empty())
    {
        data = ReadFileToBuffer(path);
        if (data.empty())
        {
            m_FontDataCache.erase(path);
            return result;
        }
    }

    result.file = Noesis::Ptr<Noesis::Stream>(
        *new Noesis::MemoryStream(data.data(), static_cast<uint32_t>(data.size())));

    const std::string filename = std::filesystem::path(path).filename().string();
    m_FilenameCache.push_back(filename);
    result.filename  = m_FilenameCache.back().c_str();
    result.faceIndex = 0;

    VANS_LOG("[NoesisFont] MatchFont '" << familyName << "' -> '" << path << "'");
    return result;
}

bool VansNoesisFontProvider::FamilyExists(const Noesis::Uri& /*baseUri*/,
                                           const char* familyName)
{
    return !ResolveFamilyPath(familyName).empty();
}

} // namespace VansRuntime
