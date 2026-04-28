// =============================================================================
// LTCData.h — Linearly Transformed Cosines (LTC) lookup tables
//
// Source : selfshadow/ltc_code  (https://github.com/selfshadow/ltc_code)
//          fit/results/ltc.js   — arrays `g_ltc_1` and `g_ltc_2`
// License: MIT  (see selfshadow/ltc_code/LICENSE)
//
// Layout : 64 x 64, 4 floats per texel, row-major
//          uv.x = sqrt(1 - NoV)        (per Heitz's parametrisation)
//          uv.y = roughness  in [0, 1]
//          uploaded to GPU as VK_FORMAT_R16G16B16A16_SFLOAT
//
// LTC1   : the inverse LTC matrix coefficients   (m11, m13, m22, m31)
// LTC2   : amplitude, GGX Fresnel/horizon term, sphere-clip threshold, padding
//
// Data definitions live in the generated LTCData.generated.inl, produced by:
//      python Source/EngineCore/RenderCore/LTC/generate_ltc_data.py
// The generator downloads ltc.js once, parses the two arrays, and emits the
// .inl file with two `static constexpr float` arrays of 16384 floats each
// (no binary asset is committed to the repo).
// =============================================================================
#pragma once
#include <cstddef>

namespace VansGraphics
{
namespace LTC
{
    // Texture dimensions for both LUTs.
    constexpr int    kLUTSize   = 64;
    constexpr size_t kLUTFloats = static_cast<size_t>(kLUTSize) * kLUTSize * 4;

    // Both arrays are defined in LTCData.generated.inl (see header for generator script).
    extern const float kLTC1[kLUTFloats];
    extern const float kLTC2[kLUTFloats];

} // namespace LTC
} // namespace VansGraphics
