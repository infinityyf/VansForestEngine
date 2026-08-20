#include "VansRetargetProcessor.h"
#include "../IK/VansTwoBoneIKSolver.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

using namespace VansGraphics;

namespace
{
	std::string ToLowerAscii(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	float SafeLengthRatio(const glm::vec3& target, const glm::vec3& source)
	{
		const float sourceLen = glm::length(source);
		const float targetLen = glm::length(target);
		if (sourceLen <= 0.0001f || targetLen <= 0.0001f)
			return 1.0f;
		return targetLen / sourceLen;
	}

	bool IsIdentityScaleMode(const std::string& mode)
	{
		return mode == "identity" ||
		       mode == "none" ||
		       mode == "one" ||
		       mode == "1" ||
		       mode == "source_units" ||
		       mode == "compatible_skeleton";
	}

	bool IsSourceBindPoseAlignmentMode(const std::string& mode)
	{
		const std::string normalized = ToLowerAscii(mode);
		return normalized == "source_bind_pose" ||
		       normalized == "sourcebindpose" ||
		       normalized == "humanoid_bind_pose" ||
		       normalized == "humanoidbindpose" ||
		       normalized == "match_source_bind_pose" ||
		       normalized == "matchsourcebindpose";
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
}

bool VansRetargetProcessor::Build(
	const Skeleton& sourceSkeleton,
	const Skeleton& targetSkeleton,
	const VansRetargetRuntimeDesc& desc)
{
	m_BoneMap.clear();
	m_TwoBoneChains.clear();
	m_Stats = {};
	m_Stats.sourceBoneCount = static_cast<uint32_t>(sourceSkeleton.bones.size());
	m_Stats.targetBoneCount = static_cast<uint32_t>(targetSkeleton.bones.size());
	m_SourceBindModelTransforms.clear();
	m_TargetBindModelTransforms.clear();
	m_TargetModelSpaceCorrection = glm::mat4(1.0f);
	m_HasTargetModelSpaceCorrection = false;
	m_Valid = false;

	if (sourceSkeleton.bones.empty() || targetSkeleton.bones.empty())
		return false;

	if (desc.hasExplicitTranslationScale)
	{
		m_Stats.translationScale = desc.translationScale;
	}
	else if (IsIdentityScaleMode(desc.translationScaleMode))
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

	for (const VansRetargetTwoBoneChainDesc& chainDesc : desc.twoBoneChains)
	{
		CompiledTwoBoneChain chain;
		chain.name = chainDesc.name;
		chain.sourceRoot = FindBone(sourceSkeleton, chainDesc.sourceRoot.c_str());
		chain.sourceMid = FindBone(sourceSkeleton, chainDesc.sourceMid.c_str());
		chain.sourceTip = FindBone(sourceSkeleton, chainDesc.sourceTip.c_str());
		chain.targetRoot = FindBone(targetSkeleton, chainDesc.targetRoot.c_str());
		chain.targetMid = FindBone(targetSkeleton, chainDesc.targetMid.c_str());
		chain.targetTip = FindBone(targetSkeleton, chainDesc.targetTip.c_str());
		chain.positionWeight = glm::clamp(chainDesc.positionWeight, 0.0f, 1.0f);
		const bool sourceChainValid =
			chain.sourceRoot >= 0 && chain.sourceMid >= 0 && chain.sourceTip >= 0 &&
			sourceSkeleton.bones[chain.sourceMid].parentIndex == chain.sourceRoot &&
			sourceSkeleton.bones[chain.sourceTip].parentIndex == chain.sourceMid;
		const bool targetChainValid =
			chain.targetRoot >= 0 && chain.targetMid >= 0 && chain.targetTip >= 0 &&
			targetSkeleton.bones[chain.targetMid].parentIndex == chain.targetRoot &&
			targetSkeleton.bones[chain.targetTip].parentIndex == chain.targetMid;
		if (sourceChainValid && targetChainValid && chain.positionWeight > 0.0f)
			m_TwoBoneChains.push_back(std::move(chain));
	}
	m_Stats.twoBoneChainCount = static_cast<uint32_t>(m_TwoBoneChains.size());

	m_Valid = !m_BoneMap.empty();
	if (m_Valid)
	{
		m_SourceBindModelTransforms = BuildBindModelTransforms(sourceSkeleton);
		m_TargetBindModelTransforms = BuildBindModelTransforms(targetSkeleton);
	}

	if (m_Valid && IsSourceBindPoseAlignmentMode(desc.targetModelSpaceAlignmentMode))
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
	return m_Valid;
}

bool VansRetargetProcessor::Process(
	const std::vector<glm::mat4>& sourceModelTransforms,
	const Skeleton& sourceSkeleton,
	const Skeleton& targetSkeleton,
	std::vector<glm::mat4>& outTargetModelTransforms) const
{
	if (!m_Valid ||
	    sourceModelTransforms.size() != sourceSkeleton.bones.size() ||
	    targetSkeleton.bones.empty())
	{
		return false;
	}

	std::vector<glm::mat4> sourceLocalTransforms =
		BuildLocalFromModel(sourceModelTransforms, sourceSkeleton);
	std::vector<glm::mat4> targetLocalTransforms(targetSkeleton.bones.size(), glm::mat4(1.0f));

	for (size_t targetIndex = 0; targetIndex < targetSkeleton.bones.size(); ++targetIndex)
		targetLocalTransforms[targetIndex] = targetSkeleton.bones[targetIndex].localTransform;

	auto applyConfiguredTwoBoneChains = [&](const glm::quat& sourceToTargetRotation)
	{
		if (m_TwoBoneChains.empty())
			return;

		std::vector<glm::mat4> targetModelTransforms;
		BuildModelFromLocal(targetLocalTransforms, targetSkeleton, targetModelTransforms);
		for (const CompiledTwoBoneChain& compiled : m_TwoBoneChains)
		{
			const glm::vec3 sourceRoot = ExtractTranslation(sourceModelTransforms[compiled.sourceRoot]);
			const glm::vec3 sourceMid = ExtractTranslation(sourceModelTransforms[compiled.sourceMid]);
			const glm::vec3 sourceTip = ExtractTranslation(sourceModelTransforms[compiled.sourceTip]);
			const float sourceUpperLength = glm::distance(sourceRoot, sourceMid);
			const float sourceLowerLength = glm::distance(sourceMid, sourceTip);
			const float sourceReach = sourceUpperLength + sourceLowerLength;

			const glm::vec3 targetRoot = ExtractTranslation(targetModelTransforms[compiled.targetRoot]);
			const glm::vec3 targetMid = ExtractTranslation(targetModelTransforms[compiled.targetMid]);
			const glm::vec3 targetTip = ExtractTranslation(targetModelTransforms[compiled.targetTip]);
			const float targetUpperLength = glm::distance(targetRoot, targetMid);
			const float targetLowerLength = glm::distance(targetMid, targetTip);
			const float targetReach = targetUpperLength + targetLowerLength;
			if (sourceReach <= 0.0001f || targetReach <= 0.0001f)
				continue;

			const glm::vec3 sourceRootToTip = sourceTip - sourceRoot;
			const glm::vec3 currentTargetDirection = NormalizeOrFallback(
				targetTip - targetRoot, glm::vec3(1.0f, 0.0f, 0.0f));
			const glm::vec3 targetDirection = NormalizeOrFallback(
				sourceToTargetRotation * sourceRootToTip, currentTargetDirection);
			const float bendRatio = glm::clamp(
				glm::length(sourceRootToTip) / sourceReach, 0.0f, 1.0f);

			IKChainDefinition chain;
			chain.chainName = compiled.name;
			chain.solverType = IKSolverType::TwoBone;
			chain.profileType = IKProfileType::HumanoidArm;
			chain.positionTolerance = 0.001f;
			chain.maintainEffectorGlobalRotation = true;
			chain.bones.resize(3);
			chain.bones[0].boneIndex = compiled.targetRoot;
			chain.bones[1].boneIndex = compiled.targetMid;
			chain.bones[2].boneIndex = compiled.targetTip;
			chain.bones[2].isEffector = true;

			glm::vec3 sourcePole = sourceMid - sourceRoot;
			sourcePole -= sourceRootToTip *
				(glm::dot(sourcePole, sourceRootToTip) /
				 std::max(glm::dot(sourceRootToTip, sourceRootToTip), 0.0001f));
			if (glm::length(sourcePole) > 0.0001f)
			{
				chain.poleVector = targetRoot +
					NormalizeOrFallback(sourceToTargetRotation * sourcePole,
						glm::vec3(0.0f, 0.0f, 1.0f)) * targetReach;
				chain.poleWeight = 1.0f;
				chain.poleSpace = IKCoordinateSpace::Model;
			}

			IKTarget target;
			target.position = targetRoot + targetDirection * targetReach * bendRatio;
			target.positionWeight = compiled.positionWeight;
			target.positionSpace = IKCoordinateSpace::Model;
			IKSolveContext context;
			VansTwoBoneIKSolver solver;
			solver.Solve(targetLocalTransforms, targetModelTransforms,
				targetSkeleton, chain, target, context);
			BuildModelFromLocal(targetLocalTransforms, targetSkeleton, targetModelTransforms);
		}
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

		std::vector<glm::quat> desiredModelRotations(targetSkeleton.bones.size(),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		std::vector<bool> mappedTargetBones(targetSkeleton.bones.size(), false);
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

		std::vector<glm::mat4> resolvedModelTransforms(
			targetSkeleton.bones.size(), glm::mat4(1.0f));
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

			glm::vec3 targetTranslation;
			glm::quat targetRotation;
			glm::vec3 targetScale;
			DecomposeTransform(targetLocalTransforms[entry.targetIndex],
				targetTranslation, targetRotation, targetScale);

			targetTranslation += (sourceTranslation - sourceBindTranslation) * m_Stats.translationScale;
			targetLocalTransforms[entry.targetIndex] =
				ComposeTransform(targetTranslation, targetRotation, targetScale);
		}

		applyConfiguredTwoBoneChains(glm::inverse(targetToSourceRotation));

		BuildModelFromLocal(targetLocalTransforms, targetSkeleton, outTargetModelTransforms);
		if (m_HasTargetModelSpaceCorrection)
		{
			for (glm::mat4& transform : outTargetModelTransforms)
				transform = m_TargetModelSpaceCorrection * transform;
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

		glm::vec3 targetTranslation = targetBindTranslation;
		if (entry.copyTranslationDelta)
			targetTranslation += (sourceTranslation - sourceBindTranslation) * m_Stats.translationScale;

		const glm::quat sourceDeltaRotation = sourceRotation * glm::inverse(sourceBindRotation);
		const glm::quat targetRotation = glm::normalize(sourceDeltaRotation * targetBindRotation);
		targetLocalTransforms[entry.targetIndex] =
			ComposeTransform(targetTranslation, targetRotation, targetBindScale);
	}

	applyConfiguredTwoBoneChains(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

	BuildModelFromLocal(targetLocalTransforms, targetSkeleton, outTargetModelTransforms);
	if (m_HasTargetModelSpaceCorrection)
	{
		for (glm::mat4& transform : outTargetModelTransforms)
			transform = m_TargetModelSpaceCorrection * transform;
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

std::vector<glm::mat4> VansRetargetProcessor::BuildLocalFromModel(
	const std::vector<glm::mat4>& modelTransforms,
	const Skeleton& skeleton)
{
	std::vector<glm::mat4> localTransforms(modelTransforms.size(), glm::mat4(1.0f));
	for (size_t index = 0; index < modelTransforms.size(); ++index)
	{
		const int parentIndex = skeleton.bones[index].parentIndex;
		if (parentIndex >= 0 && parentIndex < static_cast<int>(modelTransforms.size()))
			localTransforms[index] = glm::inverse(modelTransforms[parentIndex]) * modelTransforms[index];
		else
			localTransforms[index] = modelTransforms[index];
	}
	return localTransforms;
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

	if (!skeleton.topologicalOrder.empty())
	{
		for (int index : skeleton.topologicalOrder)
		{
			if (index < 0 || index >= static_cast<int>(boneCount))
				continue;
			const int parentIndex = skeleton.bones[index].parentIndex;
			if (parentIndex >= 0 && parentIndex < static_cast<int>(boneCount))
				outModelTransforms[index] = outModelTransforms[parentIndex] * outModelTransforms[index];
		}
		return;
	}

	for (uint32_t index = 0; index < boneCount; ++index)
	{
		const int parentIndex = skeleton.bones[index].parentIndex;
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
