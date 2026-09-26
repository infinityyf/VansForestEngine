#include "VansClothBindingResolver.h"

#include "VansAnimationTypes.h"
#include "../AssetCore/VansClothProfile.h"

#include <cmath>

namespace VansGraphics
{
	bool VansClothBindingResolver::Resolve(
		const VansEngine::VansClothProfile& profile,
		const Skeleton& skeleton,
		std::vector<VansClothPinSkinData>& bindings,
		std::string& error)
	{
		error.clear();
		bindings.clear();
		bindings.reserve(profile.m_PinnedLocalPositions.size());

		if (profile.m_PinnedBoneBindings.size() != profile.m_PinnedLocalPositions.size())
		{
			error = "Cloth pinned bone bindings must match the pinned vertex count";
			return false;
		}

		for (std::size_t pinIndex = 0; pinIndex < profile.m_PinnedBoneBindings.size(); ++pinIndex)
		{
			const auto& authored = profile.m_PinnedBoneBindings[pinIndex];
			VansClothPinSkinData resolved;

			if (authored.m_BoneNames.size() != authored.m_Weights.size() ||
				authored.m_BoneNames.size() > kMaximumClothBoneInfluences)
			{
				error = "Cloth pin " + std::to_string(pinIndex) +
					" has an invalid bone influence count";
				return false;
			}

			float weightSum = 0.0f;
			for (std::size_t influenceIndex = 0;
				influenceIndex < authored.m_BoneNames.size(); ++influenceIndex)
			{
				const std::string& boneName = authored.m_BoneNames[influenceIndex];
				const float weight = authored.m_Weights[influenceIndex];
				const int boneIndex = skeleton.FindBoneIndex(boneName);
				if (boneIndex < 0)
				{
					error = "Cloth pin " + std::to_string(pinIndex) +
						" references missing bone '" + boneName + "'";
					return false;
				}
				if (!std::isfinite(weight) || weight <= 0.0f)
				{
					error = "Cloth pin " + std::to_string(pinIndex) +
						" contains an invalid bone weight";
					return false;
				}

				auto& destination = resolved.boneWeights[resolved.boneCount++];
				destination.boneIndex = boneIndex;
				destination.weight = weight;
				weightSum += weight;
			}

			if (!authored.m_BoneNames.empty() && std::abs(weightSum - 1.0f) > 1.0e-4f)
			{
				error = "Cloth pin " + std::to_string(pinIndex) +
					" bone weights must sum to 1";
				return false;
			}
			bindings.push_back(resolved);
		}
		return true;
	}
}
