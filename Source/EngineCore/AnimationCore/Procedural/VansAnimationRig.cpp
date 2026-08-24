#include "VansAnimationRig.h"

#include "../VansPoseMath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <unordered_set>

namespace VansGraphics
{
	namespace
	{
		constexpr float kAxisEpsilon = 1.0e-6f;

		bool Finite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool Finite(const glm::quat& value)
		{
			return std::isfinite(value.w) && std::isfinite(value.x)
				&& std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool ValidAxis(const glm::vec3& value)
		{
			return Finite(value) && glm::dot(value, value) > kAxisEpsilon * kAxisEpsilon;
		}

		int ResolveBone(const Skeleton& skeleton, const std::string& name)
		{
			const auto found = skeleton.boneNameToIndex.find(name);
			return found == skeleton.boneNameToIndex.end() ? -1 : found->second;
		}

		bool IsContinuous(const Skeleton& skeleton, const std::vector<int>& bones)
		{
			for (std::size_t index = 1; index < bones.size(); ++index)
			{
				if (skeleton.bones[static_cast<std::size_t>(bones[index])].parentIndex
					!= bones[index - 1])
					return false;
			}
			return true;
		}
	}

	int VansCompiledAnimationRig::FindGoal(const std::string& id) const
	{
		const auto found = goalIndexById.find(id);
		return found == goalIndexById.end() ? -1 : found->second;
	}

	int VansCompiledAnimationRig::FindSocketByGuid(const std::string& guid) const
	{
		const auto found = socketIndexByGuid.find(guid);
		return found == socketIndexByGuid.end() ? -1 : found->second;
	}

	int VansCompiledAnimationRig::FindSocketByName(const std::string& name) const
	{
		const auto found = socketIndexByName.find(name);
		return found == socketIndexByName.end() ? -1 : found->second;
	}

	int VansCompiledAnimationRig::FindChain(const std::string& id) const
	{
		const auto found = chainIndexById.find(id);
		return found == chainIndexById.end() ? -1 : found->second;
	}

	int VansCompiledAnimationRig::FindContact(const std::string& id) const
	{
		const auto found = contactIndexById.find(id);
		return found == contactIndexById.end() ? -1 : found->second;
	}

	const VansCompiledRigJointLimit* VansCompiledAnimationRig::FindJointLimit(int boneIndex) const
	{
		const auto found = std::find_if(jointLimits.begin(), jointLimits.end(),
			[boneIndex](const VansCompiledRigJointLimit& limit)
			{ return limit.boneIndex == boneIndex; });
		return found == jointLimits.end() ? nullptr : &*found;
	}

	bool VansCompiledAnimationRig::BindSkeleton(
		const Skeleton& targetSkeleton,
		std::string& error)
	{
		error.clear();
		if (targetSkeleton.bones.empty())
		{
			error = "Animation Rig cannot bind an empty Skeleton";
			return false;
		}
		if (skeletonGuid != targetSkeleton.sourceSkeletonGuid
			|| skeletonSignature == 0
			|| skeletonSignature != VansAnimationRigCompiler::ComputeSkeletonSignature(targetSkeleton))
		{
			error = "Animation Rig '" + name
				+ "' was compiled for a different Skeleton layout or bind pose";
			return false;
		}
		skeleton = &targetSkeleton;
		return true;
	}

	std::uint64_t VansAnimationRigCompiler::ComputeSkeletonSignature(const Skeleton& skeleton)
	{
		return skeleton.ComputeSignature();
	}

	bool VansAnimationRigCompiler::Compile(
		const VansAnimationRigAsset& asset,
		const Skeleton& skeleton,
		VansCompiledAnimationRig& outRig,
		std::string& error)
	{
		error.clear();
		outRig = {};
		if (asset.name.empty() || asset.skeletonGuid.empty() || skeleton.bones.empty()
			|| asset.skeletonGuid != skeleton.sourceSkeletonGuid)
		{
			error = "Animation Rig requires name, a non-empty Skeleton, and its exact skeletonGuid";
			return false;
		}
		if (!ValidAxis(asset.modelForward) || !ValidAxis(asset.modelUp))
		{
			error = "Animation Rig model axes must be finite non-zero vectors";
			return false;
		}
		const glm::vec3 forward = glm::normalize(asset.modelForward);
		const glm::vec3 up = glm::normalize(asset.modelUp);
		if (std::abs(glm::dot(forward, up)) > 0.05f)
		{
			error = "Animation Rig model forward/up axes must be orthogonal";
			return false;
		}

		outRig.name = asset.name;
		outRig.skeletonGuid = asset.skeletonGuid;
		outRig.skeletonSignature = ComputeSkeletonSignature(skeleton);
		outRig.skeleton = &skeleton;
		outRig.modelForward = forward;
		outRig.modelUp = up;
		for (const auto& [semantic, boneName] : asset.semanticBones)
		{
			const int boneIndex = ResolveBone(skeleton, boneName);
			if (semantic.empty() || boneIndex < 0)
			{
				error = "Animation Rig semantic bone '" + semantic + "' cannot resolve '" + boneName + "'";
				return false;
			}
			outRig.semanticBoneIndices.emplace(semantic, boneIndex);
		}

		for (const VansRigSocketDefinition& source : asset.sockets)
		{
			const auto bone = skeleton.boneGuidToIndex.find(source.boneGuid);
			const float rotationLength = glm::length(source.rotationLocal);
			if (source.guid.empty() || source.name.empty() || source.boneGuid.empty()
				|| bone == skeleton.boneGuidToIndex.end()
				|| outRig.socketIndexByGuid.find(source.guid) != outRig.socketIndexByGuid.end()
				|| outRig.socketIndexByName.find(source.name) != outRig.socketIndexByName.end()
				|| !Finite(source.positionLocal) || !Finite(source.rotationLocal)
				|| !std::isfinite(rotationLength) || rotationLength <= kAxisEpsilon
				|| !Finite(source.scaleLocal)
				|| std::abs(source.scaleLocal.x) <= kAxisEpsilon
				|| std::abs(source.scaleLocal.y) <= kAxisEpsilon
				|| std::abs(source.scaleLocal.z) <= kAxisEpsilon)
			{
				error = "Animation Rig sockets require unique guid/name, a stable boneGuid, and finite local TRS";
				return false;
			}
			VansBoneTransform local;
			local.translation = source.positionLocal;
			local.rotation = glm::normalize(source.rotationLocal);
			local.scale = source.scaleLocal;
			const int socketIndex = static_cast<int>(outRig.sockets.size());
			outRig.socketIndexByGuid.emplace(source.guid, socketIndex);
			outRig.socketIndexByName.emplace(source.name, socketIndex);
			outRig.sockets.push_back({ source.guid, source.name, bone->second,
				VansPoseMath::Compose(local) });
		}

		for (const VansRigGoalDefinition& source : asset.goals)
		{
			const int effector = ResolveBone(skeleton, source.effectorBone);
			if (source.id.empty() || effector < 0
				|| outRig.goalIndexById.find(source.id) != outRig.goalIndexById.end())
			{
				error = "Animation Rig goals require unique ids and resolvable effector bones";
				return false;
			}
			outRig.goalIndexById.emplace(source.id, static_cast<int>(outRig.goals.size()));
			outRig.goals.push_back({ source.id, effector });
		}

		for (const VansRigChainDefinition& source : asset.chains)
		{
			if ((source.solver != VansRigSolverKind::Limb
					&& source.solver != VansRigSolverKind::CCD
					&& source.solver != VansRigSolverKind::FABRIK
					&& source.solver != VansRigSolverKind::Aim)
				|| source.id.empty() || source.bones.size() < 2
				|| source.bones.size() > VansMaxProceduralChainBones
				|| outRig.chainIndexById.find(source.id) != outRig.chainIndexById.end())
			{
				error = "Animation Rig chains require unique ids and 2..64 bones";
				return false;
			}
			VansCompiledRigChain compiled;
			compiled.id = source.id;
			compiled.solver = source.solver;
			compiled.goalIndex = outRig.FindGoal(source.goal);
			compiled.poleAxisLocal = source.poleAxisLocal;
			compiled.softReachStartRatio = source.softReachStartRatio;
			compiled.maxStretchScale = source.maxStretchScale;
			compiled.weights = source.weights;
			compiled.solveWeights = source.solveWeights;
			compiled.maxStepDegrees = source.maxStepDegrees;
			compiled.forwardAxisLocal = source.forwardAxisLocal;
			compiled.upAxisLocal = source.upAxisLocal;
			for (const std::string& boneName : source.bones)
				compiled.boneIndices.push_back(ResolveBone(skeleton, boneName));
			if (compiled.goalIndex < 0
				|| std::find(compiled.boneIndices.begin(), compiled.boneIndices.end(), -1)
					!= compiled.boneIndices.end()
				|| !IsContinuous(skeleton, compiled.boneIndices))
			{
				error = "Animation Rig chain '" + source.id + "' must be continuous and reference a valid goal";
				return false;
			}
			if (outRig.goals[static_cast<std::size_t>(compiled.goalIndex)].effectorBoneIndex
				!= compiled.boneIndices.back())
			{
				error = "Animation Rig chain '" + source.id + "' goal effector must equal the chain tip";
				return false;
			}
			if (compiled.solver == VansRigSolverKind::Limb
				&& (compiled.boneIndices.size() != 3 || !ValidAxis(compiled.poleAxisLocal)
					|| !compiled.weights.empty() || !compiled.solveWeights.empty()
					|| !std::isfinite(compiled.softReachStartRatio)
					|| compiled.softReachStartRatio <= 0.0f || compiled.softReachStartRatio >= 1.0f
					|| !std::isfinite(compiled.maxStretchScale) || compiled.maxStretchScale < 1.0f))
			{
				error = "Limb chain '" + source.id + "' requires three bones, a pole axis, softReachStartRatio in (0,1), and maxStretchScale >= 1";
				return false;
			}
			if (compiled.solver == VansRigSolverKind::Limb)
			{
				compiled.poleAxisLocal = glm::normalize(compiled.poleAxisLocal);
				VansBoneTransform rootBindLocal;
				const int rootIndex = compiled.boneIndices.front();
				if (!VansPoseMath::TryDecompose(
					skeleton.bones[static_cast<std::size_t>(rootIndex)].localTransform,
					rootBindLocal))
				{
					error = "Limb chain '" + source.id
						+ "' root bind transform cannot define a stable pole frame";
					return false;
				}
				compiled.poleAxisParentLocal = glm::normalize(
					rootBindLocal.rotation * compiled.poleAxisLocal);
			}
			if (compiled.solver == VansRigSolverKind::Aim)
			{
				if (!compiled.solveWeights.empty()
					|| compiled.weights.size() != compiled.boneIndices.size()
					|| !ValidAxis(compiled.forwardAxisLocal) || !ValidAxis(compiled.upAxisLocal)
					|| std::abs(glm::dot(glm::normalize(compiled.forwardAxisLocal),
						glm::normalize(compiled.upAxisLocal))) > 0.05f
					|| std::any_of(compiled.weights.begin(), compiled.weights.end(),
						[](float value) { return !std::isfinite(value) || value < 0.0f; }))
				{
					error = "Aim chain '" + source.id + "' requires one non-negative weight per bone and valid local axes";
					return false;
				}
				const float sum = std::accumulate(compiled.weights.begin(), compiled.weights.end(), 0.0f);
				if (sum <= kAxisEpsilon)
				{
					error = "Aim chain '" + source.id + "' weights must have a positive sum";
					return false;
				}
				for (float& weight : compiled.weights)
					weight /= sum;
				compiled.forwardAxisLocal = glm::normalize(compiled.forwardAxisLocal);
				compiled.upAxisLocal -= compiled.forwardAxisLocal
					* glm::dot(compiled.upAxisLocal, compiled.forwardAxisLocal);
				compiled.upAxisLocal = glm::normalize(compiled.upAxisLocal);
			}
			if (compiled.solver == VansRigSolverKind::CCD
				|| compiled.solver == VansRigSolverKind::FABRIK)
			{
				if (!compiled.weights.empty()
					|| compiled.solveWeights.size() + 1 != compiled.boneIndices.size()
					|| std::any_of(compiled.solveWeights.begin(), compiled.solveWeights.end(),
						[](float value)
						{ return !std::isfinite(value) || value < 0.0f || value > 1.0f; })
					|| (compiled.solver == VansRigSolverKind::CCD
						&& (!std::isfinite(compiled.maxStepDegrees)
							|| compiled.maxStepDegrees <= 0.0f || compiled.maxStepDegrees > 180.0f)))
				{
					error = "CCD/FABRIK chain '" + source.id
						+ "' requires one solveWeight per movable bone; CCD also requires maxStepDegrees in (0,180]";
					return false;
				}
			}
			for (std::size_t index = 1; index < compiled.boneIndices.size(); ++index)
			{
				VansBoneTransform local;
				if (!VansPoseMath::TryDecompose(
					skeleton.bones[static_cast<std::size_t>(compiled.boneIndices[index])].localTransform, local))
				{
					error = "Animation Rig chain '" + source.id + "' has a non-decomposable rest transform";
					return false;
				}
				compiled.restSegmentLengths.push_back(glm::length(local.translation));
			}
			if (compiled.solver != VansRigSolverKind::Aim
				&& std::any_of(compiled.restSegmentLengths.begin(), compiled.restSegmentLengths.end(),
				[](float length) { return !std::isfinite(length) || length <= kAxisEpsilon; }))
			{
				error = "Animation Rig chain '" + source.id + "' contains a zero-length rest segment";
				return false;
			}
			outRig.chainIndexById.emplace(compiled.id, static_cast<int>(outRig.chains.size()));
			outRig.chains.push_back(std::move(compiled));
		}

		std::unordered_set<int> limitedBones;
		for (const VansRigJointLimitDefinition& source : asset.jointLimits)
		{
			const int boneIndex = ResolveBone(skeleton, source.bone);
			if ((source.kind != VansJointLimitKind::Hinge
					&& source.kind != VansJointLimitKind::SwingTwist
					&& source.kind != VansJointLimitKind::Locked)
				|| boneIndex < 0 || !limitedBones.insert(boneIndex).second
				|| (source.kind != VansJointLimitKind::Locked
					&& (!ValidAxis(source.axisLocal)
						|| !std::isfinite(source.minDegrees) || !std::isfinite(source.maxDegrees)
						|| source.minDegrees < -180.0f || source.maxDegrees > 180.0f
						|| source.minDegrees > source.maxDegrees))
				|| (source.kind == VansJointLimitKind::SwingTwist
					&& (!ValidAxis(source.swingReferenceAxisLocal)
						|| std::abs(glm::dot(glm::normalize(source.axisLocal),
							glm::normalize(source.swingReferenceAxisLocal))) > 0.05f
						|| !std::isfinite(source.swingLimitDegrees.x)
						|| !std::isfinite(source.swingLimitDegrees.y)
						|| source.swingLimitDegrees.x < 0.0f || source.swingLimitDegrees.x > 180.0f
						|| source.swingLimitDegrees.y < 0.0f || source.swingLimitDegrees.y > 180.0f)))
			{
				error = "Animation Rig joint limits require unique bones, valid axes, and ordered finite angles";
				return false;
			}
			VansBoneTransform rest;
			if (!VansPoseMath::TryDecompose(skeleton.bones[static_cast<std::size_t>(boneIndex)].localTransform, rest))
			{
				error = "Animation Rig joint limit rest transform cannot be decomposed";
				return false;
			}
			const glm::vec3 compiledAxis = source.kind == VansJointLimitKind::Locked
				? glm::vec3(1.0f, 0.0f, 0.0f) : glm::normalize(source.axisLocal);
			glm::vec3 compiledSwingReference(0.0f, 0.0f, 1.0f);
			if (source.kind == VansJointLimitKind::SwingTwist)
			{
				compiledSwingReference = source.swingReferenceAxisLocal
					- compiledAxis * glm::dot(source.swingReferenceAxisLocal, compiledAxis);
				compiledSwingReference = glm::normalize(compiledSwingReference);
			}
			outRig.jointLimits.push_back({ boneIndex, source.kind, compiledAxis,
				compiledSwingReference,
				rest.rotation, source.minDegrees, source.maxDegrees, source.swingLimitDegrees });
		}

		for (const VansRigContactDefinition& source : asset.contacts)
		{
			VansCompiledRigContact contact;
			contact.id = source.id;
			contact.chainIndex = outRig.FindChain(source.chain);
			contact.footBoneIndex = ResolveBone(skeleton, source.footBone);
			contact.ballBoneIndex = source.ballBone.empty() ? -1 : ResolveBone(skeleton, source.ballBone);
			contact.soleForwardLocal = source.soleForwardLocal;
			contact.soleNormalLocal = source.soleNormalLocal;
			contact.soleSamplesLocal = source.soleSamplesLocal;
			contact.heelPivotLocal = source.heelPivotLocal;
			contact.ballPivotLocal = source.ballPivotLocal;
			contact.anklePivotLocal = source.anklePivotLocal;
			contact.sweepRadius = source.sweepRadius;
			if (contact.id.empty() || outRig.contactIndexById.find(contact.id) != outRig.contactIndexById.end()
				|| contact.chainIndex < 0 || contact.footBoneIndex < 0
				|| outRig.chains[static_cast<std::size_t>(contact.chainIndex)].boneIndices.back()
					!= contact.footBoneIndex
				|| outRig.chains[static_cast<std::size_t>(contact.chainIndex)].solver
					!= VansRigSolverKind::Limb
				|| outRig.FindGoal(contact.id) < 0
				|| outRig.chains[static_cast<std::size_t>(contact.chainIndex)].goalIndex
					!= outRig.FindGoal(contact.id)
				|| (!source.ballBone.empty() && (contact.ballBoneIndex < 0
					|| skeleton.bones[static_cast<std::size_t>(contact.ballBoneIndex)].parentIndex
						!= contact.footBoneIndex))
				|| !ValidAxis(contact.soleForwardLocal) || !ValidAxis(contact.soleNormalLocal)
				|| std::abs(glm::dot(glm::normalize(contact.soleForwardLocal),
					glm::normalize(contact.soleNormalLocal))) > 0.05f
				|| contact.soleSamplesLocal.size() < 3
				|| !std::isfinite(contact.sweepRadius) || contact.sweepRadius < 0.0f)
			{
				error = "Animation Rig contact '" + source.id + "' has invalid chain, bones, geometry, or radius";
				return false;
			}
			std::unordered_set<std::string> sampleIds;
			for (const VansRigSoleSample& sample : contact.soleSamplesLocal)
			{
				if (sample.id.empty() || !sampleIds.insert(sample.id).second || !Finite(sample.positionLocal))
				{
					error = "Animation Rig contact samples require unique ids and finite positions";
					return false;
				}
			}
			bool nonCollinear = false;
			glm::vec3 soleNormal(0.0f);
			for (std::size_t first = 1; first + 1 < contact.soleSamplesLocal.size() && !nonCollinear; ++first)
			{
				const glm::vec3 a = contact.soleSamplesLocal[first].positionLocal
					- contact.soleSamplesLocal[0].positionLocal;
				const glm::vec3 b = contact.soleSamplesLocal[first + 1].positionLocal
					- contact.soleSamplesLocal[0].positionLocal;
				soleNormal = glm::cross(a, b);
				nonCollinear = glm::length(soleNormal) > kAxisEpsilon;
			}
			if (!nonCollinear || !Finite(contact.heelPivotLocal) || !Finite(contact.ballPivotLocal)
				|| !Finite(contact.anklePivotLocal))
			{
				error = "Animation Rig contact '" + source.id + "' requires finite, non-collinear sole geometry";
				return false;
			}
			contact.soleForwardLocal = glm::normalize(contact.soleForwardLocal);
			contact.soleNormalLocal = glm::normalize(contact.soleNormalLocal);
			contact.soleForwardLocal -= contact.soleNormalLocal
				* glm::dot(contact.soleForwardLocal, contact.soleNormalLocal);
			contact.soleForwardLocal = glm::normalize(contact.soleForwardLocal);
			const float planeAgreement = std::abs(glm::dot(
				glm::normalize(soleNormal), contact.soleNormalLocal));
			const glm::vec3 planePoint = contact.soleSamplesLocal.front().positionLocal;
			float maxPlaneResidual = 0.0f;
			for (const VansRigSoleSample& sample : contact.soleSamplesLocal)
				maxPlaneResidual = std::max(maxPlaneResidual,
					std::abs(glm::dot(sample.positionLocal - planePoint,
						contact.soleNormalLocal)));
			if (planeAgreement < 0.999f || maxPlaneResidual > 1.0e-3f)
			{
				error = "Animation Rig contact '" + source.id
					+ "' soleNormalLocal does not match its authored sole plane";
				return false;
			}
			outRig.contactIndexById.emplace(contact.id, static_cast<int>(outRig.contacts.size()));
			outRig.contacts.push_back(std::move(contact));
		}
		return true;
	}
}
