#include "VansGroundingRuntime.h"

#include "../../VansPoseMath.h"
#include "../Solvers/VansConstraintMath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace VansGraphics
{
	namespace
	{
		constexpr float kEpsilon = 1.0e-6f;
		constexpr float kLn2 = 0.69314718055994530942f;

		bool Finite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool Finite(const glm::quat& value)
		{
			return std::isfinite(value.w) && std::isfinite(value.x)
				&& std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool ValidSupportHandle(const VansSupportHandle& handle)
		{
			return (handle.id == 0 && handle.generation == 0)
				|| (handle.id != 0 && handle.generation != 0);
		}

		float HalfLifeAlpha(float deltaTime, float halfLife)
		{
			if (deltaTime <= 0.0f) return 0.0f;
			if (halfLife <= kEpsilon) return 1.0f;
			return 1.0f - std::exp(-kLn2 * deltaTime / halfLife);
		}

		float ClearanceWeight(float clearance, float fullHeight, float fadeHeight)
		{
			const float t = std::clamp(
				(clearance - fullHeight) / (fadeHeight - fullHeight), 0.0f, 1.0f);
			const float smooth = t * t * (3.0f - 2.0f * t);
			return 1.0f - smooth;
		}

		glm::vec3 TransformPoint(const VansBoneTransform& transform, const glm::vec3& point)
		{
			return transform.translation + transform.rotation * (transform.scale * point);
		}

		glm::vec3 InverseTransformPoint(const VansBoneTransform& transform, const glm::vec3& point)
		{
			glm::vec3 local = glm::inverse(transform.rotation) * (point - transform.translation);
			for (int axis = 0; axis < 3; ++axis)
				local[axis] = std::abs(transform.scale[axis]) > kEpsilon
					? local[axis] / transform.scale[axis] : 0.0f;
			return local;
		}

		glm::vec3 InverseTransformVector(const VansBoneTransform& transform, const glm::vec3& vector)
		{
			glm::vec3 local = glm::inverse(transform.rotation) * vector;
			for (int axis = 0; axis < 3; ++axis)
				local[axis] = std::abs(transform.scale[axis]) > kEpsilon
					? local[axis] / transform.scale[axis] : 0.0f;
			return local;
		}

		glm::vec3 TransformNormalToModel(
			const VansBoneTransform& transform, const glm::vec3& normalWorld)
		{
			// For A = R*S, a model-space normal is proportional to A^T*n_world.
			return transform.scale * (glm::inverse(transform.rotation) * normalWorld);
		}

		glm::vec3 ProjectOnPlane(const glm::vec3& value, const glm::vec3& normal)
		{
			return value - normal * glm::dot(value, normal);
		}

		glm::vec3 TransformVector(const VansBoneTransform& transform, const glm::vec3& vector)
		{
			return transform.rotation * (transform.scale * vector);
		}

		glm::quat AlignFootRotation(const glm::quat& current,
		                            const glm::vec3& localNormal,
		                            const glm::vec3& localForward,
		                            const glm::vec3& targetNormal)
		{
			const glm::vec3 currentNormal = current * glm::normalize(localNormal);
			const glm::vec3 currentForward = current * glm::normalize(localForward);
			const glm::quat normalDelta = VansShortestArc(currentNormal, targetNormal);
			const glm::vec3 rotatedForward = normalDelta * currentForward;
			glm::vec3 wantedForward = ProjectOnPlane(currentForward, targetNormal);
			glm::vec3 flatRotated = ProjectOnPlane(rotatedForward, targetNormal);
			if (glm::length(wantedForward) <= kEpsilon || glm::length(flatRotated) <= kEpsilon)
				return glm::normalize(normalDelta * current);
			wantedForward = glm::normalize(wantedForward);
			flatRotated = glm::normalize(flatRotated);
			const float signedAngle = std::atan2(
				glm::dot(glm::cross(flatRotated, wantedForward), targetNormal),
				std::clamp(glm::dot(flatRotated, wantedForward), -1.0f, 1.0f));
			return glm::normalize(glm::angleAxis(signedAngle, targetNormal) * normalDelta * current);
		}

		struct PlaneFit
		{
			bool valid = false;
			glm::vec3 point{ 0.0f };
			glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
			float residual = 0.0f;
		};

		PlaneFit FitSupportPlane(const std::vector<const VansWorldQueryResult*>& hits,
		                         const glm::vec3& up,
		                         float maxNormalDeviationDegrees)
		{
			PlaneFit fit;
			if (hits.size() < 3) return fit;
			fit.point = glm::vec3(0.0f);
			glm::vec3 normalSum(0.0f);
			for (const VansWorldQueryResult* hit : hits)
			{
				fit.point += hit->positionWorld;
				glm::vec3 normal = glm::normalize(hit->normalWorld);
				if (glm::dot(normal, up) < 0.0f) normal = -normal;
				normalSum += normal;
			}
			fit.point /= static_cast<float>(hits.size());
			if (glm::dot(normalSum, normalSum) <= kEpsilon * kEpsilon) return fit;
			fit.normal = glm::normalize(normalSum);
			const float normalAgreement = std::cos(glm::radians(maxNormalDeviationDegrees));
			for (const VansWorldQueryResult* hit : hits)
			{
				glm::vec3 normal = glm::normalize(hit->normalWorld);
				if (glm::dot(normal, up) < 0.0f) normal = -normal;
				if (glm::dot(normal, fit.normal) < normalAgreement) return fit;
				fit.residual = std::max(fit.residual,
					std::abs(glm::dot(hit->positionWorld - fit.point, fit.normal)));
			}
			fit.valid = true;
			return fit;
		}

		glm::vec3 SupportPointToWorld(const VansWorldQueryResult& hit, const glm::vec3& local)
		{
			return hit.supportPositionWorld + hit.supportRotationWorld * local;
		}
	}

	bool VansGroundingRuntime::Configure(
		const VansCompiledAnimationRig& rig,
		const VansCompiledGroundingSettings& settings,
		std::string& error)
	{
		error.clear();
		if (settings.contactIndices.empty() || settings.query.collisionMask == 0)
		{
			error = "Grounding runtime received an uncompiled configuration";
			return false;
		}
		for (int contact : settings.contactIndices)
		{
			if (contact < 0 || contact >= static_cast<int>(rig.contacts.size()))
			{
				error = "Grounding runtime contact index is out of range";
				return false;
			}
		}
		m_Rig = &rig;
		m_Settings = settings;
		m_ContactStates.assign(settings.contactIndices.size(), ContactState{});
		m_TransactionContactStates.assign(settings.contactIndices.size(), ContactState{});
		m_PreparedContacts.resize(settings.contactIndices.size());
		std::size_t maxSamples = 0;
		for (std::size_t runtimeIndex = 0; runtimeIndex < settings.contactIndices.size(); ++runtimeIndex)
		{
			PreparedContact& prepared = m_PreparedContacts[runtimeIndex];
			const VansCompiledRigContact& contact =
				rig.contacts[static_cast<std::size_t>(settings.contactIndices[runtimeIndex])];
			prepared.samplePositionsWorld.reserve(contact.soleSamplesLocal.size());
			prepared.requestIds.reserve(contact.soleSamplesLocal.size());
			maxSamples = std::max(maxSamples, contact.soleSamplesLocal.size());
		}
		m_AcceptedHitsScratch.reserve(maxSamples);
		m_SupportHitsScratch.reserve(maxSamples);
		Reset();
		return true;
	}

	void VansGroundingRuntime::Reset(std::uint64_t resetToken)
	{
		m_ResetToken = resetToken;
		m_QuerySequence = 1;
		m_PelvisOffsetModel = glm::vec3(0.0f);
		m_HasPreparedContacts = false;
		m_HasResolvedTransaction = false;
		for (PreparedContact& prepared : m_PreparedContacts)
		{
			prepared.samplePositionsWorld.clear();
			prepared.requestIds.clear();
		}
		for (ContactState& state : m_ContactStates) state = {};
	}

	bool VansGroundingRuntime::Prepare(
		VansPoseWorkspace& workspace,
		const VansAnimationExternalInputSnapshot& input,
		std::vector<VansWorldQueryRequest>& outRequests)
	{
		outRequests.clear();
		m_HasPreparedContacts = false;
		if (!m_Rig || m_HasResolvedTransaction
			|| workspace.GetSkeleton() != m_Rig->skeleton
			|| (input.grounded && input.airborne))
			return false;
		if (input.resetToken != m_ResetToken)
			Reset(input.resetToken);
		if (!VansPoseMath::TryDecompose(input.ownerWorld, m_OwnerTransform)
			|| m_OwnerTransform.scale.x <= kEpsilon
			|| m_OwnerTransform.scale.y <= kEpsilon
			|| m_OwnerTransform.scale.z <= kEpsilon)
			return false;
		if (!Finite(input.approachDirectionWorld)
			|| glm::dot(input.approachDirectionWorld, input.approachDirectionWorld) <= kEpsilon * kEpsilon)
			return false;
		for (std::size_t index = 0; index < input.contacts.size(); ++index)
		{
			const VansContactAttribute& attribute = input.contacts[index];
			if (attribute.provider.empty() || attribute.id.empty()
				|| !std::isfinite(attribute.phase) || attribute.phase < 0.0f || attribute.phase > 1.0f
				|| !std::isfinite(attribute.confidence)
				|| attribute.confidence < 0.0f || attribute.confidence > 1.0f)
				return false;
			for (std::size_t other = 0; other < index; ++other)
				if (input.contacts[other].provider == attribute.provider
					&& input.contacts[other].id == attribute.id)
					return false;
		}
		const glm::vec3 approachDirectionWorld = glm::normalize(input.approachDirectionWorld);
		for (std::size_t runtimeIndex = 0; runtimeIndex < m_Settings.contactIndices.size(); ++runtimeIndex)
		{
			const int contactIndex = m_Settings.contactIndices[runtimeIndex];
			const VansCompiledRigContact& contact = m_Rig->contacts[static_cast<std::size_t>(contactIndex)];
			PreparedContact& prepared = m_PreparedContacts[runtimeIndex];
			prepared.samplePositionsWorld.clear();
			prepared.requestIds.clear();
			prepared.rigContactIndex = contactIndex;
			prepared.animatedFootModel = workspace.GetComponentPosition(contact.footBoneIndex);
			prepared.animatedFootRotationModel = workspace.GetComponentRotation(contact.footBoneIndex);
			prepared.animatedFootScaleModel = workspace.GetComponentScale(contact.footBoneIndex);
			if (input.airborne || !input.grounded)
				continue;
			for (std::size_t sampleIndex = 0; sampleIndex < contact.soleSamplesLocal.size(); ++sampleIndex)
			{
				const glm::vec3 sampleModel = prepared.animatedFootModel
					+ prepared.animatedFootRotationModel
					* (prepared.animatedFootScaleModel * contact.soleSamplesLocal[sampleIndex].positionLocal);
				const glm::vec3 sampleWorld = TransformPoint(m_OwnerTransform, sampleModel);
				const std::uint64_t localRequestId = (m_QuerySequence++ << 16)
					| (static_cast<std::uint64_t>(runtimeIndex & 0xffu) << 8)
					| static_cast<std::uint64_t>(sampleIndex & 0xffu);
				const std::uint64_t requestId = (input.ownerId * 0x9e3779b97f4a7c15ull)
					^ localRequestId;
				prepared.samplePositionsWorld.push_back(sampleWorld);
				prepared.requestIds.push_back(requestId);
				outRequests.push_back({ requestId,
					sampleWorld - approachDirectionWorld * m_Settings.query.startDistanceAgainstApproach,
					approachDirectionWorld,
					m_Settings.query.startDistanceAgainstApproach
						+ m_Settings.query.endDistanceAlongApproach,
					contact.sweepRadius,
					m_Settings.query.collisionMask,
					input.ownerId });
			}
		}
		m_HasPreparedContacts = true;
		return true;
	}

	bool VansGroundingRuntime::Resolve(
		float deltaTime,
		VansPoseWorkspace& workspace,
		const VansAnimationExternalInputSnapshot& input,
		const std::vector<VansWorldQueryResult>& results,
		std::vector<VansProceduralGoal>& outGoals,
		VansProceduralSolverResult& outResult)
	{
		outResult = {};
		if (!m_Rig || m_HasResolvedTransaction || !m_HasPreparedContacts
			|| m_PreparedContacts.size() != m_Settings.contactIndices.size()
			|| input.resetToken != m_ResetToken || deltaTime < 0.0f || !std::isfinite(deltaTime))
			return false;
		m_TransactionContactStates = m_ContactStates;
		m_TransactionPelvisOffsetModel = m_PelvisOffsetModel;
		m_HasResolvedTransaction = true;
		if (outGoals.size() != m_Rig->goals.size())
			outGoals.assign(m_Rig->goals.size(), VansProceduralGoal{});
		for (int contactIndex : m_Settings.contactIndices)
		{
			const int goalIndex = m_Rig->FindGoal(
				m_Rig->contacts[static_cast<std::size_t>(contactIndex)].id);
			if (goalIndex >= 0) outGoals[static_cast<std::size_t>(goalIndex)] = {};
		}
		if (!Finite(input.approachDirectionWorld)
			|| glm::dot(input.approachDirectionWorld, input.approachDirectionWorld) <= kEpsilon * kEpsilon)
			return false;
		const glm::vec3 worldUp = -glm::normalize(input.approachDirectionWorld);
		glm::vec3 modelUp = InverseTransformVector(m_OwnerTransform, worldUp);
		if (!Finite(modelUp) || glm::dot(modelUp, modelUp) <= kEpsilon * kEpsilon)
			return false;
		modelUp = glm::normalize(modelUp);
		const float slopeCos = std::cos(glm::radians(m_Settings.query.maxSlopeDegrees));
		bool anyGoal = false;
		bool anyQueryRejected = false;
		bool anySupportChanged = false;
		for (std::size_t runtimeIndex = 0; runtimeIndex < m_PreparedContacts.size(); ++runtimeIndex)
		{
			const PreparedContact& prepared = m_PreparedContacts[runtimeIndex];
			const VansCompiledRigContact& contact =
				m_Rig->contacts[static_cast<std::size_t>(prepared.rigContactIndex)];
			ContactState& state = m_ContactStates[runtimeIndex];
			if (input.airborne || !input.grounded)
			{
				state.plantState = PlantState::Unplanted;
				state.support = {};
				state.hasGroundNormal = false;
			}
			m_AcceptedHitsScratch.clear();
			for (std::uint64_t requestId : prepared.requestIds)
			{
				const auto found = std::find_if(results.begin(), results.end(),
					[requestId](const VansWorldQueryResult& result)
					{
						return result.requestId == requestId;
					});
				if (found == results.end() || !found->hit
					|| !Finite(found->positionWorld) || !Finite(found->normalWorld)
					|| glm::dot(found->normalWorld, found->normalWorld) <= kEpsilon * kEpsilon
					|| !ValidSupportHandle(found->support)
					|| (found->hasSupportTransform && !found->support.IsValid())
					|| (found->support.IsValid() && !found->hasSupportTransform)
					|| (found->hasSupportTransform
						&& (!Finite(found->supportPositionWorld) || !Finite(found->supportRotationWorld)
							|| glm::dot(found->supportRotationWorld, found->supportRotationWorld)
								<= kEpsilon * kEpsilon)))
					continue;
				const VansWorldQueryResult& hit = *found;
				if (glm::dot(glm::normalize(hit.normalWorld), worldUp) >= slopeCos)
					m_AcceptedHitsScratch.push_back(&hit);
			}

			m_SupportHitsScratch.clear();
			const bool staticWorldSupport = !m_AcceptedHitsScratch.empty()
				&& std::all_of(m_AcceptedHitsScratch.begin(), m_AcceptedHitsScratch.end(),
					[](const VansWorldQueryResult* hit) { return !hit->supportMovable; });
			if (staticWorldSupport)
			{
				// Static collision actors belong to one immutable world support domain.
				// This keeps coplanar modular floor tiles from forcing a false replant.
				m_SupportHitsScratch = m_AcceptedHitsScratch;
			}
			else if (!m_AcceptedHitsScratch.empty())
			{
				VansSupportHandle dominant;
				int dominantScore = -1;
				for (const VansWorldQueryResult* candidate : m_AcceptedHitsScratch)
				{
					int score = 0;
					for (const VansWorldQueryResult* hit : m_AcceptedHitsScratch)
						if (hit->support == candidate->support) ++score;
					if (score > dominantScore)
					{
						dominant = candidate->support;
						dominantScore = score;
					}
				}
				for (const VansWorldQueryResult* hit : m_AcceptedHitsScratch)
					if (hit->support == dominant) m_SupportHitsScratch.push_back(hit);
			}
			const PlaneFit plane = FitSupportPlane(
				m_SupportHitsScratch, worldUp, m_Settings.query.maxNormalDeviationDegrees);
			bool valid = m_SupportHitsScratch.size() == contact.soleSamplesLocal.size()
				&& plane.valid && plane.residual <= m_Settings.query.maxPlaneResidual
				&& glm::dot(plane.normal, worldUp) >= slopeCos;
			const glm::vec3 animatedFootWorld = TransformPoint(m_OwnerTransform, prepared.animatedFootModel);
			const glm::quat animatedRotationWorld = glm::normalize(
				m_OwnerTransform.rotation * prepared.animatedFootRotationModel);
			glm::vec3 alignmentNormalWorld = plane.normal;
			if (valid)
			{
				if (!state.hasGroundNormal)
				{
					state.groundNormalWorld = plane.normal;
					state.hasGroundNormal = true;
				}
				else
				{
					glm::vec3 wantedNormal = plane.normal;
					if (glm::dot(state.groundNormalWorld, wantedNormal) < 0.0f)
						wantedNormal = -wantedNormal;
					state.groundNormalWorld = glm::normalize(glm::mix(
						state.groundNormalWorld, wantedNormal,
						HalfLifeAlpha(deltaTime, m_Settings.alignment.normalHalfLife)));
				}
				alignmentNormalWorld = state.groundNormalWorld;
			}
			glm::vec3 planeNormalModel = valid
				? TransformNormalToModel(m_OwnerTransform, alignmentNormalWorld)
				: contact.soleNormalLocal;
			if (valid && (!Finite(planeNormalModel)
				|| glm::dot(planeNormalModel, planeNormalModel) <= kEpsilon * kEpsilon))
				valid = false;
			if (valid) planeNormalModel = glm::normalize(planeNormalModel);
			glm::quat targetRotationModel = valid
				? AlignFootRotation(prepared.animatedFootRotationModel,
					contact.soleNormalLocal, contact.soleForwardLocal, planeNormalModel)
				: prepared.animatedFootRotationModel;
			glm::quat targetRotationWorld = glm::normalize(
				m_OwnerTransform.rotation * targetRotationModel);
			glm::vec3 targetPositionWorld = animatedFootWorld;
			float soleClearance = std::numeric_limits<float>::infinity();
			if (valid)
			{
				const glm::vec3 pivotLocal = m_Settings.plant.pivot == VansPlantPivot::Heel
					? contact.heelPivotLocal
					: m_Settings.plant.pivot == VansPlantPivot::Ball
						? contact.ballPivotLocal : contact.anklePivotLocal;
				const glm::vec3 animatedPivotModel = prepared.animatedFootModel
					+ prepared.animatedFootRotationModel * (prepared.animatedFootScaleModel * pivotLocal);
				const glm::vec3 animatedPivotWorld = TransformPoint(m_OwnerTransform, animatedPivotModel);
				const glm::vec3 rotatedPivotWorldOffset = TransformVector(
					m_OwnerTransform,
					targetRotationModel * (prepared.animatedFootScaleModel * pivotLocal));
				targetPositionWorld = animatedPivotWorld - rotatedPivotWorldOffset;
				const float denominator = glm::dot(worldUp, plane.normal);
				float planeCorrection = -std::numeric_limits<float>::infinity();
				for (const VansRigSoleSample& sample : contact.soleSamplesLocal)
				{
					const glm::vec3 rotatedSampleWorld = targetPositionWorld + TransformVector(
						m_OwnerTransform,
						targetRotationModel * (prepared.animatedFootScaleModel * sample.positionLocal));
					planeCorrection = std::max(planeCorrection,
						-glm::dot(rotatedSampleWorld - plane.point, plane.normal) / denominator);
				}
				targetPositionWorld += worldUp * planeCorrection;
				const float verticalTranslation = glm::dot(
					targetPositionWorld - animatedFootWorld, worldUp);
				soleClearance = std::max(0.0f, -verticalTranslation);
				valid = verticalTranslation <= m_Settings.query.maxStepUp
					&& verticalTranslation >= -m_Settings.query.maxStepDown;
				if (!valid)
				{
					targetPositionWorld = animatedFootWorld;
					targetRotationWorld = animatedRotationWorld;
				}
			}
			const VansContactAttribute* attribute = FindContactAttribute(
				input, m_Settings.plantSignal, contact.id);
			const float phase = attribute && attribute->present ? attribute->phase : 0.0f;
			const VansWorldQueryResult* supportHit = !staticWorldSupport
				&& !m_SupportHitsScratch.empty() ? m_SupportHitsScratch.front() : nullptr;
			const VansSupportHandle queriedSupport = supportHit
				? supportHit->support : VansSupportHandle{};
			if (!m_Settings.plant.lockEnabled || input.airborne || !input.grounded)
				state.plantState = PlantState::Unplanted;
			else if (state.solveRejected)
			{
				if (state.plantState != PlantState::Replant)
					state.plantState = PlantState::Unplanted;
			}
			else if (state.plantState == PlantState::Unplanted
				&& valid && phase >= m_Settings.plant.enterPhase)
				state.plantState = PlantState::Candidate;
			else if (state.plantState == PlantState::Candidate)
			{
				if (!valid || phase <= m_Settings.plant.exitPhase)
					state.plantState = PlantState::Unplanted;
				else
				{
					state.plantState = PlantState::Planted;
					state.support = queriedSupport;
					state.lockedPositionWorld = targetPositionWorld;
					state.lockedRotationWorld = targetRotationWorld;
					if (supportHit && state.support.IsValid()
						&& supportHit->hasSupportTransform)
					{
						state.lockedSupportPosition = glm::inverse(supportHit->supportRotationWorld)
							* (targetPositionWorld - supportHit->supportPositionWorld);
						state.lockedSupportRotation = glm::normalize(
							glm::inverse(supportHit->supportRotationWorld) * targetRotationWorld);
					}
				}
			}
			if (state.plantState == PlantState::Planted)
			{
				if (queriedSupport != state.support)
				{
					state.plantState = PlantState::Replant;
					anySupportChanged = true;
				}
				else if (state.support.IsValid())
				{
					if (!supportHit || !supportHit->hasSupportTransform)
					{
						state.plantState = PlantState::Replant;
						anySupportChanged = true;
					}
					else
					{
						state.lockedPositionWorld = SupportPointToWorld(*supportHit, state.lockedSupportPosition);
						state.lockedRotationWorld = glm::normalize(
							supportHit->supportRotationWorld * state.lockedSupportRotation);
					}
				}
				if (state.plantState == PlantState::Planted)
				{
					const float positionError = glm::length(
						targetPositionWorld - state.lockedPositionWorld);
					const float angleError = VansQuaternionAngleDegrees(
						glm::inverse(state.lockedRotationWorld) * targetRotationWorld);
					if (!valid || phase <= m_Settings.plant.exitPhase
						|| positionError > m_Settings.plant.unplantDistance
						|| angleError > m_Settings.plant.unplantAngleDegrees)
						state.plantState = PlantState::Replant;
					else
					{
						targetPositionWorld = state.lockedPositionWorld;
						targetRotationWorld = state.lockedRotationWorld;
					}
				}
			}
			if (state.plantState == PlantState::Replant)
			{
				const float positionError = glm::length(targetPositionWorld - state.lockedPositionWorld);
				const float angleError = VansQuaternionAngleDegrees(
					glm::inverse(state.lockedRotationWorld) * targetRotationWorld);
				if (valid && phase >= m_Settings.plant.enterPhase
					&& positionError <= m_Settings.plant.replantDistance
					&& angleError <= m_Settings.plant.replantAngleDegrees)
				{
					state.support = {};
					state.plantState = PlantState::Candidate;
				}
				else
				{
					// A lost/changed support must fade from the last stable lock instead of
					// jumping to the newly queried (or animated) transform at full weight.
					targetPositionWorld = state.lockedPositionWorld;
					targetRotationWorld = state.lockedRotationWorld;
				}
			}
			const float clearanceConfidence = valid
				? ClearanceWeight(soleClearance,
					m_Settings.alignment.fullContactHeight,
					m_Settings.alignment.contactFadeHeight)
				: 0.0f;
			const float signalConfidence = attribute && attribute->present
				? std::clamp(attribute->confidence, 0.0f, 1.0f) : 0.0f;
			const float contactConfidence = std::max(clearanceConfidence, signalConfidence);
			const float wantedWeight = valid && !input.airborne && input.grounded
				&& state.plantState != PlantState::Replant
				? m_Settings.weight * contactConfidence : 0.0f;
			state.weight += (wantedWeight - state.weight)
				* HalfLifeAlpha(deltaTime, m_Settings.plant.weightHalfLife);
			if (state.plantState == PlantState::Replant && state.weight <= kEpsilon)
			{
				state.support = {};
				state.plantState = valid && phase >= m_Settings.plant.enterPhase
					? PlantState::Candidate : PlantState::Unplanted;
			}
			const int goalIndex = m_Rig->FindGoal(contact.id);
			if (goalIndex >= 0 && state.weight > kEpsilon)
			{
				VansProceduralGoal& goal = outGoals[static_cast<std::size_t>(goalIndex)];
				goal.positionModel = InverseTransformPoint(m_OwnerTransform, targetPositionWorld);
				goal.rotationModel = glm::normalize(
					glm::inverse(m_OwnerTransform.rotation) * targetRotationWorld);
				goal.positionWeight = state.weight;
				goal.rotationWeight = state.weight * m_Settings.alignment.rotationWeight;
				goal.valid = true;
				anyGoal = true;
			}
			anyQueryRejected = anyQueryRejected
				|| (input.grounded && !input.airborne && !valid);
		}

		bool anyReachRejected = false;
		if (!ApplyPelvis(deltaTime, workspace, outGoals, modelUp, anyReachRejected))
			return false;
		anyGoal = false;
		for (std::size_t runtimeIndex = 0; runtimeIndex < m_Settings.contactIndices.size(); ++runtimeIndex)
		{
			const VansCompiledRigContact& contact = m_Rig->contacts[static_cast<std::size_t>(
				m_Settings.contactIndices[runtimeIndex])];
			const int goalIndex = m_Rig->FindGoal(contact.id);
			if (goalIndex < 0) continue;
			VansProceduralGoal& goal = outGoals[static_cast<std::size_t>(goalIndex)];
			if (!goal.valid) continue;
			const VansCompiledRigChain& chain =
				m_Rig->chains[static_cast<std::size_t>(contact.chainIndex)];
			const glm::vec3 root = workspace.GetComponentPosition(chain.boneIndices.front());
			const float upperLength = glm::length(
				workspace.GetComponentPosition(chain.boneIndices[1]) - root);
			const float lowerLength = glm::length(
				workspace.GetComponentPosition(chain.boneIndices[2])
				- workspace.GetComponentPosition(chain.boneIndices[1]));
			const float distance = glm::length(goal.positionModel - root);
			const float minimumReach = std::abs(upperLength - lowerLength);
			const float maximumReach = (upperLength + lowerLength) * chain.softReachStartRatio;
			if (!std::isfinite(distance) || distance + 1.0e-4f < minimumReach
				|| distance > maximumReach + 1.0e-4f)
			{
				// Preserve the requested transform for diagnostics while preventing an
				// unreachable target from being consumed by a downstream solver.
				goal.positionWeight = 0.0f;
				goal.rotationWeight = 0.0f;
				goal.valid = false;
				ContactState& state = m_ContactStates[runtimeIndex];
				state.plantState = PlantState::Unplanted;
				state.support = {};
				anyReachRejected = true;
				continue;
			}
			anyGoal = true;
		}
		const bool anyRejected = anyQueryRejected || anyReachRejected;
		outResult.status = anyRejected ? VansProceduralSolverStatus::Clamped
			: anyGoal ? VansProceduralSolverStatus::Solved
				: VansProceduralSolverStatus::NoEffect;
		if (anyQueryRejected) outResult.limitReason |= VansProceduralLimitReason::Query;
		if (anyReachRejected) outResult.limitReason |= VansProceduralLimitReason::Reach;
		if (anySupportChanged) outResult.limitReason |= VansProceduralLimitReason::SupportChanged;
		outResult.iterations = 1;
		// A zero-duration evaluation may refresh query-derived output, but it must
		// not advance persistent plant, support, weight, or pelvis state.
		if (deltaTime == 0.0f)
		{
			m_ContactStates = m_TransactionContactStates;
			m_PelvisOffsetModel = m_TransactionPelvisOffsetModel;
		}
		m_HasPreparedContacts = false;
		return true;
	}

	void VansGroundingRuntime::CommitResolvedState()
	{
		m_HasResolvedTransaction = false;
	}

	void VansGroundingRuntime::RollbackResolvedState()
	{
		if (!m_HasResolvedTransaction)
			return;
		m_ContactStates = m_TransactionContactStates;
		m_PelvisOffsetModel = m_TransactionPelvisOffsetModel;
		m_HasResolvedTransaction = false;
		m_HasPreparedContacts = false;
	}

	void VansGroundingRuntime::ReportLimbSolve(
		int chainIndex, const VansProceduralSolverResult& result)
	{
		if (!m_HasResolvedTransaction) return;
		for (std::size_t runtimeIndex = 0;
			runtimeIndex < m_Settings.contactIndices.size(); ++runtimeIndex)
		{
			const VansCompiledRigContact& contact = m_Rig->contacts[static_cast<std::size_t>(
				m_Settings.contactIndices[runtimeIndex])];
			if (contact.chainIndex != chainIndex) continue;
			ContactState& state = m_ContactStates[runtimeIndex];
			if (result.status == VansProceduralSolverStatus::Solved)
			{
				state.solveRejected = false;
				continue;
			}
			state.solveRejected = true;
			if (state.plantState == PlantState::Planted
				|| state.plantState == PlantState::Replant)
				state.plantState = PlantState::Replant;
			else
			{
				state.plantState = PlantState::Unplanted;
				state.support = {};
			}
		}
	}

	const VansContactAttribute* VansGroundingRuntime::FindContactAttribute(
		const VansAnimationExternalInputSnapshot& input,
		const std::string& provider,
		const std::string& contactId) const
	{
		const auto found = std::find_if(input.contacts.begin(), input.contacts.end(),
			[&provider, &contactId](const VansContactAttribute& value)
			{
				return value.provider == provider && value.id == contactId;
			});
		return found == input.contacts.end() ? nullptr : &*found;
	}

	bool VansGroundingRuntime::ApplyPelvis(
		float deltaTime,
		VansPoseWorkspace& workspace,
		std::vector<VansProceduralGoal>& goals,
		const glm::vec3& modelUp,
		bool& outReachRejected)
	{
		outReachRejected = false;
		const auto pelvisFound = m_Rig->semanticBoneIndices.find("pelvis");
		if (pelvisFound == m_Rig->semanticBoneIndices.end()) return false;
		const glm::vec3 up = glm::normalize(modelUp);
		const glm::vec3 worldUp = glm::normalize(TransformVector(m_OwnerTransform, up));
		auto clampOffset = [&](const glm::vec3& value)
		{
			const glm::vec3 worldValue = TransformVector(m_OwnerTransform, value);
			const float vertical = std::clamp(glm::dot(worldValue, worldUp),
				-m_Settings.pelvis.maxDownOffset,
				m_Settings.pelvis.maxUpOffset);
			glm::vec3 horizontal = worldValue - worldUp * glm::dot(worldValue, worldUp);
			const float horizontalLength = glm::length(horizontal);
			if (horizontalLength > m_Settings.pelvis.maxHorizontalOffset
				&& horizontalLength > kEpsilon)
				horizontal *= m_Settings.pelvis.maxHorizontalOffset / horizontalLength;
			return InverseTransformVector(
				m_OwnerTransform, horizontal + worldUp * vertical);
		};
		auto computeWanted = [&]()
		{
			glm::vec3 wanted(0.0f);
			float weightSum = 0.0f;
			for (int contactIndex : m_Settings.contactIndices)
			{
				const VansCompiledRigContact& contact =
					m_Rig->contacts[static_cast<std::size_t>(contactIndex)];
				const int goalIndex = m_Rig->FindGoal(contact.id);
				if (goalIndex < 0 || !goals[static_cast<std::size_t>(goalIndex)].valid)
					continue;
				const VansProceduralGoal& goal = goals[static_cast<std::size_t>(goalIndex)];
				const float weight = std::max(goal.positionWeight, kEpsilon);
				wanted += (goal.positionModel
					- workspace.GetComponentPosition(contact.footBoneIndex)) * weight;
				weightSum += weight;
			}
			return clampOffset(weightSum > kEpsilon ? wanted / weightSum : glm::vec3(0.0f));
		};
		auto projectConstraints = [&](glm::vec3& offset)
		{
			constexpr int kProjectionIterations = 4;
			for (int iteration = 0; iteration < kProjectionIterations; ++iteration)
			{
				for (int priority = 0; priority < 2; ++priority)
				{
					for (std::size_t runtimeIndex = 0;
						runtimeIndex < m_Settings.contactIndices.size(); ++runtimeIndex)
					{
						const bool planted = m_ContactStates[runtimeIndex].plantState == PlantState::Planted;
						if ((priority == 0) != planted) continue;
						const VansCompiledRigContact& contact = m_Rig->contacts[static_cast<std::size_t>(
							m_Settings.contactIndices[runtimeIndex])];
						const int goalIndex = m_Rig->FindGoal(contact.id);
						if (goalIndex < 0 || !goals[static_cast<std::size_t>(goalIndex)].valid)
							continue;
						const VansCompiledRigChain& chain =
							m_Rig->chains[static_cast<std::size_t>(contact.chainIndex)];
						const glm::vec3 root = workspace.GetComponentPosition(chain.boneIndices.front());
						const float upperLength = glm::length(
							workspace.GetComponentPosition(chain.boneIndices[1]) - root);
						const float lowerLength = glm::length(
							workspace.GetComponentPosition(chain.boneIndices[2])
								- workspace.GetComponentPosition(chain.boneIndices[1]));
						const float minimumReach = std::abs(upperLength - lowerLength);
						const float maximumReach = (upperLength + lowerLength)
							* chain.softReachStartRatio;
						const glm::vec3 toGoal = goals[static_cast<std::size_t>(goalIndex)].positionModel
							- (root + offset);
						const float distance = glm::length(toGoal);
						const glm::vec3 direction = distance > kEpsilon
							? toGoal / distance : -up;
						if (distance > maximumReach)
							offset += direction * (distance - maximumReach);
						else if (distance < minimumReach)
							offset -= direction * (minimumReach - distance);
						offset = clampOffset(offset);
					}
				}
			}
		};
		auto constraintViolation = [&](std::size_t runtimeIndex, const glm::vec3& offset)
		{
			const VansCompiledRigContact& contact = m_Rig->contacts[static_cast<std::size_t>(
				m_Settings.contactIndices[runtimeIndex])];
			const int goalIndex = m_Rig->FindGoal(contact.id);
			if (goalIndex < 0 || !goals[static_cast<std::size_t>(goalIndex)].valid)
				return 0.0f;
			const VansCompiledRigChain& chain =
				m_Rig->chains[static_cast<std::size_t>(contact.chainIndex)];
			const glm::vec3 root = workspace.GetComponentPosition(chain.boneIndices.front());
			const float upperLength = glm::length(
				workspace.GetComponentPosition(chain.boneIndices[1]) - root);
			const float lowerLength = glm::length(
				workspace.GetComponentPosition(chain.boneIndices[2])
					- workspace.GetComponentPosition(chain.boneIndices[1]));
			const float minimumReach = std::abs(upperLength - lowerLength);
			const float maximumReach = (upperLength + lowerLength)
				* chain.softReachStartRatio;
			const float distance = glm::length(
				goals[static_cast<std::size_t>(goalIndex)].positionModel - (root + offset));
			if (!std::isfinite(distance)) return std::numeric_limits<float>::infinity();
			return std::max({ minimumReach - distance, distance - maximumReach, 0.0f });
		};

		glm::vec3 wanted = computeWanted();
		for (std::size_t rejection = 0;
			rejection < m_Settings.contactIndices.size(); ++rejection)
		{
			projectConstraints(wanted);
			std::size_t victim = m_Settings.contactIndices.size();
			float victimPriority = std::numeric_limits<float>::infinity();
			for (std::size_t runtimeIndex = 0;
				runtimeIndex < m_Settings.contactIndices.size(); ++runtimeIndex)
			{
				if (constraintViolation(runtimeIndex, wanted) <= 1.0e-4f) continue;
				const VansCompiledRigContact& contact = m_Rig->contacts[static_cast<std::size_t>(
					m_Settings.contactIndices[runtimeIndex])];
				const int goalIndex = m_Rig->FindGoal(contact.id);
				const bool planted = m_ContactStates[runtimeIndex].plantState == PlantState::Planted;
				const float priority = (planted ? 1.0f : 0.0f)
					+ (goalIndex >= 0 ? goals[static_cast<std::size_t>(goalIndex)].positionWeight : 0.0f);
				if (priority < victimPriority)
				{
					victimPriority = priority;
					victim = runtimeIndex;
				}
			}
			if (victim == m_Settings.contactIndices.size()) break;
			const VansCompiledRigContact& contact = m_Rig->contacts[static_cast<std::size_t>(
				m_Settings.contactIndices[victim])];
			const int goalIndex = m_Rig->FindGoal(contact.id);
			if (goalIndex >= 0) goals[static_cast<std::size_t>(goalIndex)] = {};
			m_ContactStates[victim].plantState = PlantState::Unplanted;
			m_ContactStates[victim].support = {};
			outReachRejected = true;
			wanted = computeWanted();
		}

		m_PelvisOffsetModel += (wanted - m_PelvisOffsetModel)
			* HalfLifeAlpha(deltaTime, m_Settings.pelvis.halfLife);
		m_PelvisOffsetModel = clampOffset(m_PelvisOffsetModel);
		projectConstraints(m_PelvisOffsetModel);
		const int pelvis = pelvisFound->second;
		const int parent = m_Rig->skeleton->bones[static_cast<std::size_t>(pelvis)].parentIndex;
		glm::vec3 localOffset = m_PelvisOffsetModel;
		if (workspace.IsValidBone(parent))
		{
			localOffset = glm::inverse(workspace.GetComponentRotation(parent)) * localOffset;
			const glm::vec3 scale = workspace.GetComponentScale(parent);
			for (int axis = 0; axis < 3; ++axis)
				localOffset[axis] = std::abs(scale[axis]) > kEpsilon
					? localOffset[axis] / scale[axis] : 0.0f;
		}
		VansBoneTransform transform = workspace.GetLocal(pelvis);
		transform.translation += localOffset;
		return workspace.SetLocal(pelvis, transform);
	}
}
