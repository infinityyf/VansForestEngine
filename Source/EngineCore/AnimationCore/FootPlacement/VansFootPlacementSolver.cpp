#include "VansFootPlacementSolver.h"
#include "../IK/VansIKChainBuilder.h"
#include "../IK/VansIKConstraint.h"
#include "../IK/VansTwoBoneIKSolver.h"

#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace VansGraphics
{
	namespace
	{
		constexpr float kEpsilon = 1e-5f;
		const glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);

		int FindBoneIndex(const Skeleton& skeleton, const std::string& name)
		{
			const auto it = skeleton.boneNameToIndex.find(name);
			return it != skeleton.boneNameToIndex.end() ? it->second : -1;
		}

		bool IsValidBone(int index, size_t count)
		{
			return index >= 0 && index < static_cast<int>(count);
		}

		glm::vec3 SafeNormalize(const glm::vec3& value, const glm::vec3& fallback)
		{
			const float length = glm::length(value);
			return length > kEpsilon ? value / length : fallback;
		}

		glm::vec3 HorizontalDirection(const glm::vec3& direction, const glm::vec3& fallback)
		{
			const glm::vec3 projected = direction - kWorldUp * glm::dot(direction, kWorldUp);
			const glm::vec3 fallbackProjected = fallback - kWorldUp * glm::dot(fallback, kWorldUp);
			return SafeNormalize(projected, SafeNormalize(fallbackProjected, glm::vec3(0.0f, 0.0f, 1.0f)));
		}

		float DecayAlpha(float smoothTime, float deltaTime)
		{
			if (smoothTime <= kEpsilon)
				return 1.0f;
			return glm::clamp(1.0f - std::exp(-std::max(deltaTime, 0.0f) / smoothTime), 0.0f, 1.0f);
		}

		float SmoothDamp(float current,
		                 float target,
		                 float& velocity,
		                 float smoothTime,
		                 float deltaTime)
		{
			const float dt = std::max(deltaTime, 0.0f);
			if (smoothTime <= kEpsilon || dt <= 0.0f)
			{
				velocity = 0.0f;
				return target;
			}
			const float omega = 2.0f / smoothTime;
			const float x = omega * dt;
			const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
			const float change = current - target;
			const float temporary = (velocity + omega * change) * dt;
			velocity = (velocity - omega * temporary) * decay;
			return target + (change + temporary) * decay;
		}

		glm::vec3 BuildOrthogonal(const glm::vec3& direction)
		{
			const glm::vec3 n = SafeNormalize(direction, glm::vec3(0.0f, 0.0f, 1.0f));
			const glm::vec3 axis = std::abs(n.y) < 0.75f
				? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			return SafeNormalize(glm::cross(axis, n), glm::vec3(1.0f, 0.0f, 0.0f));
		}

		glm::quat BuildFootRotation(const glm::mat4& ownerWorldTransform,
		                            const glm::mat4& currentFootModel,
		                            const glm::vec3& worldNormal,
		                            const glm::vec3& footLocalUp)
		{
			const glm::vec3 modelNormal = SafeNormalize(
				glm::transpose(glm::mat3(ownerWorldTransform)) * worldNormal,
				glm::vec3(0.0f, 1.0f, 0.0f));
			const glm::quat currentRotation = IK_ExtractRotation(currentFootModel);
			const glm::vec3 currentUp = SafeNormalize(currentRotation * footLocalUp, glm::vec3(0.0f, 1.0f, 0.0f));
			return glm::normalize(glm::rotation(currentUp, modelNormal) * currentRotation);
		}
	}

	bool VansFootPlacementSolver::Configure(const FootPlacementSettings& settings, const Skeleton& skeleton)
	{
		m_Settings = settings;
		m_PelvisIndex = FindBoneIndex(skeleton, settings.bones.pelvis);
		m_LeftHipIndex = FindBoneIndex(skeleton, settings.bones.leftHip);
		m_LeftKneeIndex = FindBoneIndex(skeleton, settings.bones.leftKnee);
		m_LeftFootIndex = FindBoneIndex(skeleton, settings.bones.leftFoot);
		m_RightHipIndex = FindBoneIndex(skeleton, settings.bones.rightHip);
		m_RightKneeIndex = FindBoneIndex(skeleton, settings.bones.rightKnee);
		m_RightFootIndex = FindBoneIndex(skeleton, settings.bones.rightFoot);
		m_Configured = IsValidBone(m_PelvisIndex, skeleton.bones.size()) &&
			IsValidBone(m_LeftHipIndex, skeleton.bones.size()) &&
			IsValidBone(m_LeftKneeIndex, skeleton.bones.size()) &&
			IsValidBone(m_LeftFootIndex, skeleton.bones.size()) &&
			IsValidBone(m_RightHipIndex, skeleton.bones.size()) &&
			IsValidBone(m_RightKneeIndex, skeleton.bones.size()) &&
			IsValidBone(m_RightFootIndex, skeleton.bones.size());
		if (!m_Configured)
			return false;

		m_LeftFootLocalUp = SafeNormalize(
			glm::conjugate(IK_ExtractRotation(glm::inverse(skeleton.bones[m_LeftFootIndex].offsetMatrix))) * kWorldUp,
			kWorldUp);
		m_RightFootLocalUp = SafeNormalize(
			glm::conjugate(IK_ExtractRotation(glm::inverse(skeleton.bones[m_RightFootIndex].offsetMatrix))) * kWorldUp,
			kWorldUp);

		m_LeftLegChain = VansIKChainBuilder::BuildHumanoidLeg(
			skeleton, settings.bones.leftHip, settings.bones.leftKnee, settings.bones.leftFoot, false);
		m_RightLegChain = VansIKChainBuilder::BuildHumanoidLeg(
			skeleton, settings.bones.rightHip, settings.bones.rightKnee, settings.bones.rightFoot, true);
		for (IKChainDefinition* chain : { &m_LeftLegChain, &m_RightLegChain })
		{
			chain->solverType = IKSolverType::TwoBone;
			chain->chainName = chain == &m_LeftLegChain ? "FootPlacement_Left" : "FootPlacement_Right";
			chain->enableRotationTarget = settings.rotationWeight > 0.0f;
			chain->rotationWeight = settings.rotationWeight;
			if (chain->bones.size() >= 2)
			{
				chain->bones[0].constraint.type = JointConstraintType::None;
				chain->bones[1].constraint.type = JointConstraintType::None;
			}
		}
		ResetTransientState();
		return true;
	}

	void VansFootPlacementSolver::ResetTransientState()
	{
		m_LeftState = FootPlacementFootState();
		m_RightState = FootPlacementFootState();
		m_GlobalWeight = 0.0f;
		m_GlobalWeightVelocity = 0.0f;
		m_PelvisOffsetWorld = 0.0f;
		m_PelvisVelocity = 0.0f;
		m_DebugData = FootPlacementDebugData();
	}

	void VansFootPlacementSolver::Solve(float deltaTime,
	                                    const Skeleton& skeleton,
	                                    const glm::mat4& ownerWorldTransform,
	                                    std::vector<glm::mat4>& localTransforms)
	{
		if (!m_Configured || localTransforms.size() != skeleton.bones.size())
			return;

		const bool active = m_Settings.enabled && !m_RuntimeState.forceDisabled && !m_RuntimeState.airborne;
		const float wantedGlobalWeight = active
			? glm::clamp(m_Settings.ikWeight * m_RuntimeState.externalWeight, 0.0f, 1.0f) : 0.0f;
		m_GlobalWeight = glm::clamp(SmoothDamp(m_GlobalWeight,
			wantedGlobalWeight,
			m_GlobalWeightVelocity,
			m_Settings.globalWeightSmoothTime,
			deltaTime), 0.0f, 1.0f);

		std::vector<glm::mat4> modelTransforms = IK_BuildModelSpaceTransforms(skeleton, localTransforms);
		if (modelTransforms.empty())
			return;

		m_DebugData = FootPlacementDebugData();
		m_DebugData.enabled = m_Settings.debugVisualization;
		m_DebugData.currentWeight = m_GlobalWeight;

		FootPlacementContact leftContact;
		FootPlacementContact rightContact;
		if (active)
		{
			leftContact = ProbeFoot(ownerWorldTransform, modelTransforms, m_LeftFootIndex,
				m_DebugData.enabled ? &m_DebugData.left : nullptr);
			rightContact = ProbeFoot(ownerWorldTransform, modelTransforms, m_RightFootIndex,
				m_DebugData.enabled ? &m_DebugData.right : nullptr);
		}

		LegTarget left = UpdateLegTarget(deltaTime, ownerWorldTransform, modelTransforms,
			m_LeftFootIndex, m_LeftFootLocalUp, m_LeftState, leftContact,
			m_RuntimeState.leftPlantWeight);
		LegTarget right = UpdateLegTarget(deltaTime, ownerWorldTransform, modelTransforms,
			m_RightFootIndex, m_RightFootLocalUp, m_RightState, rightContact,
			m_RuntimeState.rightPlantWeight);

		ApplyPelvisOffset(deltaTime, skeleton, ownerWorldTransform, left, right, localTransforms);
		modelTransforms = IK_BuildModelSpaceTransforms(skeleton, localTransforms);
		SolveLeg(deltaTime, skeleton, m_LeftLegChain, left, m_LeftState, localTransforms, modelTransforms);
		SolveLeg(deltaTime, skeleton, m_RightLegChain, right, m_RightState, localTransforms, modelTransforms);

		if (m_DebugData.enabled)
		{
			PopulateLegDebug(m_DebugData.left, ownerWorldTransform, modelTransforms,
				m_LeftHipIndex, m_LeftKneeIndex, m_LeftFootIndex, left,
				m_LeftState, m_RuntimeState.leftPlantWeight);
			PopulateLegDebug(m_DebugData.right, ownerWorldTransform, modelTransforms,
				m_RightHipIndex, m_RightKneeIndex, m_RightFootIndex, right,
				m_RightState, m_RuntimeState.rightPlantWeight);
			m_DebugData.pelvisOffset = m_PelvisOffsetWorld;
		}
	}

	FootPlacementContact VansFootPlacementSolver::ProbeFoot(
		const glm::mat4& ownerWorldTransform,
		const std::vector<glm::mat4>& modelTransforms,
		int footIndex,
		FootPlacementDebugLeg* debugLeg) const
	{
		FootPlacementContact contact;
		if (!IsValidBone(footIndex, modelTransforms.size()))
			return contact;

		const glm::mat4 footWorldTransform = ownerWorldTransform * modelTransforms[footIndex];
		const glm::vec3 footWorld = IK_ExtractTranslation(footWorldTransform);
		const glm::vec3 ownerForward = HorizontalDirection(glm::vec3(ownerWorldTransform[2]), glm::vec3(0.0f, 0.0f, 1.0f));
		const glm::vec3 ownerRight = HorizontalDirection(glm::vec3(ownerWorldTransform[0]), glm::vec3(1.0f, 0.0f, 0.0f));
		const glm::vec3 footForward = HorizontalDirection(glm::vec3(footWorldTransform[2]), ownerForward);
		const glm::vec3 footRight = HorizontalDirection(glm::vec3(footWorldTransform[0]), ownerRight);
		const float halfLength = std::max(0.0f, m_Settings.footHalfLength);
		const float halfWidth = std::max(0.0f, m_Settings.footHalfWidth);
		const std::vector<glm::vec3> offsets = {
			glm::vec3(0.0f), footForward * halfLength, -footForward * halfLength,
			footRight * halfWidth, -footRight * halfWidth
		};

		std::vector<FootGroundRayRequest> requests;
		requests.reserve(offsets.size());
		for (const glm::vec3& offset : offsets)
		{
			requests.push_back(FootGroundRayRequest{
				footWorld + offset + kWorldUp * std::max(0.0f, m_Settings.probeOriginHeight),
				-kWorldUp,
				std::max(0.0f, m_Settings.probeLength)
			});
		}
		const std::vector<FootGroundHit> hits = m_GroundProbe.RaycastBatch(requests, m_Settings.collisionMask);

		struct AcceptedHit
		{
			const FootGroundHit* hit = nullptr;
			glm::vec3 centerGroundPoint = glm::vec3(0.0f);
			float verticalOffset = 0.0f;
			float weight = 1.0f;
		};
		std::vector<AcceptedHit> accepted;
		accepted.reserve(hits.size());
		const glm::vec3 horizontalCenter = footWorld - kWorldUp * glm::dot(footWorld, kWorldUp);
		const float maxSlopeCos = std::cos(glm::radians(glm::clamp(m_Settings.maxSlopeDeg, 0.0f, 89.0f)));

		for (size_t index = 0; index < hits.size(); ++index)
		{
			const FootGroundHit& hit = hits[index];
			FootPlacementDebugSample sample;
			sample.rayStart = requests[index].origin;
			sample.rayEnd = requests[index].origin - kWorldUp * requests[index].distance;
			sample.hasHit = hit.hasHit;
			if (!hit.hasHit)
			{
				sample.status = "no hit";
				if (debugLeg) debugLeg->samples.push_back(sample);
				continue;
			}

			sample.hitPosition = hit.position;
			sample.hitNormal = SafeNormalize(hit.normal, kWorldUp);
			sample.hitLayer = hit.layerIndex;
			sample.hitActorName = hit.actorName;
			const float upDot = glm::dot(sample.hitNormal, kWorldUp);
			if (upDot < maxSlopeCos || upDot <= kEpsilon)
			{
				sample.status = "slope rejected";
				if (debugLeg) debugLeg->samples.push_back(sample);
				continue;
			}

			const float centerHeight = glm::dot(sample.hitNormal, hit.position - horizontalCenter) / upDot;
			const glm::vec3 centerGroundPoint = horizontalCenter + kWorldUp * centerHeight;
			const glm::vec3 target = FootPlacementBuildTarget(footWorld, centerGroundPoint, m_Settings.ankleHeight);
			const float verticalOffset = glm::dot(target - footWorld, kWorldUp);
			if (verticalOffset > std::max(0.0f, m_Settings.maxStepUp) ||
			    verticalOffset < -std::max(0.0f, m_Settings.maxStepDown))
			{
				sample.status = "step limit rejected";
				if (debugLeg) debugLeg->samples.push_back(sample);
				continue;
			}

			sample.accepted = true;
			sample.status = "accepted";
			accepted.push_back(AcceptedHit{ &hit, centerGroundPoint, verticalOffset, index == 0 ? 2.0f : 1.0f });
			if (debugLeg) debugLeg->samples.push_back(sample);
		}

		if (accepted.empty())
			return contact;

		std::unordered_map<uintptr_t, float> actorScores;
		for (const AcceptedHit& item : accepted)
			actorScores[item.hit->actorId] += item.weight;
		uintptr_t dominantActor = accepted.front().hit->actorId;
		float dominantScore = -1.0f;
		for (const auto& [actor, score] : actorScores)
		{
			if (score > dominantScore)
			{
				dominantActor = actor;
				dominantScore = score;
			}
		}

		glm::vec3 pointSum(0.0f);
		glm::vec3 normalSum(0.0f);
		float weightSum = 0.0f;
		const FootGroundHit* representative = nullptr;
		for (const AcceptedHit& item : accepted)
		{
			if (item.hit->actorId != dominantActor)
				continue;
			pointSum += item.centerGroundPoint * item.weight;
			normalSum += SafeNormalize(item.hit->normal, kWorldUp) * item.weight;
			weightSum += item.weight;
			if (!representative || item.weight > 1.0f)
				representative = item.hit;
		}
		if (weightSum <= kEpsilon || !representative)
			return contact;

		contact.valid = true;
		contact.groundPointWorld = pointSum / weightSum;
		contact.groundNormalWorld = SafeNormalize(normalSum, kWorldUp);
		const glm::vec3 target = FootPlacementBuildTarget(
			footWorld, contact.groundPointWorld, m_Settings.ankleHeight);
		contact.verticalOffset = glm::dot(target - footWorld, kWorldUp);
		contact.soleClearance = -contact.verticalOffset;
		contact.slopeDeg = glm::degrees(std::acos(glm::clamp(glm::dot(contact.groundNormalWorld, kWorldUp), -1.0f, 1.0f)));
		contact.layer = representative->layerIndex;
		contact.actorId = representative->actorId;
		contact.actorName = representative->actorName;
		contact.actorWorldTransform = representative->actorWorldTransform;
		contact.hasActorWorldTransform = representative->hasActorWorldTransform;
		return contact;
	}

	VansFootPlacementSolver::LegTarget VansFootPlacementSolver::UpdateLegTarget(
		float deltaTime,
		const glm::mat4& ownerWorldTransform,
		const std::vector<glm::mat4>& modelTransforms,
		int footIndex,
		const glm::vec3& footLocalUp,
		FootPlacementFootState& state,
		const FootPlacementContact& contact,
		float animationPlantWeight) const
	{
		LegTarget result;
		result.contact = contact;
		if (!IsValidBone(footIndex, modelTransforms.size()))
			return result;

		result.animatedFootWorld = glm::vec3(ownerWorldTransform *
			glm::vec4(IK_ExtractTranslation(modelTransforms[footIndex]), 1.0f));
		const float wantedOffset = contact.valid ? contact.verticalOffset : 0.0f;
		const float terrainWeight = contact.valid
			? FootPlacementClearanceWeight(contact.soleClearance,
				m_Settings.fullContactHeight, m_Settings.contactFadeHeight) : 0.0f;
		const glm::vec3 wantedNormal = contact.valid ? contact.groundNormalWorld : kWorldUp;
		const glm::vec3 poseRelativeTarget = result.animatedFootWorld +
			kWorldUp * wantedOffset;
		const float plantWeight = glm::clamp(animationPlantWeight, 0.0f, 1.0f);
		const bool useFootLock = m_Settings.footLockEnabled &&
			m_RuntimeState.hasAnimationPlantWeights;
		const float enterWeight = glm::clamp(
			m_Settings.footLockEnterPlantWeight, 0.0f, 1.0f);
		const float exitWeight = glm::clamp(
			m_Settings.footLockExitPlantWeight, 0.0f, enterWeight);
		if (!useFootLock)
			state.planted = false;
		else if (!state.planted && contact.valid && plantWeight >= enterWeight)
		{
			state.planted = true;
			state.lockedWorldPosition = poseRelativeTarget;
			state.lockedActorId = contact.actorId;
			state.hasLockedActorLocalPosition = contact.hasActorWorldTransform;
			if (state.hasLockedActorLocalPosition)
			{
				state.lockedActorLocalPosition = glm::vec3(
					glm::inverse(contact.actorWorldTransform) *
					glm::vec4(state.lockedWorldPosition, 1.0f));
			}
		}
		if (state.planted)
		{
			if (contact.valid && contact.actorId == state.lockedActorId &&
				state.hasLockedActorLocalPosition && contact.hasActorWorldTransform)
			{
				state.lockedWorldPosition = glm::vec3(
					contact.actorWorldTransform *
					glm::vec4(state.lockedActorLocalPosition, 1.0f));
			}
			const glm::vec3 horizontalDelta = (poseRelativeTarget - state.lockedWorldPosition) -
				kWorldUp * glm::dot(poseRelativeTarget - state.lockedWorldPosition, kWorldUp);
			const bool lockOutOfReach = glm::length(horizontalDelta) >
				(std::max)(0.0f, m_Settings.footLockMaxDistance);
			if (!contact.valid || contact.actorId != state.lockedActorId ||
				plantWeight <= exitWeight || lockOutOfReach)
			{
				state.planted = false;
			}
			else
			{
				if (!state.hasLockedActorLocalPosition || !contact.hasActorWorldTransform)
				{
					// 静态地面保持种脚瞬间的水平坐标，垂直位置跟随当前接触面。
					state.lockedWorldPosition += kWorldUp * glm::dot(
						poseRelativeTarget - state.lockedWorldPosition, kWorldUp);
				}
			}
		}
		const float wantedLockWeight = state.planted ? plantWeight : 0.0f;
		state.lockWeight = glm::clamp(SmoothDamp(
			state.lockWeight,
			wantedLockWeight,
			state.lockWeightVelocity,
			m_Settings.footLockSmoothTime,
			deltaTime), 0.0f, 1.0f);
		const float wantedWeight = (std::max)(terrainWeight, state.lockWeight);

		if (!state.initialized)
		{
			state.initialized = true;
			state.verticalOffset = wantedOffset;
			state.groundNormalWorld = wantedNormal;
		}
		state.verticalOffset = SmoothDamp(state.verticalOffset,
			wantedOffset, state.verticalVelocity, m_Settings.offsetSmoothTime, deltaTime);
		state.weight = glm::clamp(SmoothDamp(state.weight,
			wantedWeight, state.weightVelocity, m_Settings.weightSmoothTime, deltaTime), 0.0f, 1.0f);
		state.groundNormalWorld = SafeNormalize(glm::mix(state.groundNormalWorld,
			wantedNormal, DecayAlpha(m_Settings.normalSmoothTime, deltaTime)), wantedNormal);

		const glm::vec3 smoothedPoseRelativeTarget = result.animatedFootWorld +
			kWorldUp * state.verticalOffset;
		result.targetWorld = glm::mix(
			smoothedPoseRelativeTarget, state.lockedWorldPosition, state.lockWeight);
		result.ikTarget.position = glm::vec3(glm::inverse(ownerWorldTransform) * glm::vec4(result.targetWorld, 1.0f));
		result.ikTarget.rotation = BuildFootRotation(ownerWorldTransform,
			modelTransforms[footIndex], state.groundNormalWorld, footLocalUp);
		result.ikTarget.positionWeight = glm::clamp(m_GlobalWeight * state.weight, 0.0f, 1.0f);
		result.ikTarget.rotationWeight = result.ikTarget.positionWeight *
			glm::clamp(m_Settings.rotationWeight, 0.0f, 1.0f);
		result.valid = result.ikTarget.positionWeight > 0.001f;
		return result;
	}

	void VansFootPlacementSolver::ApplyPelvisOffset(
		float deltaTime,
		const Skeleton& skeleton,
		const glm::mat4& ownerWorldTransform,
		const LegTarget& left,
		const LegTarget& right,
		std::vector<glm::mat4>& localTransforms)
	{
		float wantedOffset = 0.0f;
		if (left.valid)
			wantedOffset = std::min(wantedOffset,
				glm::dot(left.targetWorld - left.animatedFootWorld, kWorldUp) * left.ikTarget.positionWeight);
		if (right.valid)
			wantedOffset = std::min(wantedOffset,
				glm::dot(right.targetWorld - right.animatedFootWorld, kWorldUp) * right.ikTarget.positionWeight);
		wantedOffset = glm::clamp(wantedOffset, -std::max(0.0f, m_Settings.pelvisMaxDrop), 0.0f);
		m_PelvisOffsetWorld = SmoothDamp(m_PelvisOffsetWorld,
			wantedOffset, m_PelvisVelocity, m_Settings.pelvisSmoothTime, deltaTime);
		if (!IsValidBone(m_PelvisIndex, localTransforms.size()) || std::abs(m_PelvisOffsetWorld) <= kEpsilon)
			return;

		const glm::vec3 modelOffset = glm::vec3(glm::inverse(ownerWorldTransform) *
			glm::vec4(kWorldUp * m_PelvisOffsetWorld, 0.0f));
		glm::vec3 localOffset = modelOffset;
		const int parent = skeleton.bones[m_PelvisIndex].parentIndex;
		if (IsValidBone(parent, localTransforms.size()))
		{
			const std::vector<glm::mat4> modelTransforms = IK_BuildModelSpaceTransforms(skeleton, localTransforms);
			localOffset = glm::vec3(glm::inverse(modelTransforms[parent]) * glm::vec4(modelOffset, 0.0f));
		}
		localTransforms[m_PelvisIndex][3] += glm::vec4(localOffset, 0.0f);
	}

	void VansFootPlacementSolver::SolveLeg(float deltaTime,
	                                       const Skeleton& skeleton,
	                                       const IKChainDefinition& chain,
	                                       const LegTarget& legTarget,
	                                       FootPlacementFootState& state,
	                                       std::vector<glm::mat4>& localTransforms,
	                                       std::vector<glm::mat4>& modelTransforms)
	{
		if (!legTarget.valid || chain.bones.size() < 3)
		{
			state.poleInitialized = false;
			return;
		}
		const int hipIndex = chain.bones[0].boneIndex;
		const int kneeIndex = chain.bones[1].boneIndex;
		const int footIndex = chain.bones[2].boneIndex;
		if (!IsValidBone(hipIndex, modelTransforms.size()) ||
		    !IsValidBone(kneeIndex, modelTransforms.size()) ||
		    !IsValidBone(footIndex, modelTransforms.size()))
			return;

		const glm::vec3 hip = IK_ExtractTranslation(modelTransforms[hipIndex]);
		const glm::vec3 knee = IK_ExtractTranslation(modelTransforms[kneeIndex]);
		const glm::vec3 foot = IK_ExtractTranslation(modelTransforms[footIndex]);
		const float upperLength = glm::distance(hip, knee);
		const float lowerLength = glm::distance(knee, foot);
		if (upperLength <= kEpsilon || lowerLength <= kEpsilon)
			return;

		IKTarget target = legTarget.ikTarget;
		glm::vec3 targetDelta = target.position - hip;
		float targetDistance = glm::length(targetDelta);
		const float maxReach = (upperLength + lowerLength) *
			glm::clamp(m_Settings.maxLegExtensionRatio, 0.80f, 1.0f);
		if (targetDistance > maxReach && targetDistance > kEpsilon)
		{
			target.position = hip + targetDelta * (maxReach / targetDistance);
			targetDelta = target.position - hip;
			targetDistance = maxReach;
		}

		const glm::vec3 targetDirection = SafeNormalize(targetDelta, SafeNormalize(foot - hip, glm::vec3(0.0f, 0.0f, 1.0f)));
		const glm::vec3 bend = knee - hip;
		glm::vec3 pole = bend - targetDirection * glm::dot(bend, targetDirection);
		pole = SafeNormalize(pole, BuildOrthogonal(targetDirection));
		const float authoredPoleWeight = glm::clamp(m_Settings.kneePoleModelWeight, 0.0f, 1.0f);
		if (authoredPoleWeight > 0.0f)
		{
			glm::vec3 authored = m_Settings.kneePoleModelDir -
				targetDirection * glm::dot(m_Settings.kneePoleModelDir, targetDirection);
			authored = SafeNormalize(authored, pole);
			if (glm::dot(authored, pole) < 0.0f)
				authored = -authored;
			pole = SafeNormalize(glm::mix(pole, authored, authoredPoleWeight), pole);
		}
		if (!state.poleInitialized)
		{
			state.poleInitialized = true;
			state.poleModelDir = pole;
		}
		else
		{
			state.poleModelDir = SafeNormalize(glm::mix(state.poleModelDir, pole,
				DecayAlpha(m_Settings.poleSmoothTime, deltaTime)), pole);
		}

		IKChainDefinition solveChain = chain;
		solveChain.poleVector = hip + state.poleModelDir * std::max(upperLength + lowerLength, 1.0f);
		solveChain.poleWeight = 1.0f;
		solveChain.poleSpace = IKCoordinateSpace::Model;
		const glm::mat4 originalHip = localTransforms[hipIndex];
		const glm::mat4 originalKnee = localTransforms[kneeIndex];
		const glm::mat4 originalFoot = localTransforms[footIndex];

		VansTwoBoneIKSolver solver;
		IKSolveContext context;
		context.deltaTime = deltaTime;
		const IKSolveResult solveResult = solver.Solve(
			localTransforms, modelTransforms, skeleton, solveChain, target, context);
		modelTransforms = IK_BuildModelSpaceTransforms(skeleton, localTransforms);
		const glm::vec3 solvedFoot = IK_ExtractTranslation(modelTransforms[footIndex]);
		const bool finite = std::isfinite(solvedFoot.x) && std::isfinite(solvedFoot.y) && std::isfinite(solvedFoot.z);
		if (solveResult.status == IKSolveStatus::InvalidInput ||
		    !FootPlacementSolveResultIsUsable(solveResult.finalPosError, upperLength + lowerLength, finite))
		{
			localTransforms[hipIndex] = originalHip;
			localTransforms[kneeIndex] = originalKnee;
			localTransforms[footIndex] = originalFoot;
			modelTransforms = IK_BuildModelSpaceTransforms(skeleton, localTransforms);
			state.poleInitialized = false;
		}
	}

	void VansFootPlacementSolver::PopulateLegDebug(
		FootPlacementDebugLeg& debugLeg,
		const glm::mat4& ownerWorldTransform,
		const std::vector<glm::mat4>& modelTransforms,
		int hipIndex,
		int kneeIndex,
		int footIndex,
		const LegTarget& target,
		const FootPlacementFootState& state,
		float animationPlantWeight)
	{
		auto worldPosition = [&](int index)
		{
			return IsValidBone(index, modelTransforms.size())
				? glm::vec3(ownerWorldTransform * glm::vec4(IK_ExtractTranslation(modelTransforms[index]), 1.0f))
				: glm::vec3(0.0f);
		};
		debugLeg.hip = worldPosition(hipIndex);
		debugLeg.knee = worldPosition(kneeIndex);
		debugLeg.animatedFoot = target.animatedFootWorld;
		debugLeg.solvedFoot = worldPosition(footIndex);
		debugLeg.target = target.targetWorld;
		debugLeg.contact = target.contact.groundPointWorld;
		debugLeg.normal = target.contact.groundNormalWorld;
		debugLeg.hasContact = target.contact.valid;
		debugLeg.hasTarget = target.valid;
		debugLeg.targetWeight = target.ikTarget.positionWeight;
		debugLeg.verticalOffset = glm::dot(target.targetWorld - target.animatedFootWorld, kWorldUp);
		debugLeg.planted = state.planted || state.lockWeight > 0.001f;
		debugLeg.plantWeight = glm::clamp(animationPlantWeight, 0.0f, 1.0f);
		const glm::vec3 lockDelta = target.animatedFootWorld - state.lockedWorldPosition;
		debugLeg.horizontalLockError = glm::length(
			lockDelta - kWorldUp * glm::dot(lockDelta, kWorldUp));
	}
}
