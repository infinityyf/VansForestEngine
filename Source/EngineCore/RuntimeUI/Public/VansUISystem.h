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

        // 每帧由 SceneWindow 调用，告知场景图像在屏幕空间的位置和尺寸，
        // 供 InputAdapter 将原始 GLFW 坐标映射到 Noesis View 局部坐标
        void SetSceneViewport(float screenX, float screenY, float screenW, float screenH);

        // ── ScreenManager ─────────────────────────────────────────

        VansUIScreenManager& GetScreenManager();

        // ── 输入捕获状态（供 Gameplay 层判断是否屏蔽角色输入） ────

        bool WantsMouse()    const;
        bool WantsKeyboard() const;

        // ── 调试 ──────────────────────────────────────────────────

        bool IsInitialized() const;

        // ── 每帧渲染接口（由渲染器在对应阶段调用） ────────────────

        // 第一步：在 BeginUIRenderPass 之前调用，执行 Noesis 离屏渲染
        // nativeCmdBuffer：VkCommandBuffer 句柄（以 void* 传递，避免引入 Vulkan 头）
        void RenderOffscreen(void* nativeCmdBuffer);

        // 第二步：在 BeginUIRenderPass 与 EndUIRenderPass 之间调用
        // nativeRenderPass：VkRenderPass 句柄（供 Noesis 懒编译 PSO 使用）
        // sampleCount：MSAA 采样数，无 MSAA 传 1
        void RenderDocuments(void* nativeRenderPass, uint32_t sampleCount);

    private:
        VansUISystem() = default;
        ~VansUISystem() = default;
        VansUISystem(const VansUISystem&) = delete;
        VansUISystem& operator=(const VansUISystem&) = delete;

        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

} // namespace VansRuntime
