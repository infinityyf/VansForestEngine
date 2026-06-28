#include "VansFootPlacementSolver.h"
#include "../IK/VansIKConstraint.h"
#include "../IK/VansIKSolver.h"

#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtc/constants.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace VansGraphics
{
	namespace
	{
		constexpr float kEpsilon = 1e-5f;

		int FindBoneIndex(const Skeleton& skeleton, const std::string& name)
		{
			auto it = skeleton.boneNameToIndex.find(name);
			return it != skeleton.boneNameToIndex.end() ? it->second : -1;
		}

		IKBoneLink MakeFootPlacementLink(int boneIndex, const std::string& boneName, bool isEffector)
		{
			IKBoneLink link;
			link.boneIndex = boneIndex;
			link.boneName = boneName;
			link.constraint.type = JointConstraintType::None;
			link.stiffnessWeight = 1.0f;
			link.isEffector = isEffector;
			return link;
		}

		IKChainDefinition BuildFootPlacementLegChain(const std::string& chainName,
		                                             int hipIndex,
		                                             const std::string& hipName,
		                                             int kneeIndex,
		                                             const std::string& kneeName,
		                                             int footIndex,
		                                             const std::string& footName,
		                                             const FootPlacementSettings& settings)
		{
			IKChainDefinition chain;
			chain.chainName = chainName;
			chain.profileType = IKProfileType::Custom;
			chain.solverType = IKSolverType::FABRIK;
			chain.bones.push_back(MakeFootPlacementLink(hipIndex, hipName, false));
			chain.bones.push_back(MakeFootPlacementLink(kneeIndex, kneeName, false));
			chain.bones.push_back(MakeFootPlacementLink(footIndex, footName, true));
			chain.maxIterations = 1;
			chain.positionTolerance = 0.0005f;
			chain.enableRotationTarget = settings.enableFootRotation;
			chain.rotationWeight = settings.footRotationWeight;
			return chain;
		}

		float SmoothStep01(float x)
		{
			x = glm::clamp(x, 0.0f, 1.0f);
			return x * x * (3.0f - 2.0f * x);
		}

		float ExpAlpha(float speed, float deltaTime)
		{
			if (speed <= 0.0f)
				return 1.0f;
			return glm::clamp(1.0f - std::exp(-speed * std::max(deltaTime, 0.0f)), 0.0f, 1.0f);
		}

		glm::vec3 SafeNormalize(const glm::vec3& value, const glm::vec3& fallback)
		{
			const float len = glm::length(value);
			return len > kEpsilon ? value / len : fallback;
		}

		bool IsValidBoneIndex(int index, size_t count)
		{
			return index >= 0 && index < static_cast<int>(count);
		}

		glm::vec3 BuildOrthogonalDirection(const glm::vec3& direction)
		{
			const glm::vec3 n = SafeNormalize(direction, glm::vec3(0.0f, 1.0f, 0.0f));
			const glm::vec3 axis = std::abs(n.y) < 0.75f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			return SafeNormalize(glm::cross(axis, n), glm::vec3(0.0f, 0.0f, 1.0f));
		}

		glm::vec3 HorizontalDirection(const glm::vec3& value, const glm::vec3& fallback)
		{
			const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
			const glm::vec3 projected = value - worldUp * glm::dot(value, worldUp);
			const glm::vec3 projectedFallback = fallback - worldUp * glm::dot(fallback, worldUp);
			return SafeNormalize(projected, SafeNormalize(projectedFallback, glm::vec3(0.0f, 0.0f, 1.0f)));
		}

		glm::quat SafeRotationBetween(const glm::vec3& from, const glm::vec3& to)
		{
			const float fromLen = glm::length(from);
			const float toLen = glm::length(to);
			if (fromLen <= kEpsilon || toLen <= kEpsilon)
				return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

			const glm::vec3 fromDir = from / fromLen;
			const glm::vec3 toDir = to / toLen;
			const float dotValue = glm::clamp(glm::dot(fromDir, toDir), -1.0f, 1.0f);
			if (dotValue > 1.0f - 1e-5f)
				return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			if (dotValue < -1.0f + 1e-5f)
				return glm::angleAxis(glm::pi<float>(), BuildOrthogonalDirection(fromDir));
			return glm::rotation(fromDir, toDir);
		}

		void ApplyModelSpaceAimRotation(int boneIndex,
		                                const glm::vec3& currentDirection,
		                                const glm::vec3& desiredDirection,
		                                const Skeleton& skeleton,
		                                std::vector<glm::mat4>& localTransforms,
		                                std::vector<glm::mat4>& modelTransforms)
		{
			if (!IsValidBoneIndex(boneIndex, skeleton.bones.size()) ||
			    !IsValidBoneIndex(boneIndex, localTransforms.size()) ||
			    !IsValidBoneIndex(boneIndex, modelTransforms.size()))
				return;

			const glm::quat modelDelta = SafeRotationBetween(currentDirection, desiredDirection);
			if (std::abs(modelDelta.w) > 1.0f - 1e-6f &&
			    std::abs(modelDelta.x) < 1e-6f &&
			    std::abs(modelDelta.y) < 1e-6f &&
			    std::abs(modelDelta.z) < 1e-6f)
				return;

			const BoneInfo& bone = skeleton.bones[boneIndex];
			const glm::quat parentRot =
				IsValidBoneIndex(bone.parentIndex, modelTransforms.size())
					? IK_ExtractRotation(modelTransforms[bone.parentIndex])
					: glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

			const glm::quat localDelta = IK_WorldDeltaToLocal(parentRot, modelDelta);
			const glm::quat currentLocal = IK_ExtractRotation(localTransforms[boneIndex]);
			IK_SetRotation(localTransforms[boneIndex], glm::normalize(localDelta * currentLocal));
			IK_UpdateGlobalsForSubtree(boneIndex, localTransforms, modelTransforms, skeleton);
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

		m_Configured = m_PelvisIndex >= 0 &&
		               m_LeftHipIndex >= 0 && m_LeftKneeIndex >= 0 && m_LeftFootIndex >= 0 &&
		               m_RightHipIndex >= 0 && m_RightKneeIndex >= 0 && m_RightFootIndex >= 0;
		if (!m_Configured)
			return false;

		m_LeftLegChain = BuildFootPlacementLegChain(
			"FootPlacement_LeftLeg",
			m_LeftHipIndex, settings.bones.leftHip,
			m_LeftKneeIndex, settings.bones.leftKnee,
			m_LeftFootIndex, settings.bones.leftFoot,
			settings);
		m_RightLegChain = BuildFootPlacementLegChain(
			"FootPlacement_RightLeg",
			m_RightHipIndex, settings.bones.rightHip,
			m_RightKneeIndex, settings.bones.rightKnee,
			m_RightFootIndex, settings.bones.rightFoot,
			settings);

		m_CurrentWeight = 0.0f;
		m_PelvisOffsetModel = 0.0f;
		m_LeftState = FootPlacementFootState();
		m_RightState = FootPlacementFootState();
		return true;
	}

	void VansFootPlacementSolver::Solve(float deltaTime,
	                                    const Skeleton& skeleton,
	                                    const glm::mat4& ownerWorldTransform,
	                                    std::vector<glm::mat4>& localTransforms)
	{
		if (!m_Configured || localTransforms.size() != skeleton.bones.size())
			return;

		const bool active = m_Settings.enabled && !m_RuntimeState.forceDisabled && !m_RuntimeState.airborne;
		const float wantedWeight = active ? glm::clamp(m_Settings.ikWeight * m_RuntimeState.externalWeight, 0.0f, 1.0f) : 0.0f;
		m_CurrentWeight = glm::mix(m_CurrentWeight, wantedWeight, ExpAlpha(m_Settings.ikWeightSpeed, deltaTime));

		if (m_CurrentWeight <= 0.001f && !active)
		{
			m_DebugData.enabled = false;
			return;
		}

		std::vector<glm::mat4> modelTransforms = BuildModelSpaceTransforms(skeleton, localTransforms);
		if (modelTransforms.empty())
			return;

		const float leftLegLength = LegLength(modelTransforms, m_LeftHipIndex, m_LeftKneeIndex, m_LeftFootIndex);
		const float rightLegLength = LegLength(modelTransforms, m_RightHipIndex, m_RightKneeIndex, m_RightFootIndex);

		m_DebugData = FootPlacementDebugData();
		m_DebugData.enabled = m_Settings.debugVisualization;
		m_DebugData.currentWeight = m_CurrentWeight;

		SurfaceContact leftContact = ProbeFoot(ownerWorldTransform, modelTransforms, m_LeftHipIndex, m_LeftFootIndex, leftLegLength,
		                                       m_DebugData.enabled ? &m_DebugData.left : nullptr);
		SurfaceContact rightContact = ProbeFoot(ownerWorldTransform, modelTransforms, m_RightHipIndex, m_RightFootIndex, rightLegLength,
		                                        m_DebugData.enabled ? &m_DebugData.right : nullptr);

		LegSolve left = BuildLegSolve(deltaTime, ownerWorldTransform, modelTransforms, m_LeftFootIndex, m_LeftState, leftContact);
		LegSolve right = BuildLegSolve(deltaTime, ownerWorldTransform, modelTransforms, m_RightFootIndex, m_RightState, rightContact);

		ApplyPelvisOffset(deltaTime, skeleton, ownerWorldTransform, modelTransforms, left, right, localTransforms);
		modelTransforms = BuildModelSpaceTransforms(skeleton, localTransforms);

		SolveLeg(deltaTime, skeleton, m_LeftLegChain, left.target, m_LeftState, localTransforms, modelTransforms);
		SolveLeg(deltaTime, skeleton, m_RightLegChain, right.target, m_RightState, localTransforms, modelTransforms);

		if (m_DebugData.enabled)
		{
			PopulateLegDebug(m_DebugData.left, ownerWorldTransform, modelTransforms, m_LeftHipIndex, m_LeftKneeIndex, m_LeftFootIndex, left);
			PopulateLegDebug(m_DebugData.right, ownerWorldTransform, modelTransforms, m_RightHipIndex, m_RightKneeIndex, m_RightFootIndex, right);
			m_DebugData.pelvisOffset = m_PelvisOffsetModel;
		}
	}

	SurfaceContact VansFootPlacementSolver::ProbeFoot(const glm::mat4& ownerWorldTransform,
	                                                  const std::vector<glm::mat4>& modelTransforms,
	                                                  int hipIndex,
	                                                  int footIndex,
	                                                  float legLength,
	                                                  FootPlacementDebugLeg* debugLeg) const
	{
		SurfaceContact contact;
		if (hipIndex < 0 || footIndex < 0 ||
		    hipIndex >= static_cast<int>(modelTransforms.size()) ||
		    footIndex >= static_cast<int>(modelTransforms.size()))
			return contact;

		const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
		const glm::vec3 footModel = IK_ExtractTranslation(modelTransforms[footIndex]);
		const glm::vec3 hipModel = IK_ExtractTranslation(modelTransforms[hipIndex]);
		const glm::vec3 footWorld = glm::vec3(ownerWorldTransform * glm::vec4(footModel, 1.0f));
		const glm::vec3 hipWorld = glm::vec3(ownerWorldTransform * glm::vec4(hipModel, 1.0f));

		const glm::mat4 footWorldTransform = ownerWorldTransform * modelTransforms[footIndex];
		const glm::vec3 ownerRight = SafeNormalize(glm::vec3(ownerWorldTransform[0]), glm::vec3(1.0f, 0.0f, 0.0f));
		const glm::vec3 ownerForward = SafeNormalize(glm::vec3(ownerWorldTransform[2]), glm::vec3(0.0f, 0.0f, 1.0f));
		const glm::vec3 footRight = HorizontalDirection(glm::vec3(footWorldTransform[0]), ownerRight);
		const glm::vec3 footForward = HorizontalDirection(glm::vec3(footWorldTransform[2]), ownerForward);
		const float probeRadius = std::max(0.0f, m_Settings.probeFootRadius);
		const float forwardExtent = std::max(probeRadius, m_Settings.probeFootForwardExtent);
		const float backwardExtent = std::max(probeRadius, m_Settings.probeFootBackwardExtent);
		const float sideExtent = std::max(probeRadius, m_Settings.probeFootSideExtent);
		const float longitudinalExtent = std::max(forwardExtent, backwardExtent);
		const float probeDistance = m_Settings.probeHeightAbove + m_Settings.probeDistanceBelow;
		std::vector<glm::vec3> sampleOffsets;
		sampleOffsets.reserve(9);
		sampleOffsets.push_back(glm::vec3(0.0f));
		sampleOffsets.push_back(footRight * sideExtent);
		sampleOffsets.push_back(-footRight * sideExtent);
		sampleOffsets.push_back(footForward * longitudinalExtent);
		sampleOffsets.push_back(-footForward * longitudinalExtent);
		sampleOffsets.push_back(footForward * longitudinalExtent + footRight * sideExtent);
		sampleOffsets.push_back(footForward * longitudinalExtent - footRight * sideExtent);
		sampleOffsets.push_back(-footForward * longitudinalExtent + footRight * sideExtent);
		sampleOffsets.push_back(-footForward * longitudinalExtent - footRight * sideExtent);

		FootOverlapHit overlap;
		bool footOverlapsGround = false;
		const float lengthExtent = std::max(0.05f, longitudinalExtent);
		const float heightExtent = std::max(0.05f, m_Settings.footGroundOffset + m_Settings.ankleHeightOffset);
		const glm::vec3 overlapCenter = footWorld + worldUp * heightExtent;
		const glm::vec3 overlapHalfExtents(std::max(0.05f, sideExtent), heightExtent, lengthExtent);
		overlap = m_GroundProbe.OverlapBox(overlapCenter,
		                                   overlapHalfExtents,
		                                   glm::normalize(IK_ExtractRotation(footWorldTransform)),
		                                   m_Settings.collisionMask);
		footOverlapsGround = overlap.hasHit;
		if (debugLeg)
		{
			debugLeg->overlapCenter = overlapCenter;
			debugLeg->overlapHalfExtents = overlapHalfExtents;
			debugLeg->hasOverlap = overlap.hasHit;
			debugLeg->overlapLayer = overlap.layerIndex;
			debugLeg->overlapActorName = overlap.actorName;
		}

		for (const glm::vec3& sampleOffset : sampleOffsets)
		{
			const glm::vec3 probeOrigin = footWorld + sampleOffset + worldUp * m_Settings.probeHeightAbove;
			FootGroundHit hit = m_GroundProbe.Raycast(probeOrigin, -worldUp, probeDistance, m_Settings.collisionMask);
			FootPlacementDebugSample debugSample;
			debugSample.rayStart = probeOrigin;
			debugSample.rayEnd = probeOrigin - worldUp * probeDistance;
			debugSample.hasHit = hit.hasHit;
			if (hit.hasHit)
			{
				debugSample.hitPosition = hit.position;
				debugSample.hitNormal = SafeNormalize(hit.normal, worldUp);
				debugSample.hitLayer = hit.layerIndex;
				debugSample.hitActorName = hit.actorName;
			}
			if (!hit.hasHit)
			{
				if (debugLeg)
				{
					FootGroundHit rawHit = m_GroundProbe.Raycast(probeOrigin, -worldUp, probeDistance, 0xffffffffu);
					debugSample.hasRawHit = rawHit.hasHit;
					if (rawHit.hasHit)
					{
						debugSample.rawHitPosition = rawHit.position;
						debugSample.rawHitLayer = rawHit.layerIndex;
						debugSample.rawHitActorName = rawHit.actorName;
						debugSample.status = "filtered no hit; raw hit layer " + std::to_string(rawHit.layerIndex);
					}
					else
					{
						debugSample.status = "no physics hit";
					}
				}
				if (debugLeg)
					debugLeg->samples.push_back(debugSample);
				continue;
			}

			const glm::vec3 footToHit = hit.position - footWorld;
			const float verticalDelta = glm::dot(footToHit, worldUp);
			const glm::vec3 supportPosition = footWorld + worldUp * verticalDelta;
			SurfaceContact candidate;
			candidate.hasHit = true;
			candidate.worldPosition = supportPosition;
			candidate.worldNormal = SafeNormalize(hit.normal, worldUp);
			candidate.verticalDelta = verticalDelta;
			candidate.horizontalError = glm::length(sampleOffset);
			candidate.surfaceAngleDeg = glm::degrees(std::acos(glm::clamp(glm::dot(candidate.worldNormal, worldUp), -1.0f, 1.0f)));

			const glm::vec3 targetWorld = candidate.worldPosition + candidate.worldNormal * (m_Settings.footGroundOffset + m_Settings.ankleHeightOffset);
			const float reach = glm::distance(hipWorld, targetWorld);
			if (legLength > kEpsilon && reach > legLength * 1.12f)
			{
				candidate.quality = 0.0f;
				debugSample.status = "rejected: reach";
			}
			else
			{
				candidate.quality = EvaluateContactQuality(m_Settings, candidate);
				if (candidate.surfaceAngleDeg > m_Settings.maxSurfaceAngleDeg)
					debugSample.status = "rejected: slope";
				else if (candidate.verticalDelta > m_Settings.maxVerticalCorrectionUp)
					debugSample.status = "rejected: up limit";
				else if (candidate.verticalDelta < -m_Settings.maxVerticalCorrectionDown)
					debugSample.status = "rejected: down limit";
				else if (candidate.horizontalError > m_Settings.maxHorizontalFootError)
					debugSample.status = "rejected: foot area";
				else if (candidate.quality <= 0.0f)
					debugSample.status = "rejected: quality";
				else
					debugSample.status = "candidate";
			}

			if (candidate.quality <= 0.0f)
			{
				debugSample.quality = candidate.quality;
				if (debugLeg)
					debugLeg->samples.push_back(debugSample);
				continue;
			}
			if (!contact.hasHit ||
			    candidate.worldPosition.y > contact.worldPosition.y + 0.005f ||
			    (std::abs(candidate.worldPosition.y - contact.worldPosition.y) <= 0.005f && candidate.quality > contact.quality))
			{
				contact = candidate;
				contact.hasOverlap = footOverlapsGround;
				debugSample.accepted = true;
			}
			debugSample.quality = candidate.quality;
			if (debugLeg)
				debugLeg->samples.push_back(debugSample);
		}

		if (overlap.hasHit && overlap.hasBounds)
		{
			const float topY = overlap.boundsMax.y;
			const glm::vec3 clampedFootXZ(
				glm::clamp(footWorld.x, overlap.boundsMin.x, overlap.boundsMax.x),
				footWorld.y,
				glm::clamp(footWorld.z, overlap.boundsMin.z, overlap.boundsMax.z));
			const float horizontalError = glm::length(glm::vec2(footWorld.x - clampedFootXZ.x,
			                                                   footWorld.z - clampedFootXZ.z));
			SurfaceContact overlapCandidate;
			overlapCandidate.hasHit = true;
			overlapCandidate.hasOverlap = true;
			overlapCandidate.worldPosition = glm::vec3(footWorld.x, topY, footWorld.z);
			overlapCandidate.worldNormal = worldUp;
			overlapCandidate.verticalDelta = topY - footWorld.y;
			overlapCandidate.horizontalError = horizontalError;
			overlapCandidate.surfaceAngleDeg = 0.0f;

			const glm::vec3 targetWorld = overlapCandidate.worldPosition + worldUp * (m_Settings.footGroundOffset + m_Settings.ankleHeightOffset);
			const float reach = glm::distance(hipWorld, targetWorld);
			const bool withinReach = legLength <= kEpsilon || reach <= legLength * 1.12f;
			const bool withinVerticalRange =
				overlapCandidate.verticalDelta <= m_Settings.maxVerticalCorrectionUp &&
				overlapCandidate.verticalDelta >= -m_Settings.maxVerticalCorrectionDown;
			const bool closeEnough = horizontalError <= m_Settings.maxHorizontalFootError;
			if (withinReach && withinVerticalRange && closeEnough)
				overlapCandidate.quality = EvaluateContactQuality(m_Settings, overlapCandidate);

			if (overlapCandidate.quality > 0.0f &&
			    (!contact.hasHit ||
			     overlapCandidate.worldPosition.y > contact.worldPosition.y + 0.005f ||
			     (std::abs(overlapCandidate.worldPosition.y - contact.worldPosition.y) <= 0.005f &&
			      overlapCandidate.quality > contact.quality)))
			{
				contact = overlapCandidate;
			}
		}

		if (contact.hasHit)
			contact.hasOverlap = footOverlapsGround;
		return contact;
	}

	VansFootPlacementSolver::LegSolve VansFootPlacementSolver::BuildLegSolve(
		float deltaTime,
		const glm::mat4& ownerWorldTransform,
		const std::vector<glm::mat4>& modelTransforms,
		int footIndex,
		FootPlacementFootState& state,
		const SurfaceContact& contact) const
	{
		LegSolve solve;
		solve.contact = contact;
		if (footIndex < 0 || footIndex >= static_cast<int>(modelTransforms.size()))
			return solve;

		const glm::vec3 footModel = IK_ExtractTranslation(modelTransforms[footIndex]);
		const glm::vec3 footWorld = glm::vec3(ownerWorldTransform * glm::vec4(footModel, 1.0f));
		IKTarget target;
		target.position = footModel;
		target.rotation = IK_ExtractRotation(modelTransforms[footIndex]);
		target.positionWeight = 0.0f;
		target.rotationWeight = 0.0f;

		float contactHeightWeight = 0.0f;
		if (contact.hasHit)
		{
			contactHeightWeight = 1.0f;
			if (contact.hasOverlap)
			{
				contactHeightWeight = 1.0f;
			}
			else if (contact.verticalDelta < 0.0f)
			{
				const float footHeightAboveGround = -contact.verticalDelta;
				const float fullHeight = std::max(0.0f, m_Settings.footPlantFullHeight);
				const float fadeHeight = std::max(fullHeight + 0.001f, m_Settings.footPlantFadeHeight);
				contactHeightWeight = 1.0f - SmoothStep01((footHeightAboveGround - fullHeight) / (fadeHeight - fullHeight));
			}
		}
		const float wantedContactWeight = contact.hasHit ? glm::clamp(contact.quality * contactHeightWeight, 0.0f, 1.0f) : 0.0f;
		const float weightAlpha = ExpAlpha(m_Settings.footLockInterpSpeed, deltaTime);
		const float normalAlpha = ExpAlpha(m_Settings.normalInterpSpeed, deltaTime);
		const float groundHeightAlpha = ExpAlpha(m_Settings.groundHeightInterpSpeed, deltaTime);

		if (!state.initialized)
		{
			state.initialized = true;
			state.smoothedWorldPosition = contact.hasHit ? contact.worldPosition : footWorld;
			state.smoothedWorldNormal = contact.hasHit ? contact.worldNormal : glm::vec3(0.0f, 1.0f, 0.0f);
			state.smoothedGroundHeight = glm::dot(state.smoothedWorldPosition, glm::vec3(0.0f, 1.0f, 0.0f));
			state.groundHeightInitialized = contact.hasHit;
		}

		if (contact.hasHit)
		{
			const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
			const float contactGroundHeight = glm::dot(contact.worldPosition, worldUp);
			if (!state.groundHeightInitialized)
			{
				state.groundHeightInitialized = true;
				state.smoothedGroundHeight = contactGroundHeight;
			}
			else
			{
				state.smoothedGroundHeight = glm::mix(state.smoothedGroundHeight, contactGroundHeight, groundHeightAlpha);
			}
			const float footHeight = glm::dot(footWorld, worldUp);
			state.smoothedWorldPosition = footWorld + worldUp * (state.smoothedGroundHeight - footHeight);
			state.smoothedWorldNormal = SafeNormalize(glm::mix(state.smoothedWorldNormal, contact.worldNormal, normalAlpha), glm::vec3(0.0f, 1.0f, 0.0f));
		}
		else
		{
			state.smoothedWorldPosition = footWorld;
			state.groundHeightInitialized = false;
		}
		state.smoothedWeight = contact.hasOverlap
			? std::max(state.smoothedWeight, wantedContactWeight)
			: glm::mix(state.smoothedWeight, wantedContactWeight, weightAlpha);

		if (contact.hasHit && state.smoothedWeight >= m_Settings.minContactQuality)
		{
			const glm::mat4 inverseOwner = glm::inverse(ownerWorldTransform);
			const glm::vec3 targetWorld = state.smoothedWorldPosition + state.smoothedWorldNormal * (m_Settings.footGroundOffset + m_Settings.ankleHeightOffset);
			target.position = glm::vec3(inverseOwner * glm::vec4(targetWorld, 1.0f));
			target.positionWeight = glm::clamp(m_CurrentWeight * state.smoothedWeight, 0.0f, 1.0f);
			if (m_Settings.enableFootRotation)
			{
				target.rotation = BuildFootRotation(ownerWorldTransform, modelTransforms[footIndex], state.smoothedWorldNormal);
				target.rotationWeight = target.positionWeight * glm::clamp(m_Settings.footRotationWeight, 0.0f, 1.0f);
			}
			solve.valid = target.positionWeight > 0.001f;
		}

		solve.target = target;
		return solve;
	}

	void VansFootPlacementSolver::ApplyPelvisOffset(float deltaTime,
	                                                const Skeleton& skeleton,
	                                                const glm::mat4& ownerWorldTransform,
	                                                const std::vector<glm::mat4>& modelTransforms,
	                                                const LegSolve& left,
	                                                const LegSolve& right,
	                                                std::vector<glm::mat4>& localTransforms)
	{
		if (m_PelvisIndex < 0 || m_PelvisIndex >= static_cast<int>(localTransforms.size()) ||
		    m_LeftFootIndex < 0 || m_RightFootIndex < 0)
			return;

		const glm::mat4 inverseOwner = glm::inverse(ownerWorldTransform);
		const glm::vec3 modelUp = SafeNormalize(glm::mat3(inverseOwner) * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		const float modelUnitsPerWorldMeter = glm::length(glm::mat3(inverseOwner) * glm::vec3(0.0f, 1.0f, 0.0f));
		const float maxDown = std::max(0.0f, m_Settings.pelvisMaxDown * modelUnitsPerWorldMeter);
		const float maxUp = std::max(0.0f, m_Settings.pelvisMaxUp * modelUnitsPerWorldMeter);

		bool hasOffset = false;
		float wantedOffset = 0.0f;
		if (left.valid && m_LeftFootIndex < static_cast<int>(modelTransforms.size()))
		{
			const float delta = glm::dot(left.target.position - IK_ExtractTranslation(modelTransforms[m_LeftFootIndex]), modelUp);
			wantedOffset = delta;
			hasOffset = true;
		}
		if (right.valid && m_RightFootIndex < static_cast<int>(modelTransforms.size()))
		{
			const float delta = glm::dot(right.target.position - IK_ExtractTranslation(modelTransforms[m_RightFootIndex]), modelUp);
			wantedOffset = hasOffset ? std::min(wantedOffset, delta) : delta;
			hasOffset = true;
		}

		if (!hasOffset)
			wantedOffset = 0.0f;

		wantedOffset = glm::clamp(wantedOffset, -maxDown, maxUp);
		m_PelvisOffsetModel = glm::mix(m_PelvisOffsetModel, wantedOffset * m_CurrentWeight, ExpAlpha(m_Settings.pelvisInterpSpeed, deltaTime));

		const BoneInfo& pelvis = skeleton.bones[m_PelvisIndex];
		glm::vec3 parentModelUp = modelUp;
		if (pelvis.parentIndex >= 0 && pelvis.parentIndex < static_cast<int>(modelTransforms.size()))
		{
			const glm::quat parentRot = IK_ExtractRotation(modelTransforms[pelvis.parentIndex]);
			parentModelUp = SafeNormalize(glm::inverse(parentRot) * modelUp, glm::vec3(0.0f, 1.0f, 0.0f));
		}
		localTransforms[m_PelvisIndex][3] += glm::vec4(parentModelUp * m_PelvisOffsetModel, 0.0f);
	}

	void VansFootPlacementSolver::SolveLeg(float deltaTime,
	                                       const Skeleton& skeleton,
	                                       const IKChainDefinition& chain,
	                                       const IKTarget& target,
	                                       FootPlacementFootState& state,
	                                       std::vector<glm::mat4>& localTransforms,
	                                       std::vector<glm::mat4>& modelTransforms)
	{
		if (target.positionWeight <= 0.001f)
			return;
		if (chain.bones.size() < 3)
			return;

		const int hipIndex = chain.bones[0].boneIndex;
		const int kneeIndex = chain.bones[1].boneIndex;
		const int footIndex = chain.bones[2].boneIndex;
		if (!IsValidBoneIndex(hipIndex, skeleton.bones.size()) ||
		    !IsValidBoneIndex(kneeIndex, skeleton.bones.size()) ||
		    !IsValidBoneIndex(footIndex, skeleton.bones.size()) ||
		    !IsValidBoneIndex(hipIndex, modelTransforms.size()) ||
		    !IsValidBoneIndex(kneeIndex, modelTransforms.size()) ||
		    !IsValidBoneIndex(footIndex, modelTransforms.size()))
			return;

		const glm::vec3 hip = IK_ExtractTranslation(modelTransforms[hipIndex]);
		const glm::vec3 knee = IK_ExtractTranslation(modelTransforms[kneeIndex]);
		const glm::vec3 foot = IK_ExtractTranslation(modelTransforms[footIndex]);
		const float upperLen = glm::distance(hip, knee);
		const float lowerLen = glm::distance(knee, foot);
		if (upperLen <= kEpsilon || lowerLen <= kEpsilon)
			return;

		const float weight = glm::clamp(target.positionWeight, 0.0f, 1.0f);
		glm::vec3 desiredFoot = glm::mix(foot, target.position, weight);
		glm::vec3 hipToTarget = desiredFoot - hip;
		const glm::vec3 currentHipToFoot = foot - hip;
		const glm::vec3 reachFallback = SafeNormalize(currentHipToFoot, BuildOrthogonalDirection(knee - hip));
		glm::vec3 targetDir = SafeNormalize(hipToTarget, reachFallback);

		const float maxReach = std::max(upperLen + lowerLen - 1e-4f, kEpsilon);
		const float minReach = std::max(std::abs(upperLen - lowerLen) + 1e-4f, kEpsilon);
		float targetDistance = glm::length(hipToTarget);
		targetDistance = glm::clamp(targetDistance, minReach, maxReach);
		desiredFoot = hip + targetDir * targetDistance;

		glm::vec3 pole = knee - hip - targetDir * glm::dot(knee - hip, targetDir);
		if (glm::length(pole) <= kEpsilon)
		{
			const glm::vec3 currentDir = SafeNormalize(currentHipToFoot, targetDir);
			pole = knee - hip - currentDir * glm::dot(knee - hip, currentDir);
		}
		glm::vec3 poleDir = SafeNormalize(pole, BuildOrthogonalDirection(targetDir));
		if (!state.poleInitialized)
		{
			state.poleInitialized = true;
			state.smoothedPoleModelDir = poleDir;
		}
		else
		{
			state.smoothedPoleModelDir = SafeNormalize(
				glm::mix(state.smoothedPoleModelDir, poleDir, ExpAlpha(m_Settings.poleInterpSpeed, deltaTime)),
				poleDir);
			poleDir = SafeNormalize(
				state.smoothedPoleModelDir - targetDir * glm::dot(state.smoothedPoleModelDir, targetDir),
				poleDir);
		}

		const float distanceSq = targetDistance * targetDistance;
		const float kneeAlongTarget = glm::clamp(
			(upperLen * upperLen + distanceSq - lowerLen * lowerLen) / (2.0f * std::max(targetDistance, kEpsilon)),
			0.0f,
			upperLen);
		const float kneeSide = std::sqrt(std::max(upperLen * upperLen - kneeAlongTarget * kneeAlongTarget, 0.0f));
		const glm::vec3 desiredKnee = hip + targetDir * kneeAlongTarget + poleDir * kneeSide;

		ApplyModelSpaceAimRotation(hipIndex, knee - hip, desiredKnee - hip, skeleton, localTransforms, modelTransforms);

		const glm::vec3 solvedKnee = IK_ExtractTranslation(modelTransforms[kneeIndex]);
		const glm::vec3 solvedFoot = IK_ExtractTranslation(modelTransforms[footIndex]);
		ApplyModelSpaceAimRotation(kneeIndex, solvedFoot - solvedKnee, desiredFoot - solvedKnee, skeleton, localTransforms, modelTransforms);

		if (target.rotationWeight > 0.001f)
			IK_ApplyEffectorRotationTarget(localTransforms, modelTransforms, skeleton, footIndex, target);
	}

	std::vector<glm::mat4> VansFootPlacementSolver::BuildModelSpaceTransforms(
		const Skeleton& skeleton,
		const std::vector<glm::mat4>& localTransforms)
	{
		if (localTransforms.size() != skeleton.bones.size())
			return {};

		std::vector<glm::mat4> modelTransforms = localTransforms;
		if (!skeleton.topologicalOrder.empty())
		{
			for (int idx : skeleton.topologicalOrder)
			{
				if (idx < 0 || idx >= static_cast<int>(skeleton.bones.size()))
					continue;
				const int parent = skeleton.bones[idx].parentIndex;
				if (parent >= 0 && parent < static_cast<int>(skeleton.bones.size()) && parent != idx)
					modelTransforms[idx] = modelTransforms[parent] * localTransforms[idx];
				else
					modelTransforms[idx] = localTransforms[idx];
			}
			return modelTransforms;
		}

		for (int i = 0; i < static_cast<int>(skeleton.bones.size()); ++i)
		{
			const int parent = skeleton.bones[i].parentIndex;
			if (parent >= 0 && parent < i)
				modelTransforms[i] = modelTransforms[parent] * localTransforms[i];
			else
				modelTransforms[i] = localTransforms[i];
		}
		return modelTransforms;
	}

	float VansFootPlacementSolver::EvaluateContactQuality(const FootPlacementSettings& settings, const SurfaceContact& contact)
	{
		if (!contact.hasHit)
			return 0.0f;
		if (contact.surfaceAngleDeg > settings.maxSurfaceAngleDeg)
			return 0.0f;
		if (contact.verticalDelta > settings.maxVerticalCorrectionUp ||
		    contact.verticalDelta < -settings.maxVerticalCorrectionDown)
			return 0.0f;
		if (contact.horizontalError > settings.maxHorizontalFootError)
			return 0.0f;

		const float slopeQuality = 1.0f - SmoothStep01(contact.surfaceAngleDeg / std::max(settings.maxSurfaceAngleDeg, 0.001f));
		const float upDenom = std::max(settings.maxVerticalCorrectionUp, 0.001f);
		const float downDenom = std::max(settings.maxVerticalCorrectionDown, 0.001f);
		const float verticalLimit = contact.verticalDelta >= 0.0f ? upDenom : downDenom;
		const float verticalQuality = 1.0f - SmoothStep01(std::abs(contact.verticalDelta) / verticalLimit);
		const float horizontalQuality = 1.0f - SmoothStep01(contact.horizontalError / std::max(settings.maxHorizontalFootError, 0.001f));
		return glm::clamp(slopeQuality * 0.35f + verticalQuality * 0.45f + horizontalQuality * 0.20f, 0.0f, 1.0f);
	}

	float VansFootPlacementSolver::LegLength(const std::vector<glm::mat4>& modelTransforms,
	                                         int hipIndex,
	                                         int kneeIndex,
	                                         int footIndex)
	{
		if (hipIndex < 0 || kneeIndex < 0 || footIndex < 0 ||
		    hipIndex >= static_cast<int>(modelTransforms.size()) ||
		    kneeIndex >= static_cast<int>(modelTransforms.size()) ||
		    footIndex >= static_cast<int>(modelTransforms.size()))
			return 0.0f;

		const glm::vec3 hip = IK_ExtractTranslation(modelTransforms[hipIndex]);
		const glm::vec3 knee = IK_ExtractTranslation(modelTransforms[kneeIndex]);
		const glm::vec3 foot = IK_ExtractTranslation(modelTransforms[footIndex]);
		return glm::distance(hip, knee) + glm::distance(knee, foot);
	}

	void VansFootPlacementSolver::PopulateLegDebug(FootPlacementDebugLeg& debugLeg,
	                                               const glm::mat4& ownerWorldTransform,
	                                               const std::vector<glm::mat4>& modelTransforms,
	                                               int hipIndex,
	                                               int kneeIndex,
	                                               int footIndex,
	                                               const LegSolve& solve)
	{
		auto worldPoint = [&](int boneIndex) -> glm::vec3
		{
			if (boneIndex < 0 || boneIndex >= static_cast<int>(modelTransforms.size()))
				return glm::vec3(0.0f);
			return glm::vec3(ownerWorldTransform * glm::vec4(IK_ExtractTranslation(modelTransforms[boneIndex]), 1.0f));
		};

		debugLeg.hip = worldPoint(hipIndex);
		debugLeg.knee = worldPoint(kneeIndex);
		debugLeg.foot = worldPoint(footIndex);
		debugLeg.hasContact = solve.contact.hasHit;
		debugLeg.contact = solve.contact.worldPosition;
		debugLeg.normal = solve.contact.worldNormal;
		debugLeg.hasTarget = solve.valid;
		debugLeg.targetWeight = solve.target.positionWeight;
		debugLeg.target = glm::vec3(ownerWorldTransform * glm::vec4(solve.target.position, 1.0f));
	}

	glm::quat VansFootPlacementSolver::BuildFootRotation(const glm::mat4& ownerWorldTransform,
	                                                     const glm::mat4& currentFootModel,
	                                                     const glm::vec3& worldNormal)
	{
		const glm::mat4 inverseOwner = glm::inverse(ownerWorldTransform);
		const glm::vec3 modelNormal = SafeNormalize(glm::mat3(inverseOwner) * worldNormal, glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::quat currentRot = IK_ExtractRotation(currentFootModel);
		const glm::vec3 currentUp = SafeNormalize(currentRot * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::quat align = glm::rotation(currentUp, modelNormal);
		return glm::normalize(align * currentRot);
	}
}
