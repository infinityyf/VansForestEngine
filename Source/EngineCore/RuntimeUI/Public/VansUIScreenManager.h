#pragma once
#include <string>
#include <memory>
#include <stack>
#include <unordered_map>
#include <vector>

#include "VansUIRuntimeHandles.h"

namespace VansRuntime
{
    class VansUIDocument;
    class VansUIViewModel;
    class VansUISystem;
    class VansUIScreen;

    // 管理多个界面的叠加、压栈与模态弹窗
    // 由 VansUISystem 持有，通过 VansUISystem::GetScreenManager() 访问
    class VansUIScreenManager
    {
    public:
        explicit VansUIScreenManager(VansUISystem& uiSystem);
        ~VansUIScreenManager();

        // ── 栈式界面（PauseMenu、Inventory 等） ──────────────────

        // 压入一个新界面，自动加载 XAML 并显示
        void PushScreen(const std::string& xamlAssetGuid,
                        std::shared_ptr<VansUIViewModel> vm = nullptr);
        std::shared_ptr<VansUIScreen> LoadScreen(const std::string& configPath,
                                                 std::shared_ptr<VansUIViewModel> vm = nullptr);
        std::shared_ptr<VansUIScreen> PushScreenConfig(const std::string& configPath,
                                                       std::shared_ptr<VansUIViewModel> vm = nullptr);
        std::shared_ptr<VansUIScreen> ReplaceScreenConfig(const std::string& configPath,
                                                          std::shared_ptr<VansUIViewModel> vm = nullptr);
        bool PreloadScreen(const std::string& configPath);
        void ReleaseScreen(const std::string& configPath);
        std::shared_ptr<VansUIScreen> ReloadScreen(const std::string& configPath,
                                                   std::shared_ptr<VansUIViewModel> vm = nullptr);

        // 弹出当前顶层界面并隐藏
        void PopScreen();

        // 替换当前顶层界面（先 Pop 再 Push，不增加栈深度）
        void ReplaceScreen(const std::string& xamlAssetGuid,
                           std::shared_ptr<VansUIViewModel> vm = nullptr);

        // ── 常驻 HUD ─────────────────────────────────────────────

        // 设置 HUD（不进栈，常驻于最底层）
        void SetHUD(const std::string& xamlAssetGuid,
                    std::shared_ptr<VansUIViewModel> vm = nullptr);
        std::shared_ptr<VansUIScreen> SetHUDConfig(const std::string& configPath,
                                                   std::shared_ptr<VansUIViewModel> vm = nullptr);
        void ShowHUD();
        void HideHUD();

        // ── 模态弹窗 ──────────────────────────────────────────────

        // 显示模态弹窗（会屏蔽底层输入）
        void ShowModal(const std::string& xamlAssetGuid,
                       std::shared_ptr<VansUIViewModel> vm = nullptr);
        std::shared_ptr<VansUIScreen> ShowModalConfig(const std::string& configPath,
                                                      std::shared_ptr<VansUIViewModel> vm = nullptr);
        void HideModal();
        bool IsModalVisible() const;

        // ── 叠加层（通知、Tooltip 等） ────────────────────────────

        // name 为叠加层唯一标识符（可同时存在多个）
        void ShowOverlay(const std::string& name,
                         const std::string& xamlAssetGuid,
                         std::shared_ptr<VansUIViewModel> vm = nullptr);
        void HideOverlay(const std::string& name);

        // ── 查询 ──────────────────────────────────────────────────

        // 当前栈顶界面（不含 HUD / Modal / Overlay）
        std::shared_ptr<VansUIDocument> GetTopScreen() const;
        std::shared_ptr<VansUIScreen> GetScreen(VansUIHandleId handle) const;
        std::shared_ptr<VansUIScreen> GetScreenByName(const std::string& name) const;

        // 栈是否为空
        bool IsScreenStackEmpty() const;

        // ── 全部关闭 ─────────────────────────────────────────────

        void CloseAll();
        void CloseScreen(VansUIHandleId handle);
        void CloseScreenByName(const std::string& name);

    private:
        std::shared_ptr<VansUIScreen> CreateScreenFromConfig(
            const std::string& configPath,
            std::shared_ptr<VansUIViewModel> vm);
        std::shared_ptr<VansUIScreen> TakePreloadedScreen(
            const std::string& configPath,
            std::shared_ptr<VansUIViewModel> vm);
        void RegisterScreen(const std::shared_ptr<VansUIScreen>& screen);
        void BindConfiguredEvents(const std::shared_ptr<VansUIScreen>& screen);
        void UnloadScreen(const std::shared_ptr<VansUIScreen>& screen);

        VansUISystem& m_UISystem;
        VansUIHandleId m_NextHandle = 1;
        std::vector<std::shared_ptr<VansUIScreen>> m_Screens;
        std::vector<std::shared_ptr<VansUIScreen>> m_ConfigScreenStack;
        std::unordered_map<std::string, std::shared_ptr<VansUIScreen>> m_PreloadedScreens;

        // HUD 常驻文档
        std::shared_ptr<VansUIDocument> m_HUDDocument;

        // 界面栈（栈顶为当前活跃界面）
        std::stack<std::shared_ptr<VansUIDocument>> m_ScreenStack;

        // 模态弹窗（同时只允许一个）
        std::shared_ptr<VansUIDocument> m_ModalDocument;

        // 叠加层映射
        std::unordered_map<std::string, std::shared_ptr<VansUIDocument>> m_Overlays;
    };

} // namespace VansRuntime
