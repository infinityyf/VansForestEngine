#pragma once
#include <string>
#include <functional>

namespace VansRuntime
{
    // 项目层所有 ViewModel 的基类
    // 底层由 VansNoesisDocument::SetDataContext 时注入 Noesis 反射适配体
    // 不直接暴露任何 Noesis 类型
    class VansUIViewModel
    {
    public:
        virtual ~VansUIViewModel() = default;

        // ── 属性通知 ──────────────────────────────────────────────

        // 派生类在属性值更改后调用，触发 XAML {Binding PropertyName} 刷新
        // propertyName 须与 XAML Binding 路径名及 Noesis Reflection 注册名一致
        void NotifyPropertyChanged(const std::string& propertyName);

        // ── Command 绑定 ──────────────────────────────────────────

        // 无参 Command，对应 XAML：Command="{Binding CommandName}"
        void BindCommand(const std::string& commandName,
                         std::function<void()> handler);

        // 带字符串参数 Command，对应 XAML：CommandParameter="{Binding ...}"
        void BindCommandWithParam(const std::string& commandName,
                                  std::function<void(const std::string&)> handler);

    protected:
        // 由 VansNoesisDocument 在 SetDataContext 时注入适配体指针
        // 项目层不应直接访问此成员
        struct Impl;
        Impl* m_Impl = nullptr;

        friend class VansNoesisDocument;
    };

} // namespace VansRuntime
