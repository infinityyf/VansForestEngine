#pragma once
#include <string>
#include <functional>

namespace VansRuntime
{
    // 对 XAML 中单个控件的轻量访问句柄（弱引用语义）
    // 通过 VansUIDocument::FindElement 获取
    // 句柄在所属 Document 被销毁后自动失效（IsValid() 返回 false）
    class VansUIElementHandle
    {
    public:
        VansUIElementHandle() = default;

        // 句柄是否仍然有效
        bool IsValid() const;

        // ── 文本 ──────────────────────────────────────────────────

        void        SetText(const std::string& text);
        std::string GetText() const;

        // ── 可见性 ────────────────────────────────────────────────

        void SetVisible(bool visible);
        bool IsVisible() const;

        // ── 事件绑定 ──────────────────────────────────────────────

        // 绑定点击回调（Button / Hyperlink 等可点击控件）
        void BindClick(std::function<void()> callback);

        // ── 属性设置 ──────────────────────────────────────────────

        // 设置任意 XAML DependencyProperty（字符串表示）
        // 例如：SetProperty("Background", "#FF0000")
        void SetProperty(const std::string& property, const std::string& value);

    private:
        // 持有 Noesis::UIElement 的弱引用原始指针，由 VansNoesisDocument 填充
        // 不在此处包含 NsGui 头文件，保持与 Noesis 的解耦
        void* m_NativeElement = nullptr;

        friend class VansNoesisDocument;
    };

} // namespace VansRuntime
