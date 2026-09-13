#pragma once
#include <string_view>

namespace Vans
{
// JSON 路径模式中的 * 只匹配一个字段或数组下标，不跨越层级。
inline bool MatchSerializedPathPattern(std::string_view pattern, std::string_view path)
{
    while (!pattern.empty() && !path.empty())
    {
        const auto a = pattern.find('/', 1), b = path.find('/', 1);
        const auto lhs = pattern.substr(0, a), rhs = path.substr(0, b);
        if (lhs != "/*" && lhs != rhs) return false;
        pattern = a == std::string_view::npos ? std::string_view{} : pattern.substr(a);
        path = b == std::string_view::npos ? std::string_view{} : path.substr(b);
    }
    return pattern.empty() && path.empty();
}
}
