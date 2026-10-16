#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Vans
{
    // 仅描述 RGB 传递函数；Alpha 始终是线性覆盖率，不改变颜色原色。
    enum class VansColorEncoding { Linear, SRGB };

    inline float DecodeColor8(uint8_t value, VansColorEncoding encoding)
    {
        if (encoding == VansColorEncoding::Linear) return float(value) / 255.0f;
        static const auto linear = [] {
            std::array<float, 256> values{};
            for (size_t i = 0; i < values.size(); ++i)
            {
                const float v = float(i) / 255.0f;
                values[i] = v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f);
            }
            return values;
        }();
        return linear[value];
    }

    inline uint8_t EncodeColor8(float linear, VansColorEncoding encoding)
    {
        float value = std::clamp(linear, 0.0f, 1.0f);
        if (encoding == VansColorEncoding::SRGB)
            value = value <= .0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - .055f;
        return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    }
}
