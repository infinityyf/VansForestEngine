#pragma once
#include <string>
#include <memory>

namespace VansGraphics
{
    class VansVKDevice;
}

namespace VansRuntime
{
    class VansUIDocument;
    class VansUIViewModel;
    class VansUIScreenManager;

    // UI 子系统初始化描述
    struct VansUIInitDesc
    {
        const char* m_LicenseName      = "";
        const char* m_LicenseKey       = "";
        uint32_t    m_Width            = 1920;
        uint32_t    m_Height           = 1080;
        // Debug / Profile 配置开启，Release 必须设为 false
        bool        m_EnableHotReload  = true;
        // 仅 Debug 配置开启，允许 Noesis Inspector 远程连接
        bool        m_EnableInspector  = false;
    };

    // 运行时 UI 子系统单例
    // 生命周期由引擎主循环管理：Initialize → Update / Render → Shutdown
    class VansUISystem
    {
    public:
        static VansUISystem& Get();

        // ── 生命周期 ───────────────────────────────────────────────

        // 在引擎 Vulkan 设备创建完成后、场景加载前调用
        bool Initialize(const VansUIInitDesc& desc);

        // 在 Vulkan 设备就绪时调用（含 device 指针）
        // 如果已经调用过 Initialize()，此接口会补全 Noesis 初始化
        bool InitializeWithDevice(const VansUIInitDesc& desc,
                                   VansGraphics::VansVKDevice* device);

        // 在场景卸载完成后、VansVKDevice 销毁前调用
        void Shutdown();

        // ── 每帧接口 ──────────────────────────────────────────────

        // 在 VansInputManager::Update() 之后、场景渲染之前调用
        // 负责投递输入事件、推进动画、刷新绑定
        void Update(float deltaTime);

        // ── 文档管理 ──────────────────────────────────────────────

        // xamlPath：项目相对路径（如 "UI/Views/HUD.xaml"）
        //           或引擎协议路径（如 "engine://UI/...xaml"）
        std::shared_ptr<VansUIDocument> LoadDocument(const std::string& xamlPath);

        void UnloadDocument(const std::shared_ptr<VansUIDocument>& document);

        // ── 窗口尺寸 ──────────────────────────────────────────────

        // 窗口 Resize 时调用，通知所有 Document 更新 View 尺寸
        void SetScreenSize(uint32_t width, uint32_t height);

        // ── ScreenManager ─────────────────────────────────────────

        VansUIScreenManager& GetScreenManager();

        // ── 输入捕获状态（供 Gameplay 层判断是否屏蔽角色输入） ────

        bool WantsMouse()    const;
        bool WantsKeyboard() const;

        // ── 调试 ──────────────────────────────────────────────────

        bool IsInitialized() const;

    private:
        VansUISystem() = default;
        ~VansUISystem() = default;
        VansUISystem(const VansUISystem&) = delete;
        VansUISystem& operator=(const VansUISystem&) = delete;

        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

} // namespace VansRuntime
