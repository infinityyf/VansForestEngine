#pragma once
#include <string>

namespace VansRuntime
{
    class VansUIViewModel;
    class VansUIElementHandle;

    // 代表一个加载中的 XAML 界面文档
    // 由 VansUISystem::LoadDocument 创建，VansNoesisDocument 实现
    class VansUIDocument
    {
    public:
        virtual ~VansUIDocument() = default;

        // ── 可见性控制 ────────────────────────────────────────────

        virtual void Show()                            = 0;
        virtual void Hide()                            = 0;
        virtual void SetVisible(bool visible)          = 0;
        virtual bool IsVisible()                 const = 0;

        // ── 尺寸同步（窗口 Resize 时由 VansUISystem 自动调用） ───

        virtual void SetSize(uint32_t width, uint32_t height) = 0;

        // ── 数据绑定 ──────────────────────────────────────────────

        // 设置 DataContext（vm 的生命周期由调用方管理）
        virtual void SetDataContext(VansUIViewModel* vm) = 0;

        // ── 元素访问 ──────────────────────────────────────────────

        // 查找 XAML 中 x:Name 命名的控件，返回弱引用句柄
        virtual VansUIElementHandle FindElement(const std::string& name) = 0;

        // ── 信息 ──────────────────────────────────────────────────

        // 返回加载时使用的 XAML 路径（用于调试和热重载）
        virtual const std::string& GetSourcePath() const = 0;
    };

} // namespace VansRuntime
