#pragma once

#include "../../Util/VansColorEncoding.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace VansGraphics
{
    // 输入来自已解码的确定性像素，不依赖 GI 随机样本；只在生产者选中新帧时调用。
    inline float MeasureTextureRadianceChange(const std::vector<uint8_t>& before,
        const std::vector<uint8_t>& after, int width, int height, bool srgb)
    {
        if (width <= 0 || height <= 0 || uint64_t(width) * uint64_t(height) * 4u != after.size())
            throw std::invalid_argument("Texture radiance change requires a complete RGBA8 image");
        if (before.size() != after.size()) return 1.0f;
        if (std::memcmp(before.data(), after.data(), after.size()) == 0) return 0.0f;

        const auto encoding=srgb?Vans::VansColorEncoding::SRGB:Vans::VansColorEncoding::Linear;
        float difference=0.0f, peak=1e-4f;
        for (uint32_t y=0;y<8;++y) for (uint32_t x=0;x<8;++x)
        {
            const auto px=uint64_t(2*x+1)*uint64_t(width)/16u;
            const auto py=uint64_t(2*y+1)*uint64_t(height)/16u;
            const auto offset=size_t((py*uint64_t(width)+px)*4u);
            for (size_t channel=0;channel<3;++channel)
            {
                const float a=Vans::DecodeColor8(before[offset+channel],encoding);
                const float b=Vans::DecodeColor8(after[offset+channel],encoding);
                difference=std::max(difference,std::abs(a-b));
                peak=std::max({peak,a,b});
            }
        }
        // 精确字节比较负责发现变化；固定 8×8 样本只估计响应强度。
        // 未覆盖到的局部像素变化仍启用短历史，不能被采样遗漏。
        return std::clamp(2.0f*difference/peak,.001f,1.0f);
    }
}
