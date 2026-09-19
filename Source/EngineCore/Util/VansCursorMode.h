#pragma once

namespace Vans
{
enum class VansCursorMode { Visible, Hidden, Captured };

// 平台宿主提供交互范围；输入系统不依赖编辑器或 UI 类型。
enum class VansCursorContext { Inactive, Viewport, Standalone };

inline constexpr const char* VansCursorModeNames[] = { "visible", "hidden", "captured", nullptr };

constexpr VansCursorMode ResolveCursorMode(VansCursorMode requested, VansCursorContext context, bool focused)
{
    if (!focused || context == VansCursorContext::Inactive)
        return VansCursorMode::Visible;
    if (requested == VansCursorMode::Captured && context != VansCursorContext::Standalone)
        return VansCursorMode::Visible;
    return requested;
}
}
