#pragma once
#include "../../Public/VansUISystem.h"
#include "../../Public/VansUIDocument.h"
#include <NsGui/IntegrationAPI.h>
#include <NsCore/Ptr.h>
#include <vector>
#include <memory>
#include <string>

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

        // 获取 RenderDevice（供 VansUIRenderPass 使用）
        VansNoesisRenderDevice* GetRenderDevice() const { return m_RenderDevice.get(); }

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
    };

} // namespace VansRuntime
