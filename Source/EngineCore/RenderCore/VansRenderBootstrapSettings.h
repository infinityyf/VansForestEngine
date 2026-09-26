#pragma once

#include <array>
#include <cstdint>

namespace VansGraphics
{

// Process-wide immutable defaults required before project render settings are
// available.  CascadeCount is part of the current CPU/shader ABI and must stay
// synchronized with CASCADE_COUNT in EngineAssets/Shaders/Common/Common.glsl.
struct VansRenderBootstrapSettings
{
	bool requestRayTracing = true;
	std::uint32_t cascadeShadowMapSize = 2048;
	std::uint32_t cascadeCount = 4;
	std::array<float, 4> cascadeSplits = {10.0f, 35.0f, 120.0f, 350.0f};
	std::uint32_t punctualShadowAtlasWidth = 4096;
	std::uint32_t punctualShadowAtlasHeight = 4096;
};

inline constexpr VansRenderBootstrapSettings kVansRenderBootstrapSettings{};
static_assert(kVansRenderBootstrapSettings.cascadeCount ==
			  kVansRenderBootstrapSettings.cascadeSplits.size());

} // namespace VansGraphics
