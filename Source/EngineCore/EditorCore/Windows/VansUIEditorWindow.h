#pragma once
#include "VansBaseWindowComponent.h"
#include <string>
#include <memory>

namespace VansRuntime
{
    class VansUIDocument;
}

namespace VansGraphics
{
    // -----------------------------------------------------------------------
    // UI Editor Window — XAML 预览 / 热重载面板
    //
    // 功能：
    //   1. XAML 路径输入框（支持 engine:// 协议和项目相对路径）
    //   2. Load Preview 按钮 — 调用 VansUISystem::LoadDocument()
    //   3. Reload (F5) — 热重载当前加载的 XAML 文件
    //   4. 元数据显示：当前 source path、文档可视状态
    //   5. 备注：Noesis IView 渲染结果目前无法作为 ImGui Image 显示，
    //            预览视口以占位文本代替（后续集成 Off-screen Render Target）
    // -----------------------------------------------------------------------
    class VansUIEditorWindow : public VansBaseWindowComponent
    {
    public:
        VansUIEditorWindow();

        void ShowWindow(VansVKDevice& device) override;

    private:
        void LoadPreview(VansVKDevice& device);
        void UnloadPreview();
        void DrawMetaPanel();
        void DrawPreviewViewport();

        char   m_XamlPathBuf[512] = {};
        std::shared_ptr<VansRuntime::VansUIDocument> m_PreviewDocument;

        // 上一帧 F5 按下状态（防止持续触发）
        bool m_ReloadKeyWasDown = false;
    };
}
