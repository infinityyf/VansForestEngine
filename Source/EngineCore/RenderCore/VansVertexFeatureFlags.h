#pragma once

#include <cstdint>

namespace VansGraphics
{
	enum VansVertexFeatureFlag : std::uint32_t
	{
		VANS_VERTEX_FEATURE_NONE = 0u,
		VANS_VERTEX_FEATURE_SKELETAL_SKINNING = 1u << 0,
		VANS_VERTEX_FEATURE_PREVIOUS_SKINNING = 1u << 1,
		VANS_VERTEX_FEATURE_MORPH_TARGET = 1u << 2,
		VANS_VERTEX_FEATURE_PRE_SKINNED_CACHE = 1u << 3,
	};

	inline bool VansHasVertexFeature(std::uint32_t mask, VansVertexFeatureFlag flag)
	{
		return (mask & static_cast<std::uint32_t>(flag)) != 0u;
	}
}
