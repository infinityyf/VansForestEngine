#include "VansSkinnedMeshLoader.h"
#include "VansAnimationClip.h"
#include "../AssetCore/Importers/VansAssimpSkeletonTopology.h"
#include "../AssetCore/VansAssetGuid.h"
#include "../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../Util/VansLog.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/anim.h>
#include <assimp/postprocess.h>

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>

using namespace VansGraphics;

bool VansGraphics::VansSkinnedMeshLoader::LoadSkeletonFromModelAsset(
	const std::string& modelPath,
	Skeleton& outSkeleton,
	std::string& error)
{
	error.clear();
	outSkeleton = {};
	if (modelPath.empty())
	{
		error = "Skeleton model path is empty";
		return false;
	}

	Vans::VansAssetMeta meta;
	if (!Vans::VansAssetMetaStorage::Load(
		Vans::VansAssetMeta::MetaPathFor(modelPath), meta, error))
	{
		error = "Skeleton model metadata is required: " + error;
		return false;
	}
	const Vans::VansSkeletalMeshImportSettings importSettings =
		Vans::ReadSkeletalMeshImportSettings(meta);
	if (importSettings.sourceSkeletonGuid.empty())
	{
		error = "Skeleton model metadata has no stable asset GUID";
		return false;
	}

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(modelPath,
		aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);
	if (!scene)
	{
		error = "Failed to import Skeleton model: " + std::string(importer.GetErrorString());
		return false;
	}

	ExtractSkeleton(scene, outSkeleton, 1.0f, importSettings);
	if (outSkeleton.bones.empty())
	{
		error = "Skeleton model has no bones";
		return false;
	}
	if (outSkeleton.sourceSkeletonGuid != importSettings.sourceSkeletonGuid
		|| outSkeleton.signature == 0)
	{
		error = "Skeleton model did not produce a canonical stable identity";
		outSkeleton = {};
		return false;
	}
	return true;
}

// Helper: convert Assimp mat4 -> glm::mat4 (transpose: Assimp is row-major).

static glm::mat4 ConvertMat4(const aiMatrix4x4& m)
{
	return glm::mat4(
		m.a1, m.b1, m.c1, m.d1,
		m.a2, m.b2, m.c2, m.d2,
		m.a3, m.b3, m.c3, m.d3,
		m.a4, m.b4, m.c4, m.d4
	);
}

static std::string ToLowerAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

static bool ShouldBakeSkinnedMeshNodeTransform(const Vans::VansSkeletalMeshImportSettings& importSettings)
{
	const std::string policy = ToLowerAscii(importSettings.meshNodeTransformPolicy);
	return policy == "bakeskinned" ||
		policy == "bakeskinnedmeshes" ||
		policy == "bakeall" ||
		policy == "bakenodetransform";
}

static bool IsAutoMeshNodeTransformPolicy(const Vans::VansSkeletalMeshImportSettings& importSettings)
{
	const std::string policy = ToLowerAscii(importSettings.meshNodeTransformPolicy);
	return policy.empty() ||
		policy == "auto" ||
		policy == "automatic";
}

static bool ShouldUseHierarchyBindPose(const Vans::VansSkeletalMeshImportSettings& importSettings)
{
	const std::string source = ToLowerAscii(importSettings.bindPoseSource);
	return source == "hierarchy" ||
		source == "nodehierarchy" ||
		source == "skeletonhierarchy" ||
		source == "restpose";
}

static bool ShouldRigidBindUnskinnedBoneChildren(const Vans::VansSkeletalMeshImportSettings& importSettings)
{
	const std::string policy = ToLowerAscii(importSettings.rigidAttachmentPolicy);
	return policy.empty() ||
		policy == "auto" ||
		policy == "preservenodeoffset" ||
		policy == "rigid" ||
		policy == "rigidbind" ||
		policy == "bonechild";
}

static aiMatrix4x4 MakeIdentityAiMatrix()
{
	aiMatrix4x4 transform;
	transform.a1 = 1.0f; transform.a2 = 0.0f; transform.a3 = 0.0f; transform.a4 = 0.0f;
	transform.b1 = 0.0f; transform.b2 = 1.0f; transform.b3 = 0.0f; transform.b4 = 0.0f;
	transform.c1 = 0.0f; transform.c2 = 0.0f; transform.c3 = 1.0f; transform.c4 = 0.0f;
	transform.d1 = 0.0f; transform.d2 = 0.0f; transform.d3 = 0.0f; transform.d4 = 1.0f;
	return transform;
}

static float MaxAbsMatrixDiff(const glm::mat4& a, const glm::mat4& b)
{
	float maxDiff = 0.0f;
	for (int column = 0; column < 4; ++column)
	{
		for (int row = 0; row < 4; ++row)
		{
			maxDiff = std::max(maxDiff, std::abs(a[column][row] - b[column][row]));
		}
	}
	return maxDiff;
}

static float MaxAbsIdentityDiff(const glm::mat4& matrix)
{
	return MaxAbsMatrixDiff(matrix, glm::mat4(1.0f));
}

static bool IsNearlyIdentityAiMatrix(const aiMatrix4x4& transform, float epsilon = 1.0e-4f)
{
	const float values[16] = {
		transform.a1, transform.a2, transform.a3, transform.a4,
		transform.b1, transform.b2, transform.b3, transform.b4,
		transform.c1, transform.c2, transform.c3, transform.c4,
		transform.d1, transform.d2, transform.d3, transform.d4
	};
	for (int i = 0; i < 16; ++i)
	{
		const float expected = (i == 0 || i == 5 || i == 10 || i == 15) ? 1.0f : 0.0f;
		if (std::abs(values[i] - expected) > epsilon)
			return false;
	}
	return true;
}

static bool ShouldBakeSkinnedMeshNodeTransform(
	const Vans::VansSkeletalMeshImportSettings& importSettings,
	const aiScene* scene,
	const std::unordered_map<uint32_t, aiMatrix4x4>& meshToModelTransform)
{
	if (ShouldBakeSkinnedMeshNodeTransform(importSettings))
		return true;

	if (!IsAutoMeshNodeTransformPolicy(importSettings) || !scene)
		return false;

	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
	{
		const aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!mesh || mesh->mNumBones == 0)
			continue;

		const auto transformIt = meshToModelTransform.find(meshIndex);
		if (transformIt != meshToModelTransform.end() &&
			!IsNearlyIdentityAiMatrix(transformIt->second))
		{
			return true;
		}
	}

	return false;
}

static void BuildMeshToModelTransformMap(const aiNode* node,
	const aiMatrix4x4& parentTransform,
	std::unordered_map<uint32_t, aiMatrix4x4>& outMeshToModelTransform)
{
	if (!node)
		return;

	const aiMatrix4x4 accumulatedTransform = parentTransform * node->mTransformation;
	for (uint32_t i = 0; i < node->mNumMeshes; ++i)
	{
		const uint32_t meshIndex = node->mMeshes[i];
		if (outMeshToModelTransform.find(meshIndex) == outMeshToModelTransform.end())
			outMeshToModelTransform[meshIndex] = accumulatedTransform;
	}

	for (uint32_t i = 0; i < node->mNumChildren; ++i)
		BuildMeshToModelTransformMap(node->mChildren[i], accumulatedTransform, outMeshToModelTransform);
}

static glm::vec3 ConvertVec3(const aiVector3D& v)
{
	return glm::vec3(v.x, v.y, v.z);
}

static glm::quat ConvertQuat(const aiQuaternion& q)
{
	return glm::quat(q.w, q.x, q.y, q.z);
}

struct AnimationSpaceConversion
{
	glm::mat3 basis = glm::mat3(1.0f);
	float positionScale = 1.0f;
	float normalizedError = 0.0f;
	uint32_t sampleCount = 0;
};

static const aiNode* FindAiNodeByName(const aiNode* node, const std::string& name);

static AnimationSpaceConversion ResolveAnimationSpaceConversion(
	const aiAnimation* anim,
	const Skeleton& skeleton,
	const aiScene* scene,
	float fallbackScale)
{
	AnimationSpaceConversion conversion;
	conversion.positionScale = fallbackScale;
	if (!anim)
		return conversion;

	struct TranslationPair
	{
		glm::vec3 source = glm::vec3(0.0f);
		glm::vec3 target = glm::vec3(0.0f);
	};
	std::vector<TranslationPair> pairs;
	for (uint32_t channelIndex = 0; channelIndex < anim->mNumChannels; ++channelIndex)
	{
		const aiNodeAnim* channel = anim->mChannels[channelIndex];
		if (!channel || channel->mNumPositionKeys == 0)
			continue;
		const auto boneIt = skeleton.boneNameToIndex.find(channel->mNodeName.C_Str());
		if (boneIt == skeleton.boneNameToIndex.end())
			continue;
		const aiNode* sourceNode = scene
			? FindAiNodeByName(scene->mRootNode, channel->mNodeName.C_Str())
			: nullptr;
		const glm::vec3 source = sourceNode
			? glm::vec3(ConvertMat4(sourceNode->mTransformation)[3])
			: ConvertVec3(channel->mPositionKeys[0].mValue);
		const glm::vec3 target = glm::vec3(
			skeleton.bones[static_cast<std::size_t>(boneIt->second)].localTransform[3]);
		if (glm::dot(source, source) <= 1.0e-8f || glm::dot(target, target) <= 1.0e-8f)
			continue;
		pairs.push_back({ source, target });
	}

	// A signed axis permutation covers the coordinate-system changes produced by
	// common DCC/engine exports (for example UE FBX Z-up to glTF Y-up). The uniform
	// scale is fitted independently so centimetre FBX clips can target metre glTF rigs.
	if (pairs.size() < 3)
		return conversion;

	static constexpr int Permutations[6][3] = {
		{ 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 },
		{ 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 }
	};
	std::vector<float> scaleSamples;
	scaleSamples.reserve(pairs.size());
	for (const TranslationPair& pair : pairs)
		scaleSamples.push_back(glm::length(pair.target) / glm::length(pair.source));
	std::sort(scaleSamples.begin(), scaleSamples.end());
	const float fittedScale = scaleSamples[scaleSamples.size() / 2];
	float bestError = std::numeric_limits<float>::max();
	for (const auto& permutation : Permutations)
	{
		for (int signMask = 0; signMask < 8; ++signMask)
		{
			glm::mat3 basis(0.0f);
			for (int outputAxis = 0; outputAxis < 3; ++outputAxis)
			{
				const float sign = (signMask & (1 << outputAxis)) ? -1.0f : 1.0f;
				basis[permutation[outputAxis]][outputAxis] = sign;
			}
			if (glm::determinant(basis) < 0.5f)
				continue;

			float error = 0.0f;
			for (const TranslationPair& pair : pairs)
			{
				const glm::vec3 delta = pair.target - fittedScale * basis * pair.source;
				error += glm::dot(delta, delta) / glm::dot(pair.target, pair.target);
			}
			const float normalizedError = error / static_cast<float>(pairs.size());
			if (normalizedError < bestError)
			{
				bestError = normalizedError;
				conversion.basis = basis;
				conversion.positionScale = fittedScale;
				conversion.normalizedError = normalizedError;
				conversion.sampleCount = static_cast<uint32_t>(pairs.size());
			}
		}
	}

	// A poor fit means the source is a genuinely different rig, not merely the
	// same skeleton exported with another axis/unit convention.
	if (bestError > 0.1f)
	{
		conversion = {};
		conversion.positionScale = fallbackScale;
	}
	return conversion;
}

static glm::vec3 ConvertAnimationPosition(
	const glm::vec3& position,
	const AnimationSpaceConversion& conversion)
{
	return conversion.positionScale * conversion.basis * position;
}

static glm::quat ConvertAnimationRotation(
	const glm::quat& rotation,
	const AnimationSpaceConversion& conversion)
{
	const glm::quat basisRotation = glm::normalize(glm::quat_cast(conversion.basis));
	return glm::normalize(basisRotation * rotation * glm::conjugate(basisRotation));
}

static glm::vec3 ConvertAnimationScale(
	const glm::vec3& scale,
	const AnimationSpaceConversion& conversion)
{
	glm::mat3 absoluteBasis(0.0f);
	for (int column = 0; column < 3; ++column)
		for (int row = 0; row < 3; ++row)
			absoluteBasis[column][row] = std::abs(conversion.basis[column][row]);
	return absoluteBasis * scale;
}

static bool AnimationHasNodeTransformChannels(
	const aiScene* scene,
	const aiAnimation* anim,
	const Skeleton& skeleton)
{
	if (!scene || !anim)
		return false;
	for (uint32_t c = 0; c < anim->mNumChannels; ++c)
	{
		const aiNodeAnim* channel = anim->mChannels[c];
		if (!channel)
			continue;
		const std::string nodeName = channel->mNodeName.C_Str();
		if (skeleton.boneNameToIndex.find(nodeName) != skeleton.boneNameToIndex.end())
			continue;
		if (FindAiNodeByName(scene->mRootNode, nodeName) != nullptr)
			return true;
	}
	return false;
}

static glm::mat4 ConvertNodeMatrixScaled(const aiMatrix4x4& m, float scaleFactor)
{
	glm::mat4 result = ConvertMat4(m);
	result[3].x *= scaleFactor;
	result[3].y *= scaleFactor;
	result[3].z *= scaleFactor;
	return result;
}

struct AiNodeTransformInfo
{
	const aiNode* node = nullptr;
	std::string nodeName;
	std::string nodePath;
	glm::mat4 bindLocalTransform = glm::mat4(1.0f);
	glm::mat4 bindModelTransform = glm::mat4(1.0f);
};

static void CollectNodeTransformInfos(
	const aiNode* node,
	const std::string& parentPath,
	const glm::mat4& parentModelTransform,
	float scaleFactor,
	std::vector<AiNodeTransformInfo>& outInfos,
	std::unordered_map<std::string, size_t>& outFirstNameToInfo,
	std::unordered_map<const aiNode*, size_t>& outNodeToInfo)
{
	if (!node)
		return;

	AiNodeTransformInfo info;
	info.node = node;
	info.nodeName = node->mName.C_Str();
	info.nodePath = parentPath.empty() ? info.nodeName : parentPath + "/" + info.nodeName;
	info.bindLocalTransform = ConvertNodeMatrixScaled(node->mTransformation, scaleFactor);
	info.bindModelTransform = parentModelTransform * info.bindLocalTransform;

	const size_t index = outInfos.size();
	outInfos.push_back(info);
	outNodeToInfo[node] = index;
	if (!info.nodeName.empty() && outFirstNameToInfo.find(info.nodeName) == outFirstNameToInfo.end())
		outFirstNameToInfo[info.nodeName] = index;

	for (uint32_t i = 0; i < node->mNumChildren; ++i)
	{
		CollectNodeTransformInfos(
			node->mChildren[i],
			info.nodePath,
			info.bindModelTransform,
			scaleFactor,
			outInfos,
			outFirstNameToInfo,
			outNodeToInfo);
	}
}

static bool DecomposeTRS(const glm::mat4& matrix, glm::vec3& outPosition, glm::quat& outRotation, glm::vec3& outScale)
{
	glm::vec3 skew;
	glm::vec4 perspective;
	if (!glm::decompose(matrix, outScale, outRotation, outPosition, skew, perspective))
		return false;
	outRotation = glm::normalize(outRotation);
	return true;
}

static glm::vec3 SampleVectorKeys(
	const aiVectorKey* keys,
	uint32_t keyCount,
	double sampleTicks,
	const glm::vec3& fallback,
	float scaleFactorForPosition)
{
	if (!keys || keyCount == 0)
		return fallback;
	if (keyCount == 1 || sampleTicks <= keys[0].mTime)
		return ConvertVec3(keys[0].mValue) * scaleFactorForPosition;
	if (sampleTicks >= keys[keyCount - 1].mTime)
		return ConvertVec3(keys[keyCount - 1].mValue) * scaleFactorForPosition;

	for (uint32_t k = 0; k < keyCount - 1; ++k)
	{
		if (sampleTicks <= keys[k + 1].mTime)
		{
			const double t0 = keys[k].mTime;
			const double t1 = keys[k + 1].mTime;
			float alpha = (t1 > t0) ? static_cast<float>((sampleTicks - t0) / (t1 - t0)) : 0.0f;
			alpha = glm::clamp(alpha, 0.0f, 1.0f);
			return glm::mix(
				ConvertVec3(keys[k].mValue) * scaleFactorForPosition,
				ConvertVec3(keys[k + 1].mValue) * scaleFactorForPosition,
				alpha);
		}
	}

	return fallback;
}

static glm::quat SampleQuatKeys(
	const aiQuatKey* keys,
	uint32_t keyCount,
	double sampleTicks,
	const glm::quat& fallback)
{
	if (!keys || keyCount == 0)
		return fallback;
	if (keyCount == 1 || sampleTicks <= keys[0].mTime)
		return ConvertQuat(keys[0].mValue);
	if (sampleTicks >= keys[keyCount - 1].mTime)
		return ConvertQuat(keys[keyCount - 1].mValue);

	for (uint32_t k = 0; k < keyCount - 1; ++k)
	{
		if (sampleTicks <= keys[k + 1].mTime)
		{
			const double t0 = keys[k].mTime;
			const double t1 = keys[k + 1].mTime;
			float alpha = (t1 > t0) ? static_cast<float>((sampleTicks - t0) / (t1 - t0)) : 0.0f;
			alpha = glm::clamp(alpha, 0.0f, 1.0f);
			aiQuaternion out;
			aiQuaternion::Interpolate(out, keys[k].mValue, keys[k + 1].mValue, alpha);
			return ConvertQuat(out);
		}
	}

	return fallback;
}

static void ExtractNodeTransformChannelsFromAssimp(
	const aiScene* scene,
	const aiAnimation* anim,
	const Skeleton& skeleton,
	float scaleFactor,
	VansAnimationClip& outClip)
{
	if (!scene || !scene->mRootNode || !anim)
		return;

	std::vector<AiNodeTransformInfo> nodeInfos;
	std::unordered_map<std::string, size_t> firstNameToInfo;
	std::unordered_map<const aiNode*, size_t> nodeToInfo;
	CollectNodeTransformInfos(
		scene->mRootNode,
		"",
		glm::mat4(1.0f),
		scaleFactor,
		nodeInfos,
		firstNameToInfo,
		nodeToInfo);

	std::unordered_map<const aiNode*, int> nodeToChannelIndex;
	for (uint32_t c = 0; c < anim->mNumChannels; ++c)
	{
		const aiNodeAnim* channel = anim->mChannels[c];
		if (!channel)
			continue;

		const std::string nodeName = channel->mNodeName.C_Str();
		if (skeleton.boneNameToIndex.find(nodeName) != skeleton.boneNameToIndex.end())
			continue;

		const auto infoIt = firstNameToInfo.find(nodeName);
		if (infoIt == firstNameToInfo.end())
			continue;

		const AiNodeTransformInfo& info = nodeInfos[infoIt->second];
		std::set<float> timestamps;
		for (uint32_t k = 0; k < channel->mNumPositionKeys; ++k)
			timestamps.insert(static_cast<float>(channel->mPositionKeys[k].mTime / outClip.ticksPerSecond));
		for (uint32_t k = 0; k < channel->mNumRotationKeys; ++k)
			timestamps.insert(static_cast<float>(channel->mRotationKeys[k].mTime / outClip.ticksPerSecond));
		for (uint32_t k = 0; k < channel->mNumScalingKeys; ++k)
			timestamps.insert(static_cast<float>(channel->mScalingKeys[k].mTime / outClip.ticksPerSecond));
		if (timestamps.empty())
			continue;

		glm::vec3 bindPosition(0.0f);
		glm::quat bindRotation(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 bindScale(1.0f);
		DecomposeTRS(info.bindLocalTransform, bindPosition, bindRotation, bindScale);

		NodeTransformChannel outChannel;
		outChannel.nodeName = info.nodeName;
		outChannel.nodePath = info.nodePath;
		outChannel.bindLocalTransform = info.bindLocalTransform;
		outChannel.bindModelTransform = info.bindModelTransform;
		outChannel.keyframes.reserve(timestamps.size());

		for (float time : timestamps)
		{
			const double sampleTicks = static_cast<double>(time) * outClip.ticksPerSecond;
			TransformKeyframe kf;
			kf.time = time;
			kf.position = SampleVectorKeys(
				channel->mPositionKeys,
				channel->mNumPositionKeys,
				sampleTicks,
				bindPosition,
				scaleFactor);
			kf.rotation = SampleQuatKeys(
				channel->mRotationKeys,
				channel->mNumRotationKeys,
				sampleTicks,
				bindRotation);
			kf.scale = SampleVectorKeys(
				channel->mScalingKeys,
				channel->mNumScalingKeys,
				sampleTicks,
				bindScale,
				1.0f);
			outChannel.keyframes.push_back(kf);
		}

		const int channelIndex = static_cast<int>(outClip.nodeTransformChannels.size());
		nodeToChannelIndex[info.node] = channelIndex;
		outClip.nodeTransformChannels.push_back(std::move(outChannel));
	}

	for (NodeTransformChannel& channel : outClip.nodeTransformChannels)
	{
		const auto infoIt = firstNameToInfo.find(channel.nodeName);
		if (infoIt == firstNameToInfo.end())
			continue;
		const aiNode* parent = nodeInfos[infoIt->second].node ? nodeInfos[infoIt->second].node->mParent : nullptr;
		while (parent)
		{
			auto parentIt = nodeToChannelIndex.find(parent);
			if (parentIt != nodeToChannelIndex.end())
			{
				channel.parentChannelIndex = parentIt->second;
				break;
			}
			parent = parent->mParent;
		}
	}
}


// ---------------------------------------------------------------------------
// ProcessAnimatedMesh: main entry point
// ---------------------------------------------------------------------------

bool VansGraphics::VansSkinnedMeshLoader::ProcessAnimatedMesh(
	const aiScene* scene,
	const std::string& fbxFilePath,
	uint32_t totalVertexCount,
	float scaleFactor,
	VansAnimationImportResult& outResult,
	const Vans::VansSkeletalMeshImportSettings& importSettings)
{
	if (!scene)
	{
		VANS_LOG_ERROR("[VansSkinnedMeshLoader] Null aiScene");
		return false;
	}

	// Step 1: Check if any mesh has bones.
	// Animations are optional; a bind-pose model with no clips still needs the full
	// skeleton + bone-weight pipeline so the GPU skinning path is enabled.
	bool sceneHasBones = false;
	for (uint32_t m = 0; m < scene->mNumMeshes && !sceneHasBones; m++)
		if (scene->mMeshes[m]->mNumBones > 0)
			sceneHasBones = true;

	if (!sceneHasBones)
	{
		outResult.hasAnimation = false;
		outResult.skeleton = {};
		outResult.vertexBoneData.clear();
		outResult.clips.clear();

		if (scene->HasAnimations())
		{
			std::string clipDir = GetParentDirectory(fbxFilePath);
			std::string baseName = GetFileBaseName(fbxFilePath);
			for (uint32_t i = 0; i < scene->mNumAnimations; i++)
			{
				aiAnimation* anim = scene->mAnimations[i];
				std::string clipName = anim->mName.C_Str();
				if (clipName.empty())
					clipName = baseName + "_clip" + std::to_string(i);
				for (char& c : clipName)
				{
					if (c == ' ' || c == '/' || c == '\\' || c == ':')
						c = '_';
				}

				std::string vclipPath = clipDir + "/" + baseName + "_" + clipName + ".vclip";
				VansAnimationClip clip;
				if (FileExists(vclipPath))
				{
					Skeleton cachedSkeleton;
					if (!VansAnimationClipIO::Load(vclipPath, clip, cachedSkeleton) ||
						(AnimationHasNodeTransformChannels(scene, anim, outResult.skeleton) &&
						 clip.nodeTransformChannels.empty()))
					{
						ExtractClipFromAssimp(anim, outResult.skeleton, clip, scene, scaleFactor);
						clip.clipName = clipName;
						VansAnimationClipIO::Save(vclipPath, clip, outResult.skeleton);
					}
				}
				else
				{
					ExtractClipFromAssimp(anim, outResult.skeleton, clip, scene, scaleFactor);
					clip.clipName = clipName;
					VansAnimationClipIO::Save(vclipPath, clip, outResult.skeleton);
				}
				if (!clip.nodeTransformChannels.empty())
				{
					outResult.clips.push_back(std::move(clip));
				}
			}
			outResult.hasAnimation = !outResult.clips.empty();
		}

		VANS_LOG("[VansSkinnedMeshLoader] No bones found in: " << fbxFilePath
		         << ", node transform clips=" << outResult.clips.size());
		return true;
	}

	VANS_LOG("[VansSkinnedMeshLoader] Bones detected in: " << fbxFilePath
	         << (scene->HasAnimations()
	             ? " (" + std::to_string(scene->mNumAnimations) + " animation clip(s))"
	             : " (bind-pose only, no animation clips)"));
	if (importSettings.diagnostics)
	{
		VANS_LOG("[SkeletalImport] Skeleton import settings: bindPoseSource=\""
			<< importSettings.bindPoseSource
			<< "\" meshNodeTransformPolicy=\"" << importSettings.meshNodeTransformPolicy
			<< "\" scaleFactor=" << scaleFactor);
	}

	// Step 2: Extract skeleton
	ExtractSkeleton(scene, outResult.skeleton, scaleFactor, importSettings);

	if (outResult.skeleton.bones.empty())
	{
		VANS_LOG_WARN("[VansSkinnedMeshLoader] Meshes have bones but skeleton extraction yielded nothing!");
		outResult.hasAnimation = false;
		return true;
	}

	// Skeleton successfully extracted; mark as having a skeletal rig.
	// Even without animation clips the bone pipeline must run (identity-pose skinning).
	outResult.hasAnimation = true;

	// Step 3: Extract bone weights per vertex
	ExtractVertexBoneData(scene, outResult.skeleton, totalVertexCount, outResult.vertexBoneData,
		importSettings);

	// Step 4: For each animation clip, check cache or extract
	std::string clipDir  = GetParentDirectory(fbxFilePath);
	std::string baseName = GetFileBaseName(fbxFilePath);

	for (uint32_t i = 0; i < scene->mNumAnimations; i++)
	{
		aiAnimation* anim = scene->mAnimations[i];
		std::string clipName = anim->mName.C_Str();
		if (clipName.empty())
			clipName = baseName + "_clip" + std::to_string(i);

		// Sanitize clip name (replace spaces, special chars)
		for (char& c : clipName)
		{
			if (c == ' ' || c == '/' || c == '\\' || c == ':')
				c = '_';
		}

		// Expected cache path: same folder as source FBX
		std::string vclipPath = clipDir + "/" + baseName + "_" + clipName + ".vclip";

		VansAnimationClip clip;

		if (FileExists(vclipPath))
		{
			// Fast path: load from cached .vclip
			Skeleton cachedSkeleton;
			if (VansAnimationClipIO::Load(vclipPath, clip, cachedSkeleton))
			{
				// Cache validity is an exact animation-layout contract: identity,
				// ordering, hierarchy and bind pose must all match the fresh skeleton.
				const bool cachedMissingNodeChannels =
					AnimationHasNodeTransformChannels(scene, anim, outResult.skeleton) &&
					clip.nodeTransformChannels.empty();
				std::string skeletonMismatch;
				const bool cachedSkeletonMatches = cachedSkeleton.MatchesAnimationLayout(
					outResult.skeleton, &skeletonMismatch);
				if (!cachedSkeletonMatches || cachedMissingNodeChannels)
				{
					VANS_LOG_WARN("[VansSkinnedMeshLoader] Cached clip stale (cachedBones="
					              << cachedSkeleton.bones.size() << ", currentBones="
					              << outResult.skeleton.bones.size()
					              << ", skeletonMismatch=\"" << skeletonMismatch << "\""
					              << ", missingNodeChannels=" << (cachedMissingNodeChannels ? 1 : 0)
					              << "), re-extracting: " << vclipPath);
					ExtractClipFromAssimp(anim, outResult.skeleton, clip, scene, scaleFactor);
					clip.clipName = clipName;
					VansAnimationClipIO::Save(vclipPath, clip, outResult.skeleton);
				}
				else
				{
					VANS_LOG("[VansSkinnedMeshLoader] Loaded cached clip: " << vclipPath);
				}
			}
			else
			{
			// Cache is corrupt; re-extract.
				VANS_LOG_WARN("[VansSkinnedMeshLoader] Failed to load cached clip, re-extracting: " << vclipPath);
				ExtractClipFromAssimp(anim, outResult.skeleton, clip, scene, scaleFactor);
				clip.clipName = clipName;
				VansAnimationClipIO::Save(vclipPath, clip, outResult.skeleton);
			}
		}
		else
		{
			// Slow path: extract from Assimp, then save cache
			ExtractClipFromAssimp(anim, outResult.skeleton, clip, scene, scaleFactor);
			clip.clipName = clipName;
			VansAnimationClipIO::Save(vclipPath, clip, outResult.skeleton);
			VANS_LOG("[VansSkinnedMeshLoader] Extracted and cached clip: " << vclipPath);
		}

		outResult.clips.push_back(std::move(clip));
	}

	VANS_LOG("[VansSkinnedMeshLoader] Import complete: "
	         << outResult.skeleton.bones.size() << " bones, "
	         << outResult.clips.size() << " clips");
	return true;
}

// ---------------------------------------------------------------------------
// ExtractSkeleton
// ---------------------------------------------------------------------------

// Helper: find an aiNode by name in the scene tree.
static const aiNode* FindAiNodeByName(const aiNode* node, const std::string& name)
{
	if (!node) return nullptr;
	if (std::string(node->mName.C_Str()) == name)
		return node;
	for (uint32_t i = 0; i < node->mNumChildren; i++)
	{
		const aiNode* found = FindAiNodeByName(node->mChildren[i], name);
		if (found) return found;
	}
	return nullptr;
}

// Helper: compute the offset matrix for a hierarchy-only node.
// The offset matrix transforms from model space to bone-local space.
// For a bone that is part of the node tree, this is the inverse of the
// accumulated (model-space) transform of that node.
static glm::mat4 ComputeOffsetMatrixFromNode(const aiNode* node)
{
	// Accumulate transforms from root to this node
	glm::mat4 modelSpaceTransform = glm::mat4(1.0f);
	std::vector<const aiNode*> chain;
	const aiNode* cur = node;
	while (cur)
	{
		chain.push_back(cur);
		cur = cur->mParent;
	}
	// Walk from root to node.
	for (int i = (int)chain.size() - 1; i >= 0; i--)
		modelSpaceTransform = modelSpaceTransform * ConvertMat4(chain[i]->mTransformation);

	return glm::inverse(modelSpaceTransform);
}

static bool IsNearlyIdentity(const glm::mat4& matrix, float epsilon = 1.0e-4f)
{
	for (int column = 0; column < 4; ++column)
	{
		for (int row = 0; row < 4; ++row)
		{
			const float expected = column == row ? 1.0f : 0.0f;
			if (std::abs(matrix[column][row] - expected) > epsilon)
				return false;
		}
	}
	return true;
}

static bool HasGrossBindTranslationError(const glm::mat4& importedOffset,
	const glm::mat4& hierarchyOffset)
{
	// A valid inverse-bind cancels the node's accumulated bind transform. Some
	// UE-exported rigid attachment bones retain rotation but lose that inverse
	// translation, so they are not close to identity yet still place geometry
	// tens of units away from the socket. Keep the threshold deliberately large
	// and gate this behind the per-model import option.
	const glm::mat4 bindResidual = glm::inverse(hierarchyOffset) * importedOffset;
	return glm::length(glm::vec3(bindResidual[3])) > 1.0f;
}

static bool ComputeRelativeTransformToNamedAncestor(const aiNode* node,
	const char* ancestorName, glm::mat4& outRelativeTransform)
{
	std::vector<const aiNode*> chain;
	const aiNode* current = node;
	while (current && std::string(current->mName.C_Str()) != ancestorName)
	{
		chain.push_back(current);
		current = current->mParent;
	}
	if (!current)
		return false;

	outRelativeTransform = glm::mat4(1.0f);
	for (auto it = chain.rbegin(); it != chain.rend(); ++it)
		outRelativeTransform = outRelativeTransform * ConvertMat4((*it)->mTransformation);
	return true;
}

static const char* FindWeaponAttachmentHand(const aiNode* node)
{
	for (const aiNode* current = node; current; current = current->mParent)
	{
		const std::string nodeName = current->mName.C_Str();
		if (nodeName.rfind("weapon_", 0) != 0 && nodeName.rfind("ult_weapon_", 0) != 0)
			continue;
		if (nodeName.size() >= 2 && nodeName.compare(nodeName.size() - 2, 2, "_l") == 0)
			return "hand_l";
		if (nodeName.size() >= 2 && nodeName.compare(nodeName.size() - 2, 2, "_r") == 0)
			return "hand_r";
	}
	return nullptr;
}

struct WeightedBoneSource
{
	const aiBone* bone = nullptr;
	glm::mat4 normalizedOffsetMatrix = glm::mat4(1.0f);
	glm::mat4 vertexToModelSpace = glm::mat4(1.0f);
	uint32_t firstMeshIndex = 0;
};

void VansGraphics::VansSkinnedMeshLoader::ExtractSkeleton(const aiScene* scene,
                                                           Skeleton& outSkeleton,
                                                           float scaleFactor,
                                                           const Vans::VansSkeletalMeshImportSettings& importSettings)
{
	outSkeleton.bones.clear();
	outSkeleton.boneNameToIndex.clear();
	outSkeleton.bonePathToIndex.clear();
	outSkeleton.boneGuidToIndex.clear();
	outSkeleton.sourceSkeletonGuid = importSettings.sourceSkeletonGuid;
	outSkeleton.signature = 0;
	const Vans::VansAssimpSkeletonTopology sourceTopology =
		Vans::BuildAssimpSkeletonTopology(scene);
	if (!sourceTopology.ambiguousWeightedNames.empty())
	{
		VANS_LOG_ERROR("[VansSkinnedMeshLoader] Cannot import a skeleton whose weighted bone name "
			"resolves to multiple source nodes: \"" << sourceTopology.ambiguousWeightedNames.front()
			<< "\". Weighted bones require unique source-node identity.");
		return;
	}

	// Phase 1: Collect all unique bones that have vertex weights from meshes.
	// If a skinned mesh node transform is baked into its vertices, the imported
	// inverse-bind matrix must be moved into that same model space:
	//     v_model = meshNodeTransform * v_meshLocal
	//     offset_model = offset_meshLocal * inverse(meshNodeTransform)
	// This lets a single Skeleton keep stable offsets even when a character is
	// split into many skinned mesh nodes in the FBX.
	std::unordered_map<uint32_t, aiMatrix4x4> meshToModelTransform;
	BuildMeshToModelTransformMap(scene->mRootNode, MakeIdentityAiMatrix(), meshToModelTransform);

	std::unordered_map<std::string, WeightedBoneSource> weightedBones;
	const bool bakeSkinnedMeshNodeTransform =
		ShouldBakeSkinnedMeshNodeTransform(importSettings, scene, meshToModelTransform);
	const bool useHierarchyBindPose = ShouldUseHierarchyBindPose(importSettings);
	if (importSettings.diagnostics && bakeSkinnedMeshNodeTransform)
	{
		VANS_LOG("[SkeletalImport] Baking skinned mesh-node transforms into vertices"
			<< " (meshNodeTransformPolicy=\"" << importSettings.meshNodeTransformPolicy << "\")");
	}
	for (uint32_t m = 0; m < scene->mNumMeshes; m++)
	{
		const aiMesh* mesh = scene->mMeshes[m];
		for (uint32_t b = 0; b < mesh->mNumBones; b++)
		{
			const aiBone* bone = mesh->mBones[b];
			std::string boneName = bone->mName.C_Str();
			const aiNode* weightedNode = sourceTopology.FindWeightedNode(boneName);
			glm::mat4 normalizedOffset =
				(useHierarchyBindPose && weightedNode)
					? ComputeOffsetMatrixFromNode(weightedNode)
					: ConvertMat4(bone->mOffsetMatrix);
			if (bakeSkinnedMeshNodeTransform && !useHierarchyBindPose)
			{
				auto transformIt = meshToModelTransform.find(m);
				if (transformIt != meshToModelTransform.end())
				{
					glm::mat4 vertexToModelSpace = ConvertMat4(transformIt->second);
					if (scaleFactor != 1.0f)
						vertexToModelSpace = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor)) * vertexToModelSpace;
					normalizedOffset = normalizedOffset * glm::inverse(vertexToModelSpace);
				}
			}

			glm::mat4 vertexToModelSpace(1.0f);
			if (bakeSkinnedMeshNodeTransform)
			{
				auto transformIt = meshToModelTransform.find(m);
				if (transformIt != meshToModelTransform.end())
					vertexToModelSpace = ConvertMat4(transformIt->second);
			}
			if (scaleFactor != 1.0f)
				vertexToModelSpace = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor)) * vertexToModelSpace;

			auto insertResult = weightedBones.emplace(
				boneName,
				WeightedBoneSource{ bone, normalizedOffset, vertexToModelSpace, m });
			if (!insertResult.second && importSettings.diagnostics)
			{
				const float diff = MaxAbsMatrixDiff(insertResult.first->second.normalizedOffsetMatrix, normalizedOffset);
				if (diff > 1.0e-3f)
				{
					VANS_LOG_WARN("[SkeletalImport] Bone \"" << boneName
						<< "\" has different inverse-bind offsets across skinned meshes after normalization"
						<< " (firstMesh=" << insertResult.first->second.firstMeshIndex
						<< ", mesh=" << m
						<< ", maxAbsDiff=" << diff
						<< ", meshNodeTransformPolicy=\"" << importSettings.meshNodeTransformPolicy << "\")");
				}
			}
		}
	}

	// Phase 2: 按精确源节点身份收集的真实骨骼祖先链，以 pre-order 创建骨骼。
	// 必要的无权重父节点会保留；同名但不在骨架祖先链上的 Mesh/包装节点不会进入变形骨架。
	// 运行时 index 只属于本次编译；持久引用来自模型 meta 中的 path/GUID。
	const std::string identityNamespace = !outSkeleton.sourceSkeletonGuid.empty()
		? outSkeleton.sourceSkeletonGuid
		: std::string(scene->mRootNode->mName.C_Str());
	std::unordered_set<int> weightedBoneIndices;
	const auto buildBoneInfo = [&](const std::string& name,
	                               const std::string& canonicalPath,
	                               const aiNode* node,
	                               int parentIndex,
	                               const WeightedBoneSource* weightedSource)
	{
		BoneInfo info;
		info.id = static_cast<int>(outSkeleton.bones.size());
		info.name = name;
		info.canonicalPath = canonicalPath;
		info.parentIndex = parentIndex;
		info.localTransform = node ? ConvertMat4(node->mTransformation) : glm::mat4(1.0f);
		const auto stableGuid = importSettings.boneGuidByCanonicalPath.find(canonicalPath);
		info.guid = stableGuid != importSettings.boneGuidByCanonicalPath.end()
			? stableGuid->second
			: Vans::VansAssetGuid::FromStableName(identityNamespace, "bone:" + canonicalPath).ToString();

		if (weightedSource)
		{
			info.offsetMatrix = weightedSource->normalizedOffsetMatrix;
			if (importSettings.legacyFixups.repairInvalidIdentityBindPose && node)
			{
				const glm::mat4 hierarchyOffset = ComputeOffsetMatrixFromNode(node);
				if (!IsNearlyIdentity(hierarchyOffset) &&
					(IsNearlyIdentity(info.offsetMatrix) ||
					 HasGrossBindTranslationError(info.offsetMatrix, hierarchyOffset)))
				{
					info.offsetMatrix = hierarchyOffset;
					VANS_LOG("[VansSkinnedMeshLoader] Rebuilt invalid inverse-bind from hierarchy for bone: \""
						<< name << "\"");
				}
			}

			if (importSettings.legacyFixups.remapWeaponAttachmentsToHands && node)
			{
				const char* handName = FindWeaponAttachmentHand(node);
				glm::mat4 handRelativeTransform(1.0f);
				if (handName && ComputeRelativeTransformToNamedAncestor(
					node, handName, handRelativeTransform))
					info.offsetMatrix = glm::inverse(handRelativeTransform);
			}
		}
		else
		{
			info.offsetMatrix = node ? ComputeOffsetMatrixFromNode(node) : glm::mat4(1.0f);
		}

		outSkeleton.bones.push_back(std::move(info));
		const int createdIndex = static_cast<int>(outSkeleton.bones.size() - 1);
		if (weightedSource)
			weightedBoneIndices.insert(createdIndex);
		if (parentIndex >= 0 && parentIndex < createdIndex)
			outSkeleton.bones[static_cast<std::size_t>(parentIndex)].children.push_back(createdIndex);
		return createdIndex;
	};

	std::function<void(const aiNode*, const std::string&, int)> visit =
		[&](const aiNode* node, const std::string& parentPath, int nearestBoneParent)
		{
			if (!node)
				return;
			const std::string name = node->mName.C_Str();
			const std::string path = parentPath.empty() ? name : parentPath + "/" + name;
			int childParent = nearestBoneParent;
			if (sourceTopology.requiredNodes.find(node) != sourceTopology.requiredNodes.end())
			{
				const auto weighted = weightedBones.find(name);
				const WeightedBoneSource* weightedSource =
					weighted != weightedBones.end() && sourceTopology.IsWeightedNode(node, name)
						? &weighted->second : nullptr;
				childParent = buildBoneInfo(name, path, node, nearestBoneParent, weightedSource);
			}
			for (uint32_t child = 0; child < node->mNumChildren; ++child)
				visit(node->mChildren[child], path, childParent);
		};
	visit(scene->mRootNode, {}, -1);

	// 缺失或重名而无法唯一解析的权重骨骼按名称排序追加，禁止错误猜测节点身份。
	std::vector<std::string> unresolvedNames = sourceTopology.unresolvedWeightedNames;
	for (const std::string& name : unresolvedNames)
	{
		const auto weighted = weightedBones.find(name);
		if (weighted == weightedBones.end())
			continue;
		buildBoneInfo(name, name, nullptr, -1, &weighted->second);
		VANS_LOG_WARN("[VansSkinnedMeshLoader] Weighted bone is missing from source node tree: \""
			<< name << "\"");
	}

	// Store global inverse transform
	outSkeleton.globalInverseTransform = glm::inverse(ConvertMat4(scene->mRootNode->mTransformation));

	// Log which bones are hierarchy-only (no vertex weights)
	int hierarchyOnlyCount = 0;
	for (const auto& bone : outSkeleton.bones)
	{
		if (weightedBoneIndices.find(bone.id) == weightedBoneIndices.end())
		{
			hierarchyOnlyCount++;
			VANS_LOG("[VansSkinnedMeshLoader] Hierarchy-only bone: \"" << bone.name
			         << "\" (id=" << bone.id << ", parentIndex=" << bone.parentIndex << ")");
		}
	}

	VANS_LOG("[VansSkinnedMeshLoader] Skeleton extracted: " << outSkeleton.bones.size()
	         << " bones (" << hierarchyOnlyCount << " hierarchy-only)");

	// Phase 4: 建立确定性 lookup/signature 与拓扑顺序。
	outSkeleton.BuildTopologicalOrder();
	outSkeleton.RebuildIdentityMapsAndSignature();

	if (importSettings.diagnostics)
	{
		std::vector<glm::mat4> bindGlobals(outSkeleton.bones.size(), glm::mat4(1.0f));
		for (size_t i = 0; i < outSkeleton.bones.size(); ++i)
			bindGlobals[i] = outSkeleton.bones[i].localTransform;

		const auto accumulateBone = [&](int index)
		{
			if (index < 0 || index >= static_cast<int>(outSkeleton.bones.size()))
				return;
			const int parentIndex = outSkeleton.bones[index].parentIndex;
			if (parentIndex >= 0 && parentIndex < static_cast<int>(outSkeleton.bones.size()))
				bindGlobals[index] = bindGlobals[parentIndex] * bindGlobals[index];
		};

		if (!outSkeleton.topologicalOrder.empty())
		{
			for (int index : outSkeleton.topologicalOrder)
				accumulateBone(index);
		}
		else
		{
			for (int index = 0; index < static_cast<int>(outSkeleton.bones.size()); ++index)
				accumulateBone(index);
		}

		float worstBindResidual = 0.0f;
		std::string worstBoneName;
		for (const BoneInfo& bone : outSkeleton.bones)
		{
			if (weightedBoneIndices.find(bone.id) == weightedBoneIndices.end())
				continue;
			const glm::mat4 residual =
				bindGlobals[bone.id] *
				bone.offsetMatrix;
			const float diff = MaxAbsIdentityDiff(residual);
			if (diff > worstBindResidual)
			{
				worstBindResidual = diff;
				worstBoneName = bone.name;
			}
		}
		VANS_LOG("[SkeletalImport] Bind-pose skin residual: maxAbsIdentityDiff="
			<< worstBindResidual << " bone=\"" << worstBoneName
			<< "\" meshNodeTransformPolicy=\"" << importSettings.meshNodeTransformPolicy << "\"");
	}
}

// ---------------------------------------------------------------------------
//  Helper: build a map from mesh index (scene->mMeshes[]) to the aiNode that owns it.
//  A mesh can appear in multiple nodes; we record the first one found (depth-first).
// ---------------------------------------------------------------------------

static void BuildMeshToNodeMap(const aiNode* node,
                               std::unordered_map<uint32_t, const aiNode*>& meshToNode)
{
	if (!node) return;
	for (uint32_t i = 0; i < node->mNumMeshes; i++)
	{
		uint32_t meshIdx = node->mMeshes[i];
		if (meshToNode.find(meshIdx) == meshToNode.end())
			meshToNode[meshIdx] = node;
	}
	for (uint32_t i = 0; i < node->mNumChildren; i++)
		BuildMeshToNodeMap(node->mChildren[i], meshToNode);
}

// ---------------------------------------------------------------------------
//  Helper: walk up the aiNode tree from a given node and return the
//  bone ID of the nearest ancestor whose name matches a bone in the skeleton.
//  Returns -1 if no bone ancestor is found.
// ---------------------------------------------------------------------------

static int FindNearestBoneAncestor(const aiNode* node,
                                   const VansGraphics::Skeleton& skeleton)
{
	// Start from the node itself; its name may already be a bone (for example,
	// has the same name as the bone, or the node IS the bone).
	const aiNode* current = node;
	while (current)
	{
		std::string nodeName = current->mName.C_Str();
		if (!nodeName.empty())
		{
			auto it = skeleton.boneNameToIndex.find(nodeName);
			if (it != skeleton.boneNameToIndex.end())
				return it->second;   // found a bone
		}
		current = current->mParent;
	}
	return -1;
}

// ---------------------------------------------------------------------------
// ExtractVertexBoneData
// ---------------------------------------------------------------------------

void VansGraphics::VansSkinnedMeshLoader::ExtractVertexBoneData(
	const aiScene* scene,
	const Skeleton& skeleton,
	uint32_t totalVertexCount,
	std::vector<VertexBoneData>& outData,
	const Vans::VansSkeletalMeshImportSettings& importSettings)
{
	outData.clear();
	outData.resize(totalVertexCount);

	// Track vertex offset as we process each mesh
	uint32_t vertexOffset = 0;

	// First pass: record per-mesh vertex offset for the second-pass fixup
	std::vector<uint32_t> meshVertexOffsets(scene->mNumMeshes, 0);

	for (uint32_t m = 0; m < scene->mNumMeshes; m++)
	{
		const aiMesh* mesh = scene->mMeshes[m];
		meshVertexOffsets[m] = vertexOffset;

		for (uint32_t b = 0; b < mesh->mNumBones; b++)
		{
			const aiBone* bone = mesh->mBones[b];
			if (!bone) continue;

			std::string boneName = bone->mName.C_Str();

			auto it = skeleton.boneNameToIndex.find(boneName);
			if (it == skeleton.boneNameToIndex.end())
				continue;

			int boneID = it->second;
			if (importSettings.legacyFixups.remapWeaponAttachmentsToHands && boneName.rfind("grenade", 0) == 0)
			{
				const auto weaponIt = skeleton.boneNameToIndex.find("weapon_r");
				if (weaponIt != skeleton.boneNameToIndex.end())
					boneID = weaponIt->second;
			}
			// FBX files commonly emit bone entries for the full skeleton in every
			// sub-mesh, even when a bone has zero influence on that mesh's vertices.
			// Skip silently; this is expected Assimp behavior, not an error.
			if (!bone->mWeights || bone->mNumWeights == 0)
				continue;

			for (uint32_t w = 0; w < bone->mNumWeights; w++)
			{
				uint32_t localVertexID  = bone->mWeights[w].mVertexId;
				uint32_t globalVertexID = vertexOffset + localVertexID;
				float weight            = bone->mWeights[w].mWeight;

				if (weight <= 0.0f)
					continue;  // skip zero-weight influences

				if (globalVertexID < totalVertexCount)
				{
					outData[globalVertexID].AddBoneInfluence(boneID, weight);
				}
				else
				{
					VANS_LOG_WARN("[VansSkinnedMeshLoader] Out-of-bounds vertexID "
					              << globalVertexID << " >= " << totalVertexCount
					              << " (bone \"" << boneName << "\", mesh " << m << ")");
				}
			}
		}

		vertexOffset += mesh->mNumVertices;
	}

	// Second pass: generic rigid attachments for unskinned meshes parented to bones.
	// These meshes are authored as rigid child nodes rather than skinned meshes.
	// Their vertices have already been baked into model space; assigning them to
	// the nearest ancestor bone with weight 1.0 makes:
	//     boneAnimated * inverse(boneBind) * vertexModel
	// evaluate to vertexModel in bind pose, preserving the FBX node offset while
	// letting the attachment follow animation.
	// Build a mesh-index to aiNode map so we can walk up the node tree.
	std::unordered_map<uint32_t, const aiNode*> meshToNode;
	BuildMeshToNodeMap(scene->mRootNode, meshToNode);

	const bool useRigidAttachmentBind =
		ShouldRigidBindUnskinnedBoneChildren(importSettings) ||
		importSettings.legacyFixups.nearestBoneRigidBind;

	for (uint32_t m = 0; m < scene->mNumMeshes; m++)
	{
		const aiMesh* mesh = scene->mMeshes[m];

		// Skip meshes that already have bone data (skinned meshes)
		if (mesh->mNumBones > 0)
		{
			continue;
		}

		uint32_t start = meshVertexOffsets[m];
		uint32_t end   = start + mesh->mNumVertices;

		// Check if ALL vertices of this mesh are still unbound (-1)
		bool allUnbound = true;
		for (uint32_t v = start; v < end && v < totalVertexCount; v++)
		{
			if (outData[v].boneIDs[0] >= 0)
			{
				allUnbound = false;
				break;
			}
		}
		if (!allUnbound)
			continue;

		if (!useRigidAttachmentBind)
		{
			if (importSettings.diagnostics)
			{
				VANS_LOG("[SkeletalImport] Preserving unskinned mesh " << m
					<< " (" << mesh->mNumVertices
					<< " vertices) without legacy nearest-bone skin weights.");
			}
			continue;
		}

		// Find the aiNode that owns this mesh, then walk up to find a bone ancestor
		auto nodeIt = meshToNode.find(m);
		if (nodeIt == meshToNode.end())
			continue;

		const aiNode* ownerNode = nodeIt->second;
		int parentBoneID = FindNearestBoneAncestor(ownerNode, skeleton);
		if (parentBoneID < 0)
			continue;   // no bone ancestor found; leave unbound

		// Rigid-bind: assign all vertices to the parent bone with weight 1.0
		for (uint32_t v = start; v < end && v < totalVertexCount; v++)
		{
			outData[v].AddBoneInfluence(parentBoneID, 1.0f);
		}

		if (importSettings.legacyFixups.nearestBoneRigidBind &&
			!ShouldRigidBindUnskinnedBoneChildren(importSettings))
		{
			VANS_LOG("[VansSkinnedMeshLoader] Legacy nearest-bone rigid-bind: mesh " << m
			         << " (" << mesh->mNumVertices << " vertices) -> bone \""
			         << skeleton.bones[parentBoneID].name << "\" (id=" << parentBoneID << ")");
		}
		else
		{
			if (importSettings.diagnostics)
			{
				VANS_LOG("[SkeletalImport] Rigid attachment bind: mesh " << m
				         << " (" << mesh->mNumVertices << " vertices) -> bone \""
				         << skeleton.bones[parentBoneID].name << "\" (id=" << parentBoneID
				         << ", policy=\"" << importSettings.rigidAttachmentPolicy << "\")");
			}
		}
	}

	// Normalize all weights
	for (auto& vbd : outData)
		vbd.Normalize();

	VANS_LOG("[VansSkinnedMeshLoader] Vertex bone data extracted for "
	         << totalVertexCount << " vertices");
}

// ---------------------------------------------------------------------------
// ExtractClipFromAssimp
// ---------------------------------------------------------------------------

void VansGraphics::VansSkinnedMeshLoader::ExtractClipFromAssimp(
	const aiAnimation* anim,
	const Skeleton& skeleton,
	VansAnimationClip& outClip,
	const aiScene* scene,
	float scaleFactor)
{
	if (!anim) return;

	outClip.clipName = anim->mName.C_Str();

	double ticksPerSecond = (anim->mTicksPerSecond > 0.0) ? anim->mTicksPerSecond : 25.0;
	outClip.ticksPerSecond = (float)ticksPerSecond;
	outClip.duration       = (float)(anim->mDuration / ticksPerSecond);

	uint32_t boneCount = (uint32_t)skeleton.bones.size();
	outClip.boneKeyframes.resize(boneCount);
	outClip.nodeTransformChannels.clear();
	const AnimationSpaceConversion spaceConversion =
		ResolveAnimationSpaceConversion(anim, skeleton, scene, scaleFactor);
	if (spaceConversion.sampleCount > 0 &&
		(std::abs(spaceConversion.positionScale - 1.0f) > 1.0e-4f ||
		 MaxAbsIdentityDiff(glm::mat4(spaceConversion.basis)) > 1.0e-4f))
	{
		VANS_LOG("[VansSkinnedMeshLoader] Animation space converted from "
			<< spaceConversion.sampleCount << " bind translations (scale="
			<< spaceConversion.positionScale << ", normalizedError="
			<< spaceConversion.normalizedError << ")");
	}

	// Process each channel (one channel per animated bone)
	for (uint32_t c = 0; c < anim->mNumChannels; c++)
	{
		const aiNodeAnim* channel = anim->mChannels[c];
		std::string boneName = channel->mNodeName.C_Str();

		auto it = skeleton.boneNameToIndex.find(boneName);
		if (it == skeleton.boneNameToIndex.end())
			continue;  // this channel animates a non-bone node

		int boneIdx = it->second;

		// We need to merge position, rotation, scale keyframes into unified BoneKeyframe entries.
		// Collect all unique timestamps first.
		std::set<float> timestamps;

		for (uint32_t k = 0; k < channel->mNumPositionKeys; k++)
			timestamps.insert((float)(channel->mPositionKeys[k].mTime / ticksPerSecond));
		for (uint32_t k = 0; k < channel->mNumRotationKeys; k++)
			timestamps.insert((float)(channel->mRotationKeys[k].mTime / ticksPerSecond));
		for (uint32_t k = 0; k < channel->mNumScalingKeys; k++)
			timestamps.insert((float)(channel->mScalingKeys[k].mTime / ticksPerSecond));

		auto& keyframes = outClip.boneKeyframes[boneIdx];
		keyframes.reserve(timestamps.size());

		for (float t : timestamps)
		{
			BoneKeyframe kf;
			kf.time = t;

			// Sample position at time t.
			if (channel->mNumPositionKeys == 1)
			{
				kf.position = ConvertVec3(channel->mPositionKeys[0].mValue);
			}
			else
			{
				kf.position = glm::vec3(0.0f);
				double tTicks = t * ticksPerSecond;
				for (uint32_t k = 0; k < channel->mNumPositionKeys - 1; k++)
				{
					if (tTicks <= channel->mPositionKeys[k + 1].mTime)
					{
						double t0 = channel->mPositionKeys[k].mTime;
						double t1 = channel->mPositionKeys[k + 1].mTime;
						float alpha = (t1 > t0) ? (float)((tTicks - t0) / (t1 - t0)) : 0.0f;
						alpha = glm::clamp(alpha, 0.0f, 1.0f);
						kf.position = glm::mix(
							ConvertVec3(channel->mPositionKeys[k].mValue),
							ConvertVec3(channel->mPositionKeys[k + 1].mValue),
							alpha);
						break;
					}
				}
				if (tTicks > channel->mPositionKeys[channel->mNumPositionKeys - 1].mTime)
					kf.position = ConvertVec3(channel->mPositionKeys[channel->mNumPositionKeys - 1].mValue);
			}
			kf.position = ConvertAnimationPosition(kf.position, spaceConversion);

			// Sample rotation at time t.
			if (channel->mNumRotationKeys == 1)
			{
				kf.rotation = ConvertQuat(channel->mRotationKeys[0].mValue);
			}
			else
			{
				kf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				double tTicks = t * ticksPerSecond;
				for (uint32_t k = 0; k < channel->mNumRotationKeys - 1; k++)
				{
					if (tTicks <= channel->mRotationKeys[k + 1].mTime)
					{
						double t0 = channel->mRotationKeys[k].mTime;
						double t1 = channel->mRotationKeys[k + 1].mTime;
						float alpha = (t1 > t0) ? (float)((tTicks - t0) / (t1 - t0)) : 0.0f;
						alpha = glm::clamp(alpha, 0.0f, 1.0f);
						aiQuaternion out;
						aiQuaternion::Interpolate(out,
							channel->mRotationKeys[k].mValue,
							channel->mRotationKeys[k + 1].mValue,
							alpha);
						kf.rotation = ConvertQuat(out);
						break;
					}
				}
				if (tTicks > channel->mRotationKeys[channel->mNumRotationKeys - 1].mTime)
					kf.rotation = ConvertQuat(channel->mRotationKeys[channel->mNumRotationKeys - 1].mValue);
			}
			kf.rotation = ConvertAnimationRotation(kf.rotation, spaceConversion);

			// Sample scale at time t.
			if (channel->mNumScalingKeys == 1)
			{
				kf.scale = ConvertVec3(channel->mScalingKeys[0].mValue);
			}
			else
			{
				kf.scale = glm::vec3(1.0f);
				double tTicks = t * ticksPerSecond;
				for (uint32_t k = 0; k < channel->mNumScalingKeys - 1; k++)
				{
					if (tTicks <= channel->mScalingKeys[k + 1].mTime)
					{
						double t0 = channel->mScalingKeys[k].mTime;
						double t1 = channel->mScalingKeys[k + 1].mTime;
						float alpha = (t1 > t0) ? (float)((tTicks - t0) / (t1 - t0)) : 0.0f;
						alpha = glm::clamp(alpha, 0.0f, 1.0f);
						kf.scale = glm::mix(
							ConvertVec3(channel->mScalingKeys[k].mValue),
							ConvertVec3(channel->mScalingKeys[k + 1].mValue),
							alpha);
						break;
					}
				}
				if (tTicks > channel->mScalingKeys[channel->mNumScalingKeys - 1].mTime)
					kf.scale = ConvertVec3(channel->mScalingKeys[channel->mNumScalingKeys - 1].mValue);
			}
			kf.scale = ConvertAnimationScale(kf.scale, spaceConversion);

			keyframes.push_back(kf);
		}
	}

	ExtractNodeTransformChannelsFromAssimp(scene, anim, skeleton, scaleFactor, outClip);

	VANS_LOG("[VansSkinnedMeshLoader] Extracted clip \"" << outClip.clipName
	         << "\" (" << outClip.duration << "s, " << anim->mNumChannels
	         << " channels, nodeTransformChannels="
	         << outClip.nodeTransformChannels.size() << ")");
}

// ---------------------------------------------------------------------------
//  Utility functions
// ---------------------------------------------------------------------------

std::string VansGraphics::VansSkinnedMeshLoader::GetParentDirectory(const std::string& filePath)
{
	std::filesystem::path p(filePath);
	return p.parent_path().string();
}

std::string VansGraphics::VansSkinnedMeshLoader::GetFileBaseName(const std::string& filePath)
{
	std::filesystem::path p(filePath);
	return p.stem().string();
}

bool VansGraphics::VansSkinnedMeshLoader::FileExists(const std::string& filePath)
{
	return std::filesystem::exists(filePath);
}

// ---------------------------------------------------------------------------
//  ExtractExternAnimationClips
//
//  Opens an external FBX file and extracts ONLY animation clips,
//  mapping bone channels to the origin model's skeleton by name.
//  No bone weights are read; those come from the origin model.
//  Clips are cached as .vclip files alongside the external FBX.
// ---------------------------------------------------------------------------

bool VansGraphics::VansSkinnedMeshLoader::ExtractExternAnimationClips(
	const std::string& externFbxPath,
	const Skeleton& originSkeleton,
	std::vector<VansAnimationClip>& outClips)
{
	outClips.clear();

	if (externFbxPath.empty())
	{
		VANS_LOG_WARN("[VansSkinnedMeshLoader] Extern animation path is empty.");
		return false;
	}

	if (!FileExists(externFbxPath))
	{
		VANS_LOG_ERROR("[VansSkinnedMeshLoader] Extern animation file not found: " << externFbxPath);
		return false;
	}

	if (originSkeleton.bones.empty())
	{
		VANS_LOG_ERROR("[VansSkinnedMeshLoader] Cannot load extern animation: origin skeleton has no bones.");
		return false;
	}

	// Open the external FBX; only minimal processing is needed (no tangents or normals).
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(externFbxPath,
		aiProcess_Triangulate | aiProcess_FlipUVs);

	if (!scene)
	{
		VANS_LOG_ERROR("[VansSkinnedMeshLoader] Failed to load extern animation FBX: "
		               << externFbxPath << ": " << importer.GetErrorString());
		return false;
	}

	if (!scene->HasAnimations())
	{
		VANS_LOG_WARN("[VansSkinnedMeshLoader] Extern FBX has no animations: " << externFbxPath);
		return false;
	}

	VANS_LOG("[VansSkinnedMeshLoader] Loading extern animation from: " << externFbxPath
	         << " (" << scene->mNumAnimations << " clip(s))");

	std::string clipDir  = GetParentDirectory(externFbxPath);
	std::string baseName = GetFileBaseName(externFbxPath);

	for (uint32_t i = 0; i < scene->mNumAnimations; i++)
	{
		aiAnimation* anim = scene->mAnimations[i];
		std::string clipName = anim->mName.C_Str();
		if (clipName.empty())
			clipName = baseName + "_clip" + std::to_string(i);

		// Sanitize clip name for filesystem
		for (char& c : clipName)
		{
			if (c == ' ' || c == '/' || c == '\\' || c == ':')
				c = '_';
		}

		std::string vclipPath = clipDir + "/" + baseName + "_" + clipName + ".vclip";

		VansAnimationClip clip;

		// This entry point is an explicit import operation. Always rebuild from the
		// FBX so changes to source data or coordinate conversion cannot be hidden by
		// a stale adjacent cache.
		ExtractClipFromAssimp(anim, originSkeleton, clip, scene);
		clip.clipName = clipName;
		VansAnimationClipIO::Save(vclipPath, clip, originSkeleton);
		VANS_LOG("[VansSkinnedMeshLoader] Imported extern clip: " << vclipPath);

		outClips.push_back(std::move(clip));
	}

	VANS_LOG("[VansSkinnedMeshLoader] Extern animation import complete: "
	         << outClips.size() << " clip(s) from " << externFbxPath);
	return true;
}
