#include "VansWorldTextureWindow.h"
#include <cmath>
#include <climits>
namespace Vans
{
bool ResolveWorldTextureWindow(const VansWorldTextureWindow& window,const glm::vec3& camera,
    glm::vec4& rectangle,std::array<std::int64_t,2>& texelOrigin)
{
    for (int i=0;i<4;++i) if (!std::isfinite(window.worldRect[i])) return false;
    if (window.worldRect.z<=0 || window.worldRect.w<=0 || window.resolution.x==0 || window.resolution.y==0) return false;
    auto result=window.worldRect;std::array<std::int64_t,2> origin{};
    if (window.cameraCentered) for (int axis=0;axis<2;++axis)
    {
        const double texel=double(result[axis+2])/window.resolution[axis];
        const double value=std::floor(double(camera[axis==0?0:2])/texel-double(window.resolution[axis])*0.5);
        if (!std::isfinite(value) || std::abs(value)>double(INT32_MAX)) return false;
        origin[axis]=static_cast<std::int64_t>(value);result[axis]=static_cast<float>(value*texel);
    }
    rectangle=result;texelOrigin=origin;return true;
}
}
