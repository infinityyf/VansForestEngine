#pragma once
#include <NsGui/XamlProvider.h>
#include <NsGui/TextureProvider.h>
#include <NsGui/FontProvider.h>
#include <NsRender/Texture.h>
#include <NsCore/Ptr.h>
#include <string>

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
    // 优先在项目 UI/Fonts/ 中查找，其次在 engine://UI/Fonts/ 中查找
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

        // 尝试从给定路径目录中加载匹配的字体，返回是否成功
        bool TryLoadFontFromDir(const std::string&   fontDir,
                                const char*          familyName,
                                Noesis::FontSource&  outSource);
    };

} // namespace VansRuntime
