#include "VansRetargetProcessor.h"
#include "../Procedural/Solvers/VansLimbIKSolver.h"
#include "../VansPoseMath.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

using namespace VansGraphics;

namespace
{
	float SafeLengthRatio(const glm::vec3& target, const glm::vec3& source)
	{
		const float sourceLen = glm::length(source);
		const float targetLen = glm::length(target);
		if (sourceLen <= 0.0001f || targetLen <= 0.0001f)
			return 1.0f;
		return targetLen / sourceLen;
	}

	glm::vec3 ExtractTranslation(const glm::mat4& transform)
	{
		return glm::vec3(transform[3]);
	}

	glm::vec3 NormalizeOrFallback(const glm::vec3& value, const glm::vec3& fallback)
	{
		const float len = glm::length(value);
		return len > 1.0e-5f ? value / len : fallback;
	}

	glm::mat4 Mat4FromMat3(const glm::mat3& value)
	{
		glm::mat4 result(1.0f);
		result[0] = glm::vec4(value[0], 0.0f);
		result[1] = glm::vec4(value[1], 0.0f);
		result[2] = glm::vec4(value[2], 0.0f);
		return result;
	}

	bool HasValidTopologicalOrder(const Skeleton& skeleton)
	{
		if (skeleton.topologicalOrder.size() != skeleton.bones.size()) return false;
		std::vector<bool> visited(skeleton.bones.size(), false);
		for (int bone : skeleton.topologicalOrder)
		{
			if (bone < 0 || bone >= static_cast<int>(skeleton.bones.size())
				|| visited[static_cast<std::size_t>(bone)]) return false;
			const int parent = skeleton.bones[static_cast<std::size_t>(bone)].parentIndex;
			if (parent >= 0 && (parent >= static_cast<int>(skeleton.bones.size())
				|| !visited[static_cast<std::size_t>(parent)])) return false;
			visited[static_cast<std::size_t>(bone)] = true;
		}
		return true;
	}
}

bool VansRetargetProcessor::Build(
	const Skeleton& sourceSkeleton,
	const Skeleton& targetSkeleton,
	const VansCompiledAnimationRig& targetRig,
	const VansRetargetRuntimeDesc& desc)
{
	m_BoneMap.clear();
	m_LimbChains.clear();
	m_TargetRig = {};
	m_Stats = {};
	m_SourceSkeleton = nullptr;
	m_TargetSkeleton = nullptr;
	m_Stats.sourceBoneCount = static_cast<uint32_t>(sourceSkeleton.bones.size());
	m_Stats.targetBoneCount = static_cast<uint32_t>(targetSkeleton.bones.size());
	m_SourceBindModelTransforms.clear();
	m_TargetBindModelTransforms.clear();
	m_TargetModelSpaceCorrection = glm::mat4(1.0f);
	m_RootAlignmentCorrection = glm::mat4(1.0f);
	m_HasTargetModelSpaceCorrection = false;
	m_HasRootAlignmentCorrection = false;
	m_Valid = false;
	m_SourceLocalScratch.clear();
	m_TargetLocalScratch.clear();
	m_DesiredModelRotationsScratch.clear();
	m_MappedTargetBonesScratch.clear();
	m_ResolvedModelScratch.clear();
	m_LocalPoseScratch.clear();

	if (sourceSkeleton.bones.empty() || targetSkeleton.bones.empty()
		|| !HasValidTopologicalOrder(sourceSkeleton)
		|| !HasValidTopologicalOrder(targetSkeleton)
		|| targetRig.skeleton != &targetSkeleton)
		return false;
	if (desc.translationScaleMode != VansRetargetTranslationScaleMode::AutoPelvis
		&& desc.translationScaleMode != VansRetargetTranslationScaleMode::CompatibleSkeleton
		&& desc.translationScaleMode != VansRetargetTranslationScaleMode::Explicit)
		return false;
	if (desc.rootAlignment != VansRetargetRootAlignment::None
		&& desc.rootAlignment != VansRetargetRootAlignment::FeetToOwner)
		return false;
	if (desc.targetModelSpaceAlignment != VansRetargetModelSpaceAlignment::None
		&& desc.targetModelSpaceAlignment != VansRetargetModelSpaceAlignment::SourceBindPose)
		return false;
	m_SourceSkeleton = &sourceSkeleton;
	m_TargetSkeleton = &targetSkeleton;
	m_TargetRig = targetRig;
	m_TargetRig.skeleton = &targetSkeleton;

	if (desc.translationScaleMode == VansRetargetTranslationScaleMode::Explicit)
	{
		if (!std::isfinite(desc.translationScale) || desc.translationScale <= 0.0f)
			return false;
		m_Stats.translationScale = desc.translationScale;
	}
	else if (desc.translationScaleMode == VansRetargetTranslationScaleMode::CompatibleSkeleton)
	{
		m_Stats.translationScale = 1.0f;
	}
	else
	{
		const int sourcePelvis = FindBone(sourceSkeleton, "pelvis");
		const int targetPelvis = FindBone(targetSkeleton, "pelvis");
		if (sourcePelvis >= 0 && targetPelvis >= 0)
		{
			const glm::vec3 sourcePelvisTranslation = glm::vec3(sourceSkeleton.bones[sourcePelvis].localTransform[3]);
			const glm::vec3 targetPelvisTranslation = glm::vec3(targetSkeleton.bones[targetPelvis].localTransform[3]);
			m_Stats.translationScale = SafeLengthRatio(targetPelvisTranslation, sourcePelvisTranslation);
		}
		else
		{
			m_Stats.translationScale = 1.0f;
		}
	}

	for (size_t targetIndex = 0; targetIndex < targetSkeleton.bones.size(); ++targetIndex)
	{
		const BoneInfo& targetBone = targetSkeleton.bones[targetIndex];
		auto sourceIt = sourceSkeleton.boneNameToIndex.find(targetBone.name);
		if (sourceIt == sourceSkeleton.boneNameToIndex.end())
			continue;

		BoneMapEntry entry;
		entry.sourceIndex = sourceIt->second;
		entry.targetIndex = static_cast<int>(targetIndex);
		entry.copyTranslationDelta =
			targetBone.name == "root" ||
			targetBone.name == "pelvis" ||
			targetBone.parentIndex < 0;
		m_BoneMap.push_back(entry);
	}

	m_Stats.mappedBoneCount = static_cast<uint32_t>(m_BoneMap.size());
	m_Stats.unmappedTargetBoneCount =
		m_Stats.targetBoneCount > m_Stats.mappedBoneCount
			? m_Stats.targetBoneCount - m_Stats.mappedBoneCount
			: 0;

	std::unordered_set<std::string> limbNames;
	std::unordered_set<std::string> targetChainIds;
	for (const VansRetargetLimbChainDesc& chainDesc : desc.limbChains)
	{
		CompiledLimbChain chain;
		chain.name = chainDesc.name;
		chain.sourceRoot = FindBone(sourceSkeleton, chainDesc.sourceRoot.c_str());
		chain.sourceMid = FindBone(sourceSkeleton, chainDesc.sourceMid.c_str());
		chain.sourceTip = FindBone(sourceSkeleton, chainDesc.sourceTip.c_str());
		chain.targetChainIndex = m_TargetRig.FindChain(chainDesc.targetChainId);
		chain.positionWeight = glm::clamp(chainDesc.positionWeight, 0.0f, 1.0f);
		const bool sourceChainValid =
			chain.sourceRoot >= 0 && chain.sourceMid >= 0 && chain.sourceTip >= 0 &&
			sourceSkeleton.bones[chain.sourceMid].parentIndex == chain.sourceRoot &&
			sourceSkeleton.bones[chain.sourceTip].parentIndex == chain.sourceMid;
		const bool targetChainValid = chain.targetChainIndex >= 0
			&& m_TargetRig.chains[static_cast<std::size_t>(chain.targetChainIndex)].solver == VansRigSolverKind::Limb;
		if (chainDesc.name.empty() || chainDesc.targetChainId.empty()
			|| !limbNames.insert(chainDesc.name).second
			|| !targetChainIds.insert(chainDesc.targetChainId).second
			|| !sourceChainValid || !targetChainValid
			|| !std::isfinite(chainDesc.positionWeight)
			|| chainDesc.positionWeight <= 0.0f || chainDesc.positionWeight > 1.0f)
			return false;
		m_LimbChains.push_back(std::move(chain));
	}
	m_Stats.limbChainCount = static_cast<uint32_t>(m_LimbChains.size());

	m_Valid = !m_BoneMap.empty();
	if (m_Valid)
	{
		m_SourceBindModelTransforms = BuildBindModelTransforms(sourceSkeleton);
		m_TargetBindModelTransforms = BuildBindModelTransforms(targetSkeleton);
		m_SourceLocalScratch.reserve(sourceSkeleton.bones.size());
		m_TargetLocalScratch.reserve(targetSkeleton.bones.size());
		m_DesiredModelRotationsScratch.reserve(targetSkeleton.bones.size());
		m_MappedTargetBonesScratch.reserve(targetSkeleton.bones.size());
		m_ResolvedModelScratch.reserve(targetSkeleton.bones.size());
		m_LocalPoseScratch.reserve(targetSkeleton.bones.size());
	}

	if (m_Valid
		&& desc.targetModelSpaceAlignment == VansRetargetModelSpaceAlignment::SourceBindPose)
	{
		glm::mat3 sourceBasis(1.0f);
		glm::mat3 targetBasis(1.0f);
		if (TryBuildHumanoidBasis(sourceSkeleton, m_SourceBindModelTransforms, sourceBasis) &&
		    TryBuildHumanoidBasis(targetSkeleton, m_TargetBindModelTransforms, targetBasis))
		{
			m_TargetModelSpaceCorrection = Mat4FromMat3(sourceBasis * glm::inverse(targetBasis));
			m_HasTargetModelSpaceCorrection = true;
		}
	}
	if (m_Valid && desc.rootAlignment == VansRetargetRootAlignment::FeetToOwner)
	{
		const int footL = FindBone(targetSkeleton, "foot_l");
		const int footR = FindBone(targetSkeleton, "foot_r");
		if (footL < 0 || footR < 0)
		{
			m_Valid = false;
			return false;
		}

		// feetToOwner 是目标骨架 Bind Pose 到实体原点的一次性校准。
		// 不能按当前动画帧重新居中，否则会吞掉步态/Root Motion 的水平位移，
		// 并与 Grounding 的世界空间 Plant Lock 形成可见的脚部滞后。
		const glm::mat4 modelCorrection = m_HasTargetModelSpaceCorrection
			? m_TargetModelSpaceCorrection : glm::mat4(1.0f);
		const glm::vec3 footLModel = glm::vec3(modelCorrection
			* m_TargetBindModelTransforms[static_cast<std::size_t>(footL)]
			* glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		const glm::vec3 footRModel = glm::vec3(modelCorrection
			* m_TargetBindModelTransforms[static_cast<std::size_t>(footR)]
			* glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		const glm::vec3 footCenter = (footLModel + footRModel) * 0.5f;
		const glm::vec3 bindOffset(
			-footCenter.x,
			-std::min(footLModel.y, footRModel.y),
			-footCenter.z);
		if (!std::isfinite(bindOffset.x) || !std::isfinite(bindOffset.y)
			|| !std::isfinite(bindOffset.z))
		{
			m_Valid = false;
			return false;
		}
		m_RootAlignmentCorrection = glm::translate(glm::mat4(1.0f), bindOffset);
		m_HasRootAlignmentCorrection = glm::length(bindOffset) > 1.0e-5f;
	}
	return m_Valid;
}

bool VansRetargetProcessor::Process(
	const std::vector<glm::mat4>& sourceModelTransforms,
	const Skeleton& sourceSkeleton,
	const Skeleton& targetSkeleton,
	std::vector<glm::mat4>& outTargetModelTransforms) const
{
	if (!m_Valid || m_SourceSkeleton != &sourceSkeleton || m_TargetSkeleton != &targetSkeleton ||
	    sourceModelTransforms.size() != sourceSkeleton.bones.size() ||
	    targetSkeleton.bones.empty())
	{
		return false;
	}

	BuildLocalFromModel(sourceModelTransforms, sourceSkeleton, m_SourceLocalScratch);
	std::vector<glm::mat4>& sourceLocalTransforms = m_SourceLocalScratch;
	std::vector<glm::mat4>& targetLocalTransforms = m_TargetLocalScratch;
	targetLocalTransforms.assign(targetSkeleton.bones.size(), glm::mat4(1.0f));

	for (size_t targetIndex = 0; targetIndex < targetSkeleton.bones.size(); ++targetIndex)
		targetLocalTransforms[targetIndex] = targetSkeleton.bones[targetIndex].localTransform;

	// 平移通道必须先在组件/模型空间对齐，再换算回目标父骨骼局部空间。
	// 直接复制 Source local delta 只对父层级缩放完全一致的骨架成立；例如
	// SWAT 的 motion root 位于 100x Armature 下，直接相加会把 Root Motion 放大 100 倍。
	const auto resolveTargetLocalTranslationDelta = [&]
	(
		const BoneMapEntry& entry,
		const std::vector<glm::mat4>& resolvedTargetModels,
		const glm::quat& sourceToTargetModelRotation,
		glm::vec3& outDelta
	)
	{
		if (entry.sourceIndex < 0 ||
			entry.sourceIndex >= static_cast<int>(sourceLocalTransforms.size()) ||
			entry.targetIndex < 0 ||
			entry.targetIndex >= static_cast<int>(targetSkeleton.bones.size()))
		{
			return false;
		}

		glm::vec3 sourceTranslation;
		glm::quat sourceRotation;
		glm::vec3 sourceScale;
		glm::vec3 sourceBindTranslation;
		glm::quat sourceBindRotation;
		glm::vec3 sourceBindScale;
		if (!DecomposeTransform(sourceLocalTransforms[entry.sourceIndex],
			sourceTranslation, sourceRotation, sourceScale) ||
			!DecomposeTransform(sourceSkeleton.bones[entry.sourceIndex].localTransform,
				sourceBindTranslation, sourceBindRotation, sourceBindScale))
		{
			return false;
		}

		glm::vec3 sourceModelDelta = sourceTranslation - sourceBindTranslation;
		const int sourceParent = sourceSkeleton.bones[entry.sourceIndex].parentIndex;
		if (sourceParent >= 0 &&
			sourceParent < static_cast<int>(sourceModelTransforms.size()))
		{
			sourceModelDelta = glm::mat3(sourceModelTransforms[sourceParent]) * sourceModelDelta;
		}

		const glm::vec3 targetModelDelta =
			sourceToTargetModelRotation * sourceModelDelta * m_Stats.translationScale;
		const int targetParent = targetSkeleton.bones[entry.targetIndex].parentIndex;
		if (targetParent < 0)
		{
			outDelta = targetModelDelta;
			return true;
		}
		if (targetParent >= static_cast<int>(resolvedTargetModels.size()))
			return false;

		const glm::mat3 targetParentLinear(resolvedTargetModels[targetParent]);
		const float determinant = glm::determinant(targetParentLinear);
		if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-8f)
			return false;
		outDelta = glm::inverse(targetParentLinear) * targetModelDelta;
		return std::isfinite(outDelta.x) && std::isfinite(outDelta.y) &&
			std::isfinite(outDelta.z);
	};

	auto applyConfiguredLimbChains = [&](const glm::quat& sourceToTargetRotation)
	{
		if (m_LimbChains.empty())
			return true;

		m_LocalPoseScratch.resize(targetLocalTransforms.size());
		for (std::size_t index = 0; index < targetLocalTransforms.size(); ++index)
			if (!VansPoseMath::TryDecompose(targetLocalTransforms[index], m_LocalPoseScratch[index]))
				return false;
		VansPoseWorkspace& workspace = m_PoseWorkspaceScratch;
		if (!workspace.Initialize(targetSkeleton, m_LocalPoseScratch))
			return false;
		for (const CompiledLimbChain& compiled : m_LimbChains)
		{
			const VansCompiledRigChain& targetChain =
				m_TargetRig.chains[static_cast<std::size_t>(compiled.targetChainIndex)];
			const int targetRootIndex = targetChain.boneIndices[0];
			const int targetMidIndex = targetChain.boneIndices[1];
			const int targetTipIndex = targetChain.boneIndices[2];
			const glm::vec3 sourceRoot = ExtractTranslation(sourceModelTransforms[compiled.sourceRoot]);
			const glm::vec3 sourceMid = ExtractTranslation(sourceModelTransforms[compiled.sourceMid]);
			const glm::vec3 sourceTip = ExtractTranslation(sourceModelTransforms[compiled.sourceTip]);
			const float sourceUpperLength = glm::distance(sourceRoot, sourceMid);
			const float sourceLowerLength = glm::distance(sourceMid, sourceTip);
			const float sourceReach = sourceUpperLength + sourceLowerLength;

			const glm::vec3 targetRoot = workspace.GetComponentPosition(targetRootIndex);
			const glm::vec3 targetMid = workspace.GetComponentPosition(targetMidIndex);
			const glm::vec3 targetTip = workspace.GetComponentPosition(targetTipIndex);
			// Retarget goals and the Limb solver both operate in component/model space.
			// Measure the current chain there as well so inherited skeleton scale (for
			// example SWAT's 100x FBX root scale) cannot collapse the hand goal.
			const float targetReach = glm::distance(targetRoot, targetMid)
				+ glm::distance(targetMid, targetTip);
			if (sourceReach <= 0.0001f || targetReach <= 0.0001f)
				continue;

			const glm::vec3 sourceRootToTip = sourceTip - sourceRoot;
			const glm::vec3 currentTargetDirection = NormalizeOrFallback(
				targetTip - targetRoot, glm::vec3(1.0f, 0.0f, 0.0f));
			const glm::vec3 targetDirection = NormalizeOrFallback(
				sourceToTargetRotation * sourceRootToTip, currentTargetDirection);
			const float bendRatio = glm::clamp(
				glm::length(sourceRootToTip) / sourceReach, 0.0f, 1.0f);

			VansProceduralGoal target;
			target.positionModel = targetRoot + targetDirection * targetReach * bendRatio;
			target.positionWeight = compiled.positionWeight;
			target.valid = true;
			VansLimbIKSettings settings;
			settings.tipRotationMode = VansLimbTipRotationMode::PreserveInput;
			settings.positionTolerance = 0.001f;
			settings.weight = 1.0f;
			settings.commitClampedPose = true;
			const VansProceduralSolverResult result = VansLimbIKSolver::Solve(
				workspace, m_TargetRig, targetChain, target, settings);
			if (result.status == VansProceduralSolverStatus::InvalidInput)
				return false;
		}
		const auto& solved = workspace.GetLocalPose();
		for (std::size_t index = 0; index < solved.size(); ++index)
			targetLocalTransforms[index] = VansPoseMath::Compose(solved[index]);
		return true;
	};

	if (m_HasTargetModelSpaceCorrection &&
	    m_SourceBindModelTransforms.size() == sourceSkeleton.bones.size() &&
	    m_TargetBindModelTransforms.size() == targetSkeleton.bones.size())
	{
		glm::quat targetToSourceRotation(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 correctionTranslation;
		glm::vec3 correctionScale;
		DecomposeTransform(m_TargetModelSpaceCorrection,
			correctionTranslation, targetToSourceRotation, correctionScale);

		std::vector<glm::quat>& desiredModelRotations = m_DesiredModelRotationsScratch;
		desiredModelRotations.assign(targetSkeleton.bones.size(),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		std::vector<bool>& mappedTargetBones = m_MappedTargetBonesScratch;
		mappedTargetBones.assign(targetSkeleton.bones.size(), false);
		for (size_t targetIndex = 0; targetIndex < targetSkeleton.bones.size(); ++targetIndex)
		{
			glm::vec3 translation;
			glm::vec3 scale;
			DecomposeTransform(m_TargetBindModelTransforms[targetIndex],
				translation, desiredModelRotations[targetIndex], scale);
		}

		for (const BoneMapEntry& entry : m_BoneMap)
		{
			if (entry.sourceIndex < 0 ||
			    entry.sourceIndex >= static_cast<int>(sourceModelTransforms.size()) ||
			    entry.targetIndex < 0 ||
			    entry.targetIndex >= static_cast<int>(targetLocalTransforms.size()))
			{
				continue;
			}

			glm::vec3 sourceModelTranslation;
			glm::quat sourceModelRotation;
			glm::vec3 sourceModelScale;
			if (!DecomposeTransform(sourceModelTransforms[entry.sourceIndex],
				sourceModelTranslation, sourceModelRotation, sourceModelScale))
			{
				continue;
			}

			glm::vec3 sourceBindModelTranslation;
			glm::quat sourceBindModelRotation;
			glm::vec3 sourceBindModelScale;
			DecomposeTransform(m_SourceBindModelTransforms[entry.sourceIndex],
				sourceBindModelTranslation, sourceBindModelRotation, sourceBindModelScale);

			glm::vec3 targetBindModelTranslation;
			glm::quat targetBindModelRotation;
			glm::vec3 targetBindModelScale;
			DecomposeTransform(m_TargetBindModelTransforms[entry.targetIndex],
				targetBindModelTranslation, targetBindModelRotation, targetBindModelScale);

			const glm::quat sourceDeltaRotation =
				sourceModelRotation * glm::inverse(sourceBindModelRotation);
			const glm::quat targetSpaceDeltaRotation =
				glm::inverse(targetToSourceRotation) * sourceDeltaRotation * targetToSourceRotation;
			desiredModelRotations[entry.targetIndex] =
				glm::normalize(targetSpaceDeltaRotation * targetBindModelRotation);
			mappedTargetBones[entry.targetIndex] = true;
		}

		std::vector<glm::mat4>& resolvedModelTransforms = m_ResolvedModelScratch;
		resolvedModelTransforms.assign(targetSkeleton.bones.size(), glm::mat4(1.0f));
		const auto applyModelSpaceRotation = [&](int targetIndex)
		{
			if (targetIndex < 0 || targetIndex >= static_cast<int>(targetSkeleton.bones.size()))
				return;

			const BoneInfo& targetBone = targetSkeleton.bones[targetIndex];
			const int parentIndex = targetBone.parentIndex;
			if (mappedTargetBones[targetIndex])
			{
				glm::vec3 targetBindTranslation;
				glm::quat targetBindRotation;
				glm::vec3 targetBindScale;
				DecomposeTransform(targetBone.localTransform,
					targetBindTranslation, targetBindRotation, targetBindScale);

				glm::quat targetLocalRotation = desiredModelRotations[targetIndex];
				if (parentIndex >= 0 &&
				    parentIndex < static_cast<int>(resolvedModelTransforms.size()))
				{
					glm::vec3 parentTranslation;
					glm::quat parentModelRotation;
					glm::vec3 parentScale;
					DecomposeTransform(resolvedModelTransforms[parentIndex],
						parentTranslation, parentModelRotation, parentScale);
					targetLocalRotation =
						glm::inverse(parentModelRotation) * desiredModelRotations[targetIndex];
				}

				targetLocalTransforms[targetIndex] =
					ComposeTransform(targetBindTranslation, targetLocalRotation, targetBindScale);
			}

			resolvedModelTransforms[targetIndex] = targetLocalTransforms[targetIndex];
			if (parentIndex >= 0 &&
			    parentIndex < static_cast<int>(resolvedModelTransforms.size()))
			{
				resolvedModelTransforms[targetIndex] =
					resolvedModelTransforms[parentIndex] * targetLocalTransforms[targetIndex];
			}
		};

		if (!targetSkeleton.topologicalOrder.empty())
		{
			for (int targetIndex : targetSkeleton.topologicalOrder)
				applyModelSpaceRotation(targetIndex);
		}
		else
		{
			for (int targetIndex = 0; targetIndex < static_cast<int>(targetSkeleton.bones.size()); ++targetIndex)
				applyModelSpaceRotation(targetIndex);
		}

		for (const BoneMapEntry& entry : m_BoneMap)
		{
			if (!entry.copyTranslationDelta ||
			    entry.sourceIndex < 0 ||
			    entry.sourceIndex >= static_cast<int>(sourceLocalTransforms.size()) ||
			    entry.targetIndex < 0 ||
			    entry.targetIndex >= static_cast<int>(targetLocalTransforms.size()))
			{
				continue;
			}

			glm::vec3 targetTranslation;
			glm::quat targetRotation;
			glm::vec3 targetScale;
			DecomposeTransform(targetLocalTransforms[entry.targetIndex],
				targetTranslation, targetRotation, targetScale);

			glm::vec3 targetLocalDelta(0.0f);
			if (!resolveTargetLocalTranslationDelta(
				entry,
				resolvedModelTransforms,
				glm::inverse(targetToSourceRotation),
				targetLocalDelta))
			{
				continue;
			}
			targetTranslation += targetLocalDelta;
			targetLocalTransforms[entry.targetIndex] =
				ComposeTransform(targetTranslation, targetRotation, targetScale);
		}

		if (!applyConfiguredLimbChains(glm::inverse(targetToSourceRotation)))
			return false;

		BuildModelFromLocal(targetLocalTransforms, targetSkeleton, outTargetModelTransforms);
		if (m_HasTargetModelSpaceCorrection || m_HasRootAlignmentCorrection)
		{
			for (glm::mat4& transform : outTargetModelTransforms)
			{
				if (m_HasTargetModelSpaceCorrection)
					transform = m_TargetModelSpaceCorrection * transform;
				if (m_HasRootAlignmentCorrection)
					transform = m_RootAlignmentCorrection * transform;
			}
		}
		return outTargetModelTransforms.size() == targetSkeleton.bones.size();
	}

	for (const BoneMapEntry& entry : m_BoneMap)
	{
		if (entry.sourceIndex < 0 ||
		    entry.sourceIndex >= static_cast<int>(sourceLocalTransforms.size()) ||
		    entry.targetIndex < 0 ||
		    entry.targetIndex >= static_cast<int>(targetLocalTransforms.size()))
		{
			continue;
		}

		glm::vec3 sourceTranslation;
		glm::quat sourceRotation;
		glm::vec3 sourceScale;
		if (!DecomposeTransform(sourceLocalTransforms[entry.sourceIndex],
			sourceTranslation, sourceRotation, sourceScale))
		{
			continue;
		}

		glm::vec3 sourceBindTranslation;
		glm::quat sourceBindRotation;
		glm::vec3 sourceBindScale;
		DecomposeTransform(sourceSkeleton.bones[entry.sourceIndex].localTransform,
			sourceBindTranslation, sourceBindRotation, sourceBindScale);

		glm::vec3 targetBindTranslation;
		glm::quat targetBindRotation;
		glm::vec3 targetBindScale;
		DecomposeTransform(targetSkeleton.bones[entry.targetIndex].localTransform,
			targetBindTranslation, targetBindRotation, targetBindScale);

		const glm::quat sourceDeltaRotation = sourceRotation * glm::inverse(sourceBindRotation);
		const glm::quat targetRotation = glm::normalize(sourceDeltaRotation * targetBindRotation);
		targetLocalTransforms[entry.targetIndex] =
			ComposeTransform(targetBindTranslation, targetRotation, targetBindScale);
	}

	std::vector<glm::mat4>& resolvedModelTransforms = m_ResolvedModelScratch;
	BuildModelFromLocal(targetLocalTransforms, targetSkeleton, resolvedModelTransforms);
	for (const BoneMapEntry& entry : m_BoneMap)
	{
		if (!entry.copyTranslationDelta ||
			entry.targetIndex < 0 ||
			entry.targetIndex >= static_cast<int>(targetLocalTransforms.size()))
		{
			continue;
		}

		glm::vec3 targetTranslation;
		glm::quat targetRotation;
		glm::vec3 targetScale;
		if (!DecomposeTransform(targetLocalTransforms[entry.targetIndex],
			targetTranslation, targetRotation, targetScale))
		{
			continue;
		}
		glm::vec3 targetLocalDelta(0.0f);
		if (!resolveTargetLocalTranslationDelta(
			entry,
			resolvedModelTransforms,
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			targetLocalDelta))
		{
			continue;
		}
		targetLocalTransforms[entry.targetIndex] = ComposeTransform(
			targetTranslation + targetLocalDelta, targetRotation, targetScale);
	}

	if (!applyConfiguredLimbChains(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)))
		return false;

	BuildModelFromLocal(targetLocalTransforms, targetSkeleton, outTargetModelTransforms);
	if (m_HasTargetModelSpaceCorrection || m_HasRootAlignmentCorrection)
	{
		for (glm::mat4& transform : outTargetModelTransforms)
		{
			if (m_HasTargetModelSpaceCorrection)
				transform = m_TargetModelSpaceCorrection * transform;
			if (m_HasRootAlignmentCorrection)
				transform = m_RootAlignmentCorrection * transform;
		}
	}
	return outTargetModelTransforms.size() == targetSkeleton.bones.size();
}

glm::mat4 VansRetargetProcessor::ComposeTransform(
	const glm::vec3& translation,
	const glm::quat& rotation,
	const glm::vec3& scale)
{
	return glm::translate(glm::mat4(1.0f), translation) *
		glm::toMat4(glm::normalize(rotation)) *
		glm::scale(glm::mat4(1.0f), scale);
}

bool VansRetargetProcessor::DecomposeTransform(
	const glm::mat4& transform,
	glm::vec3& translation,
	glm::quat& rotation,
	glm::vec3& scale)
{
	translation = glm::vec3(transform[3]);

	scale.x = glm::length(glm::vec3(transform[0]));
	scale.y = glm::length(glm::vec3(transform[1]));
	scale.z = glm::length(glm::vec3(transform[2]));

	if (scale.x <= 0.0001f || scale.y <= 0.0001f || scale.z <= 0.0001f)
	{
		rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		scale = glm::vec3(1.0f);
		return false;
	}

	glm::mat3 rotationMatrix;
	rotationMatrix[0] = glm::vec3(transform[0]) / scale.x;
	rotationMatrix[1] = glm::vec3(transform[1]) / scale.y;
	rotationMatrix[2] = glm::vec3(transform[2]) / scale.z;
	rotation = glm::normalize(glm::quat_cast(rotationMatrix));
	return true;
}

void VansRetargetProcessor::BuildLocalFromModel(
	const std::vector<glm::mat4>& modelTransforms,
	const Skeleton& skeleton,
	std::vector<glm::mat4>& outLocalTransforms)
{
	outLocalTransforms.assign(modelTransforms.size(), glm::mat4(1.0f));
	for (size_t index = 0; index < modelTransforms.size(); ++index)
	{
		const int parentIndex = skeleton.bones[index].parentIndex;
		if (parentIndex >= 0 && parentIndex < static_cast<int>(modelTransforms.size()))
			outLocalTransforms[index] = glm::inverse(modelTransforms[parentIndex]) * modelTransforms[index];
		else
			outLocalTransforms[index] = modelTransforms[index];
	}
}

void VansRetargetProcessor::BuildModelFromLocal(
	const std::vector<glm::mat4>& localTransforms,
	const Skeleton& skeleton,
	std::vector<glm::mat4>& outModelTransforms)
{
	const uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());
	outModelTransforms.assign(boneCount, glm::mat4(1.0f));
	for (uint32_t index = 0; index < boneCount; ++index)
		outModelTransforms[index] = localTransforms[index];

	for (int index : skeleton.topologicalOrder)
	{
		const int parentIndex = skeleton.bones[static_cast<std::size_t>(index)].parentIndex;
		if (parentIndex >= 0 && parentIndex < static_cast<int>(boneCount))
			outModelTransforms[index] = outModelTransforms[parentIndex] * outModelTransforms[index];
	}
}

std::vector<glm::mat4> VansRetargetProcessor::BuildBindModelTransforms(const Skeleton& skeleton)
{
	std::vector<glm::mat4> localTransforms(skeleton.bones.size(), glm::mat4(1.0f));
	for (size_t index = 0; index < skeleton.bones.size(); ++index)
		localTransforms[index] = skeleton.bones[index].localTransform;

	std::vector<glm::mat4> modelTransforms;
	BuildModelFromLocal(localTransforms, skeleton, modelTransforms);
	return modelTransforms;
}

bool VansRetargetProcessor::TryBuildHumanoidBasis(
	const Skeleton& skeleton,
	const std::vector<glm::mat4>& modelTransforms,
	glm::mat3& outBasis)
{
	const int pelvis = FindBone(skeleton, "pelvis");
	const int head = FindBone(skeleton, "head");
	const int handL = FindBone(skeleton, "hand_l");
	const int handR = FindBone(skeleton, "hand_r");
	const int footL = FindBone(skeleton, "foot_l");
	const int footR = FindBone(skeleton, "foot_r");

	if (pelvis < 0 || head < 0 ||
	    pelvis >= static_cast<int>(modelTransforms.size()) ||
	    head >= static_cast<int>(modelTransforms.size()))
	{
		return false;
	}

	const glm::vec3 pelvisPosition = ExtractTranslation(modelTransforms[pelvis]);
	const glm::vec3 headPosition = ExtractTranslation(modelTransforms[head]);
	glm::vec3 up = NormalizeOrFallback(headPosition - pelvisPosition, glm::vec3(0.0f, 1.0f, 0.0f));

	glm::vec3 right(0.0f);
	if (handL >= 0 && handR >= 0 &&
	    handL < static_cast<int>(modelTransforms.size()) &&
	    handR < static_cast<int>(modelTransforms.size()))
	{
		right = ExtractTranslation(modelTransforms[handR]) - ExtractTranslation(modelTransforms[handL]);
	}
	if (glm::length(right) <= 1.0e-5f &&
	    footL >= 0 && footR >= 0 &&
	    footL < static_cast<int>(modelTransforms.size()) &&
	    footR < static_cast<int>(modelTransforms.size()))
	{
		right = ExtractTranslation(modelTransforms[footR]) - ExtractTranslation(modelTransforms[footL]);
	}
	if (glm::length(right) <= 1.0e-5f)
		return false;

	right = NormalizeOrFallback(right, glm::vec3(1.0f, 0.0f, 0.0f));
	right = NormalizeOrFallback(right - up * glm::dot(right, up), glm::vec3(1.0f, 0.0f, 0.0f));
	glm::vec3 forward = NormalizeOrFallback(glm::cross(right, up), glm::vec3(0.0f, 0.0f, 1.0f));
	right = NormalizeOrFallback(glm::cross(up, forward), right);

	outBasis = glm::mat3(1.0f);
	outBasis[0] = right;
	outBasis[1] = up;
	outBasis[2] = forward;
	return true;
}

int VansRetargetProcessor::FindBone(const Skeleton& skeleton, const char* name)
{
	auto it = skeleton.boneNameToIndex.find(name);
	return it != skeleton.boneNameToIndex.end() ? it->second : -1;
}
