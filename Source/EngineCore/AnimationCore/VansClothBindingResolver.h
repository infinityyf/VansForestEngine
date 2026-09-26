#pragma once

#include <GLM/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace VansEngine
{
	struct VansClothProfile;
}

namespace VansGraphics
{
	struct Skeleton;

	inline constexpr std::uint32_t kMaximumClothBoneInfluences = 4;

	struct VansClothPinSkinData
	{
		struct BoneWeight
		{
			int boneIndex = -1;
			float weight = 0.0f;
			glm::vec3 boneLocalOffset{ 0.0f };
		};

		BoneWeight boneWeights[kMaximumClothBoneInfluences];
		std::uint32_t boneCount = 0;
	};

	// AnimationCore owns the translation from authored bone names to Skeleton indices.
	// AssetCore remains a data-only dependency and PhysicsCore consumes the resolved value data.
	class VansClothBindingResolver final
	{
	public:
		static bool Resolve(
			const VansEngine::VansClothProfile& profile,
			const Skeleton& skeleton,
			std::vector<VansClothPinSkinData>& bindings,
			std::string& error);
	};
}
