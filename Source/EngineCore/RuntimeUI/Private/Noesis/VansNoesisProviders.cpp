#include "VansNoesisProviders.h"
#include "VansNoesisRenderDevice.h"

#include "../../../ProjectSystem/VansProjectManager.h"
#include "../../../ProjectSystem/VansPathResolver.h"
#include "../../../AssetCore/Storage/VansFileStorage.h"
#include "../../../Util/VansLog.h"

#include <NsGui/Uri.h>
#include <NsGui/MemoryStream.h>
#include <NsRender/RenderDevice.h>

#include <filesystem>
#include <vector>
#include <algorithm>
#include <cstring>
#include <unordered_map>

#include <stb_image.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace VansRuntime
{

namespace
{
    class OwnedMemoryStream final : public Noesis::Stream
    {
    public:
        explicit OwnedMemoryStream(std::vector<uint8_t>&& data)
            : m_Data(std::move(data))
        {}

        void SetPosition(uint32_t pos) override
        {
            m_Offset = std::min<uint32_t>(pos, GetLength());
        }

        uint32_t GetPosition() const override
        {
            return m_Offset;
        }

        uint32_t GetLength() const override
        {
            return static_cast<uint32_t>(m_Data.size());
        }

        uint32_t Read(void* buffer, uint32_t size) override
        {
            if (buffer == nullptr || size == 0 || m_Offset >= GetLength())
            {
                return 0;
            }

            const uint32_t readSize = std::min<uint32_t>(size, GetLength() - m_Offset);
            std::memcpy(buffer, m_Data.data() + m_Offset, readSize);
            m_Offset += readSize;
            return readSize;
        }

        const void* GetMemoryBase() const override
        {
            return m_Data.empty() ? nullptr : m_Data.data();
        }

        void Close() override
        {
            m_Data.clear();
            m_Data.shrink_to_fit();
            m_Offset = 0;
        }

    private:
        std::vector<uint8_t> m_Data;
        uint32_t m_Offset = 0;
    };
}

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
    std::string normalized = uriStr;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.rfind("Assets/", 0) == 0)
        return pathResolver->Resolve(normalized);

    const auto& dirs = projectManager->GetConfig().assetDirectories;
    auto it = dirs.find("ui");
    if (it != dirs.end())
    {
        std::string uiRoot = it->second;
        std::replace(uiRoot.begin(), uiRoot.end(), '\\', '/');
        while (!uiRoot.empty() && uiRoot.back() == '/')
            uiRoot.pop_back();
        if (normalized.rfind("UI/", 0) == 0)
            return pathResolver->Resolve(uiRoot + "/" + normalized.substr(3));
        return pathResolver->Resolve(uiRoot + "/" + normalized);
    }

    return pathResolver->Resolve(normalized);
}
// ── 文件读取辅助 ──────────────────────────────────────────────────────

static std::vector<uint8_t> ReadFileToBuffer(const std::string& fullPath)
{
    std::string bytes;
    std::string error;
    if (!Vans::VansFileStorage::ReadAllBytes(fullPath, bytes, error) || bytes.empty())
        return {};
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
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

    return Noesis::Ptr<Noesis::Stream>(*new OwnedMemoryStream(std::move(buffer)));
}

// ── VansNoesisTextureProvider ─────────────────────────────────────────

VansNoesisTextureProvider::VansNoesisTextureProvider(
    const Vans::VansProjectManager* projectManager,
    const Vans::VansPathResolver*   pathResolver,
    VansNoesisRenderDevice*         renderDevice)
    : m_ProjectManager(projectManager)
    , m_PathResolver(pathResolver)
{
    (void)renderDevice;
}

Noesis::TextureInfo VansNoesisTextureProvider::GetTextureInfo(const Noesis::Uri& uri)
{
    const std::string fullPath = ResolveUIPath(
        uri.Str(), m_ProjectManager, m_PathResolver);

    auto buffer = ReadFileToBuffer(fullPath);
    if (buffer.empty())
    {
        VANS_LOG_ERROR("[NoesisTextureProvider] Texture not found: uri='" << uri.Str()
            << "' resolved='" << fullPath << "'");
        return {};
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(buffer.data(), static_cast<int>(buffer.size()), &width, &height, &channels))
    {
        VANS_LOG_ERROR("[NoesisTextureProvider] Cannot read texture info: uri='" << uri.Str()
            << "' resolved='" << fullPath << "'");
        return {};
    }

    Noesis::TextureInfo info{};
    info.width = static_cast<uint32_t>(width);
    info.height = static_cast<uint32_t>(height);
    info.dpiScale = 1.0f;
    return info;
}

Noesis::Ptr<Noesis::Texture> VansNoesisTextureProvider::LoadTexture(
    const Noesis::Uri& uri, Noesis::RenderDevice* device)
{
    if (!device)
        return nullptr;

    const std::string fullPath = ResolveUIPath(
        uri.Str(), m_ProjectManager, m_PathResolver);

    auto buffer = ReadFileToBuffer(fullPath);
    if (buffer.empty())
    {
        VANS_LOG_ERROR("[NoesisTextureProvider] Texture not found: uri='" << uri.Str()
            << "' resolved='" << fullPath << "'");
        return nullptr;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* image = stbi_load_from_memory(
        buffer.data(), static_cast<int>(buffer.size()), &width, &height, &channels, 4);
    if (!image || width <= 0 || height <= 0)
    {
        VANS_LOG_ERROR("[NoesisTextureProvider] Cannot decode texture: uri='" << uri.Str()
            << "' resolved='" << fullPath << "'");
        if (image)
            stbi_image_free(image);
        return nullptr;
    }

    const bool hasAlpha = channels == 2 || channels == 4;
    if (hasAlpha)
    {
        for (int i = 0; i < width * height; ++i)
        {
            stbi_uc* pixel = image + i * 4;
            const unsigned alpha = pixel[3];
            pixel[0] = static_cast<stbi_uc>((static_cast<unsigned>(pixel[0]) * alpha) / 255u);
            pixel[1] = static_cast<stbi_uc>((static_cast<unsigned>(pixel[1]) * alpha) / 255u);
            pixel[2] = static_cast<stbi_uc>((static_cast<unsigned>(pixel[2]) * alpha) / 255u);
        }
    }

    std::vector<std::vector<stbi_uc>> mipStorage;
    std::vector<const void*> mipData;
    mipData.push_back(image);

    int mipWidth = width;
    int mipHeight = height;
    while (mipWidth > 1 || mipHeight > 1)
    {
        const stbi_uc* source = mipStorage.empty() ? image : mipStorage.back().data();
        const int nextWidth = std::max(1, mipWidth / 2);
        const int nextHeight = std::max(1, mipHeight / 2);
        std::vector<stbi_uc> next(static_cast<size_t>(nextWidth) * nextHeight * 4u);

        for (int y = 0; y < nextHeight; ++y)
        {
            for (int x = 0; x < nextWidth; ++x)
            {
                unsigned rgba[4] = {};
                int samples = 0;
                for (int oy = 0; oy < 2; ++oy)
                {
                    for (int ox = 0; ox < 2; ++ox)
                    {
                        const int sx = std::min(mipWidth - 1, x * 2 + ox);
                        const int sy = std::min(mipHeight - 1, y * 2 + oy);
                        const stbi_uc* pixel = source + (static_cast<size_t>(sy) * mipWidth + sx) * 4u;
                        rgba[0] += pixel[0];
                        rgba[1] += pixel[1];
                        rgba[2] += pixel[2];
                        rgba[3] += pixel[3];
                        ++samples;
                    }
                }

                stbi_uc* out = next.data() + (static_cast<size_t>(y) * nextWidth + x) * 4u;
                out[0] = static_cast<stbi_uc>(rgba[0] / samples);
                out[1] = static_cast<stbi_uc>(rgba[1] / samples);
                out[2] = static_cast<stbi_uc>(rgba[2] / samples);
                out[3] = static_cast<stbi_uc>(rgba[3] / samples);
            }
        }

        mipStorage.push_back(std::move(next));
        mipData.push_back(mipStorage.back().data());
        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    const size_t labelOffset = fullPath.find_last_of("/\\");
    const std::string label = labelOffset == std::string::npos
        ? fullPath
        : fullPath.substr(labelOffset + 1);
    Noesis::Ptr<Noesis::Texture> texture = device->CreateTexture(
        label.c_str(),
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        static_cast<uint32_t>(mipData.size()),
        hasAlpha ? Noesis::TextureFormat::RGBA8 : Noesis::TextureFormat::RGBX8,
        mipData.data());

    stbi_image_free(image);
    return texture;
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
