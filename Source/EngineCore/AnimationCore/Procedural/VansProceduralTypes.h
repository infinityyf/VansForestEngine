#pragma once

#include "../VansPoseTypes.h"

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
	enum class VansProceduralSolverStatus
	{
		InvalidInput,
		NoEffect,
		Solved,
		Clamped,
		Unreachable
	};

	enum class VansProceduralLimitReason : std::uint32_t
	{
		None = 0,
		Reach = 1u << 0,
		Joint = 1u << 1,
		AngularSpeed = 1u << 2,
		Query = 1u << 3,
		SupportChanged = 1u << 4
	};

	inline VansProceduralLimitReason operator|(VansProceduralLimitReason left,
	                                          VansProceduralLimitReason right)
	{
		return static_cast<VansProceduralLimitReason>(
			static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
	}

	inline VansProceduralLimitReason& operator|=(VansProceduralLimitReason& left,
	                                             VansProceduralLimitReason right)
	{
		left = left | right;
		return left;
	}

	struct VansProceduralSolverResult
	{
		VansProceduralSolverStatus status = VansProceduralSolverStatus::InvalidInput;
		VansProceduralLimitReason limitReason = VansProceduralLimitReason::None;
		float requestedPositionError = 0.0f;
		float effectivePositionError = 0.0f;
		float rotationErrorDegrees = 0.0f;
		int iterations = 0;
		bool softReachApplied = false;
		bool stretchApplied = false;
	};

	struct VansProceduralGoal
	{
		glm::vec3 positionModel{ 0.0f };
		glm::quat rotationModel{ 1.0f, 0.0f, 0.0f, 0.0f };
		float positionWeight = 1.0f;
		float rotationWeight = 0.0f;
		bool valid = false;
	};

	struct VansContactAttribute
	{
		std::string provider;
		std::string id;
		float phase = 0.0f;
		float confidence = 0.0f;
		bool present = false;
	};

	struct VansSupportHandle
	{
		std::uint64_t id = 0;
		std::uint32_t generation = 0;

		bool IsValid() const { return id != 0 && generation != 0; }
		bool operator==(const VansSupportHandle& other) const
		{
			return id == other.id && generation == other.generation;
		}
		bool operator!=(const VansSupportHandle& other) const { return !(*this == other); }
	};

	// SceneRuntime resolves authored binding names to stable world-space targets
	// before animation evaluation. AnimationCore only consumes this immutable
	// value snapshot and never reaches into Entity/Transform storage.
	struct VansResolvedAnimationTarget
	{
		std::string id;
		glm::vec3 positionWorld{ 0.0f };
		glm::quat rotationWorld{ 1.0f, 0.0f, 0.0f, 0.0f };
		float positionWeight = 1.0f;
		float rotationWeight = 1.0f;
		VansSupportHandle handle;
		bool valid = false;
	};

	struct VansWorldQueryRequest
	{
		std::uint64_t requestId = 0;
		glm::vec3 originWorld{ 0.0f };
		glm::vec3 directionWorld{ 0.0f, -1.0f, 0.0f };
		float distance = 0.0f;
		float sweepRadius = 0.0f;
		std::uint32_t collisionMask = 0;
		std::uint64_t ignoredOwnerId = 0;
	};

	struct VansWorldQueryResult
	{
		std::uint64_t requestId = 0;
		bool hit = false;
		glm::vec3 positionWorld{ 0.0f };
		glm::vec3 normalWorld{ 0.0f, 1.0f, 0.0f };
		float distance = 0.0f;
		std::uint32_t layerIndex = 0;
		VansSupportHandle support;
		glm::vec3 supportPositionWorld{ 0.0f };
		glm::quat supportRotationWorld{ 1.0f, 0.0f, 0.0f, 0.0f };
		bool hasSupportTransform = false;
		// Static actors share the immutable world support domain. Dynamic and
		// kinematic actors retain their individual support identity.
		bool supportMovable = false;
	};

	struct VansAnimationExternalInputSnapshot
	{
		std::uint64_t ownerId = 0;
		std::uint64_t resetToken = 0;
		bool grounded = true;
		bool airborne = false;
		// Unit world-space direction from the character toward its support surface.
		// Callers using non-standard gravity must author this explicitly.
		glm::vec3 approachDirectionWorld{ 0.0f, -1.0f, 0.0f };
		glm::mat4 ownerWorld{ 1.0f };
		std::vector<VansResolvedAnimationTarget> targets;
		std::vector<VansContactAttribute> contacts;
	};
}
