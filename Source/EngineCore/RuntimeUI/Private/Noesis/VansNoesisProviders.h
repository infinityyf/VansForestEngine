#pragma once
#include <NsGui/XamlProvider.h>
#include <NsGui/TextureProvider.h>
#include <NsGui/FontProvider.h>
#include <NsRender/Texture.h>
#include <NsCore/Ptr.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace Vans
{
    class VansProjectManager;
    class VansPathResolver;
}

namespace VansGraphics
{
    class VansVKDevice;
}

namespace VansRuntime
{
    class VansNoesisRenderDevice;

    // ── 通用路径解析工具函数 ───────────────────────────────────────
    // 根据 uri 字符串判断是 engine:// 协议还是项目相对路径，返回完整磁盘路径
    std::string ResolveUIPath(
        const std::string&           uriStr,
        const Vans::VansProjectManager* projectManager,
        const Vans::VansPathResolver*   pathResolver);

    // ── VansNoesisXamlProvider ──────────────────────────────────────
    // URI 格式：
    //   engine://UI/Themes/BaseTheme.xaml  → EngineAssets/UI/Themes/BaseTheme.xaml
    //   UI/Views/HUD.xaml                  → [ProjectRoot]/Assets/UI/Views/HUD.xaml
    class VansNoesisXamlProvider : public Noesis::XamlProvider
    {
    public:
        VansNoesisXamlProvider(const Vans::VansProjectManager* projectManager,
                               const Vans::VansPathResolver*   pathResolver);

        Noesis::Ptr<Noesis::Stream> LoadXaml(const Noesis::Uri& uri) override;

    private:
        const Vans::VansProjectManager* m_ProjectManager;
        const Vans::VansPathResolver*   m_PathResolver;
    };

    // ── VansNoesisTextureProvider ──────────────────────────────────
    // 与 XamlProvider 相同的双路径解析逻辑
    class VansNoesisTextureProvider : public Noesis::TextureProvider
    {
    public:
        VansNoesisTextureProvider(const Vans::VansProjectManager* projectManager,
                                  const Vans::VansPathResolver*   pathResolver,
                                  VansNoesisRenderDevice*         renderDevice);

        Noesis::TextureInfo GetTextureInfo(const Noesis::Uri& uri) override;
        Noesis::Ptr<Noesis::Texture> LoadTexture(const Noesis::Uri& uri,
                                                  Noesis::RenderDevice* device) override;

    private:
        const Vans::VansProjectManager* m_ProjectManager;
        const Vans::VansPathResolver*   m_PathResolver;
        VansNoesisRenderDevice*         m_RenderDevice;
    };

    // ── VansNoesisFontProvider ─────────────────────────────────────
    // 搜索顺序：项目 UI/Fonts/ → 引擎 EngineAssets/UI/Fonts/ → Windows 系统字体目录
    // 每个目录仅扫描一次（scan-once cache），后续调用为纯内存查找，无文件 I/O。
    // 字体名称匹配同时尝试原始形式和去空格形式（"Segoe UI" → "segoeui"）。
    class VansNoesisFontProvider : public Noesis::FontProvider
    {
    public:
        VansNoesisFontProvider(const Vans::VansProjectManager* projectManager,
                               const Vans::VansPathResolver*   pathResolver);

        Noesis::FontSource MatchFont(const Noesis::Uri& baseUri,
                                     const char*         familyName,
                                     Noesis::FontWeight& weight,
                                     Noesis::FontStretch& stretch,
                                     Noesis::FontStyle&  style) override;

        bool FamilyExists(const Noesis::Uri& baseUri, const char* familyName) override;

    private:
        const Vans::VansProjectManager* m_ProjectManager;
        const Vans::VansPathResolver*   m_PathResolver;

        // ── 目录扫描缓存 ──────────────────────────────────────────────
        // 每个目录只扫描一次；key = 目录绝对路径，value = { compact_stem → full_path }
        // compact_stem：文件名去扩展名、转小写、去空格（"Segoe UI" → "segoeui"）
        struct DirCache
        {
            bool                                          scanned = false;
            std::unordered_map<std::string, std::string> stemToPath; // stem → full path
        };
        mutable std::unordered_map<std::string, DirCache> m_DirCache;

        // ── 字体族解析缓存 ────────────────────────────────────────────
        // key = lowercase family name，value = 完整文件路径（空字符串 = 未找到）
        mutable std::unordered_map<std::string, std::string> m_FamilyPathCache;

        // ── 字体数据缓存 ──────────────────────────────────────────────
        // key = 完整文件路径，value = 原始字节（避免每帧重新读取磁盘）
        mutable std::unordered_map<std::string, std::vector<uint8_t>> m_FontDataCache;

        // ── 文件名缓存 ────────────────────────────────────────────────
        // 保证 FontSource::filename 指针在系统生命周期内始终有效
        mutable std::vector<std::string> m_FilenameCache;

        // 扫描目录，若尚未扫描则填充 DirCache（幂等）
        void ScanDirIfNeeded(const std::string& dir) const;

        // 在已扫描的目录集合中查找 family，填充 m_FamilyPathCache，返回路径（空 = 未找到）
        const std::string& ResolveFamilyPath(const char* familyName) const;

        // 构建目录扫描列表（项目 → 引擎 → 系统）
        std::vector<std::string> BuildSearchDirs() const;

        // 工具：生成紧凑 stem = lowercase 文件名去扩展名去空格
        static std::string MakeCompactStem(const std::string& filename);

        // 工具：生成 family 的多种查找键
        static std::vector<std::string> MakeFamilyKeys(const char* familyName);
    };

} // namespace VansRuntime
