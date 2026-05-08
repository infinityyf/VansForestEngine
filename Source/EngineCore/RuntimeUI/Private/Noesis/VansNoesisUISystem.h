#pragma once
#include "../../Public/VansUISystem.h"
#include "../../Public/VansUIDocument.h"
#include <NsGui/IntegrationAPI.h>
#include <NsCore/Ptr.h>
#include <vector>
#include <memory>
#include <string>

#include "vulkan/vulkan.h"

namespace VansGraphics
{
    class VansVKDevice;
}

namespace VansRuntime
{
    class VansNoesisXamlProvider;
    class VansNoesisTextureProvider;
    class VansNoesisFontProvider;
    class VansNoesisRenderDevice;
    class VansNoesisDocument;
    class VansNoesisInputAdapter;

    // Noesis 适配层核心，是 VansUISystem::Impl 的实际持有对象
    // 负责 Noesis 初始化、Provider 安装、类型注册、Document 生命周期管理
    class VansNoesisUISystem
    {
    public:
        explicit VansNoesisUISystem(VansGraphics::VansVKDevice* device);
        ~VansNoesisUISystem();

        // 在 Vulkan 设备就绪后调用
        bool Initialize(const VansUIInitDesc& desc);

        // 释放所有 Noesis 资源，必须在 VansVKDevice 销毁前调用
        void Shutdown();

        // 每帧驱动：输入分发、动画更新、绑定刷新
        void Update(float deltaTime);

        // 通知所有 Document 调整 View 尺寸
        void SetScreenSize(uint32_t width, uint32_t height);

        // 加载 / 卸载文档
        std::shared_ptr<VansNoesisDocument> LoadDocument(const std::string& xamlPath);
        void UnloadDocument(const std::shared_ptr<VansNoesisDocument>& doc);

        // 查询输入捕获状态
        bool WantsMouse()    const;
        bool WantsKeyboard() const;

        bool IsInitialized() const { return m_Initialized; }

        // 每帧由 VansSceneWindow 更新场景图像的屏幕位置与尺寸，
        // 以便 InputAdapter 将原始 GLFW 坐标变换为 Noesis View 局部坐标
        void SetSceneViewport(float screenX, float screenY, float screenW, float screenH);

        // 获取 RenderDevice（供 VansUIRenderPass 使用）
        VansNoesisRenderDevice* GetRenderDevice() const { return m_RenderDevice.get(); }

        // ── 每帧渲染接口 ─────────────────────────────────────────────────
        // 第一步：在 BeginUIRenderPass 之前调用，执行 Noesis 离屏渲染
        // cmd：当前帧正在录制中的 Vulkan 命令缓冲区句柄
        void RenderOffscreenPass(VkCommandBuffer cmd);

        // 第二步：在 BeginUIRenderPass / EndUIRenderPass 之间调用
        // renderPass：激活的 VkRenderPass 句柄（供 Noesis 懒编译 PSO 用）
        // sampleCount：MSAA 采样数，无 MSAA 时传 1
        void RenderDocumentsPass(VkRenderPass renderPass, uint32_t sampleCount);

    private:
        void SetupLogHandler();
        void SetupErrorHandler();
        void SetupLicense(const VansUIInitDesc& desc);
        void InstallProviders();
        void RegisterTypes();
        void LoadGlobalTheme();

        VansGraphics::VansVKDevice*                      m_Device         = nullptr;
        bool                                             m_Initialized    = false;
        uint32_t                                         m_ScreenWidth    = 1920;
        uint32_t                                         m_ScreenHeight   = 1080;

        // Providers（Noesis::Ptr 管理 Noesis 侧引用计数）
        Noesis::Ptr<VansNoesisXamlProvider>    m_XamlProvider;
        Noesis::Ptr<VansNoesisTextureProvider> m_TextureProvider;
        Noesis::Ptr<VansNoesisFontProvider>    m_FontProvider;

        // RenderDevice（Vulkan 后端）
        std::unique_ptr<VansNoesisRenderDevice> m_RenderDevice;

        // InputAdapter（将 VansInputManager 事件路由到各 Document 的 IView）
        std::unique_ptr<VansNoesisInputAdapter> m_InputAdapter;

        // 所有活跃 Document
        std::vector<std::shared_ptr<VansNoesisDocument>> m_Documents;

        // 累计时间（秒），用于 Noesis IView::Update(totalSeconds)
        double m_TotalTimeSeconds = 0.0;

        // 帧计数器（单调递增），用于 VKFactory::SetCommandBuffer 的资源生命周期管理
        uint64_t m_FrameNumber = 0;

        // 认为安全可回收的帧偏移（最保守：与 MAX_FRAMES_IN_FLIGHT 对齐即可）
        static constexpr uint64_t k_MaxFramesInFlight = 2;
    };

} // namespace VansRuntime
