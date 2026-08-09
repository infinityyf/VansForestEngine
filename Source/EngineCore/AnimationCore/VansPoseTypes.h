#pragma once

#include "VansAnimationTypes.h"
#include "VansAnimationFrameMemory.h"
#include "FootPlacement/VansFootPlacementTypes.h"

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VansGraphics
{
	// 动画求值主表示。矩阵只允许出现在导入、后处理适配、层级传播和蒙皮边界。
	struct VansBoneTransform
	{
		glm::vec3 translation{ 0.0f };
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 scale{ 1.0f };
	};

	struct VansRootMotionDelta
	{
		glm::vec3 translation{ 0.0f };
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 scale{ 1.0f };
		bool valid = false;
		std::uint64_t sourceClipId = 0;
		std::uint64_t sourceNodeId = 0;
		std::uint64_t sourceLayerId = 0;
	};

	struct VansAnimationCurveSample
	{
		std::uint64_t id = 0;
		std::string_view name;
		float value = 0.0f;
		bool present = false;
	};

	using VansAnimationEventValue = AnimationEventValue;

	struct VansAnimationEventSample
	{
		std::uint64_t id = 0;
		std::uint64_t clipId = 0;
		std::uint64_t sourceNodeId = 0;
		std::uint64_t sourceLayerId = 0;
		std::string_view name;
		float sourceTime = 0.0f;
		std::int64_t loopIndex = 0;
		float weight = 1.0f;
		VansAnimationEventValue payload;
	};

	struct VansAnimationSyncState
	{
		std::uint64_t groupId = 0;
		std::uint64_t markerId = 0;
		std::uint64_t nextMarkerId = 0;
		float normalizedTime = 0.0f;
		float phase = 0.0f;
		bool valid = false;
	};

	struct VansFootPlacementRequest
	{
		int sourceNodeId = -1;
		// Definition-owned settings stay immutable for the lifetime of the graph
		// instance. The per-frame pose only transports this non-owning reference.
		const FootPlacementSettings* settings = nullptr;
		bool valid = false;
	};

	// 拥有型 Payload 先作为统一正确性边界；Animator Compiler 随后为节点分配
	// 预分配 scratch buffer，并以 View 传递相同的数据语义。
	struct VansPosePayload
	{
		VansAnimationFrameVector<VansBoneTransform> localPose;
		VansRootMotionDelta rootMotion;
		VansAnimationFrameVector<VansAnimationCurveSample> curves;
		VansAnimationFrameVector<VansAnimationEventSample> events;
		VansAnimationFrameVector<SampledNodeTransform> nodeTransforms;
		VansAnimationSyncState sync;
		VansFootPlacementRequest footPlacement;
		float sourceWeight = 1.0f;
		bool sourceAdditive = false;
		VansAnimationFrameVector<float> sourceBoneMask;
		bool valid = false;
	};
}
