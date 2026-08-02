#include "VansRetargetProcessor.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <string>

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

	bool IsIdentityScaleMode(const std::string& mode)
	{
		return mode == "identity" ||
		       mode == "none" ||
		       mode == "one" ||
		       mode == "1" ||
		       mode == "source_units" ||
		       mode == "compatible_skeleton";
	}
}

bool VansRetargetProcessor::Build(
	const Skeleton& sourceSkeleton,
	const Skeleton& targetSkeleton,
	const VansRetargetRuntimeDesc& desc)
{
	m_BoneMap.clear();
	m_Stats = {};
	m_Stats.sourceBoneCount = static_cast<uint32_t>(sourceSkeleton.bones.size());
	m_Stats.targetBoneCount = static_cast<uint32_t>(targetSkeleton.bones.size());
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

	m_Valid = !m_BoneMap.empty();
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

	BuildModelFromLocal(targetLocalTransforms, targetSkeleton, outTargetModelTransforms);
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

int VansRetargetProcessor::FindBone(const Skeleton& skeleton, const char* name)
{
	auto it = skeleton.boneNameToIndex.find(name);
	return it != skeleton.boneNameToIndex.end() ? it->second : -1;
}
