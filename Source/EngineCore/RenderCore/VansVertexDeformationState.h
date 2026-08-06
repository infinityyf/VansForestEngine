#pragma once

#include "VansVertexFeatureFlags.h"

#include <cstdint>

namespace VansGraphics
{
	class VansAnimationNode;
	class VansVKBuffer;

	struct VansVertexDeformationState
	{
		std::uint32_t featureMask = VANS_VERTEX_FEATURE_NONE;
		VansAnimationNode* skinningOwner = nullptr;
		std::uint32_t submeshIndex = 0;
		VansVKBuffer* boneIDBuffer = nullptr;
		VansVKBuffer* boneWeightBuffer = nullptr;

		bool HasValidSkeletalSkinningResources() const
		{
			return skinningOwner != nullptr &&
				boneIDBuffer != nullptr &&
				boneWeightBuffer != nullptr;
		}

		std::uint32_t BuildFeatureMask() const
		{
			std::uint32_t mask = featureMask;
			if (HasValidSkeletalSkinningResources())
				mask |= VANS_VERTEX_FEATURE_SKELETAL_SKINNING;
			return mask;
		}
	};
}
