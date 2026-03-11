#include "VansSkinnedMeshLoader.h"
#include "VansAnimationClip.h"
#include "../Util/VansLog.h"

#include <assimp/scene.h>
#include <assimp/anim.h>

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtc/type_ptr.hpp>

#include <filesystem>
#include <set>
#include <unordered_set>

using namespace VansGraphics;

// ─── Helper: convert Assimp mat4 → glm::mat4 (transpose: Assimp is row-major) ───

static glm::mat4 ConvertMat4(const aiMatrix4x4& m)
{
	return glm::mat4(
		m.a1, m.b1, m.c1, m.d1,
		m.a2, m.b2, m.c2, m.d2,
		m.a3, m.b3, m.c3, m.d3,
		m.a4, m.b4, m.c4, m.d4
	);
}

static glm::vec3 ConvertVec3(const aiVector3D& v)
{
	return glm::vec3(v.x, v.y, v.z);
}

static glm::quat ConvertQuat(const aiQuaternion& q)
{
	return glm::quat(q.w, q.x, q.y, q.z);
}

// ════════════════════════════════════════════════════════════════
//  ProcessAnimatedMesh — main entry point
// ════════════════════════════════════════════════════════════════

bool VansGraphics::VansSkinnedMeshLoader::ProcessAnimatedMesh(
	const aiScene* scene,
	const std::string& fbxFilePath,
	uint32_t totalVertexCount,
	VansAnimationImportResult& outResult)
{
	if (!scene)
	{
		VANS_LOG_ERROR("[VansSkinnedMeshLoader] Null aiScene");
		return false;
	}

	// Step 1: Check if FBX contains animations
	if (!scene->HasAnimations())
	{
		outResult.hasAnimation = false;
		VANS_LOG("[VansSkinnedMeshLoader] No animations found in: " << fbxFilePath);
		return true;
	}

	outResult.hasAnimation = true;
	VANS_LOG("[VansSkinnedMeshLoader] Found " << scene->mNumAnimations
	         << " animation(s) in: " << fbxFilePath);

	// Step 2: Extract skeleton
	ExtractSkeleton(scene, outResult.skeleton);

	if (outResult.skeleton.bones.empty())
	{
		VANS_LOG_WARN("[VansSkinnedMeshLoader] Scene has animations but no bones found!");
		outResult.hasAnimation = false;
		return true;
	}

	// Step 3: Extract bone weights per vertex
	ExtractVertexBoneData(scene, outResult.skeleton, totalVertexCount, outResult.vertexBoneData);

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
				VANS_LOG("[VansSkinnedMeshLoader] Loaded cached clip: " << vclipPath);
			}
			else
			{
				// Cache is corrupt — re-extract
				VANS_LOG_WARN("[VansSkinnedMeshLoader] Failed to load cached clip, re-extracting: " << vclipPath);
				ExtractClipFromAssimp(anim, outResult.skeleton, clip);
				clip.clipName = clipName;
				VansAnimationClipIO::Save(vclipPath, clip, outResult.skeleton);
			}
		}
		else
		{
			// Slow path: extract from Assimp, then save cache
			ExtractClipFromAssimp(anim, outResult.skeleton, clip);
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

// ════════════════════════════════════════════════════════════════
//  ExtractSkeleton
// ════════════════════════════════════════════════════════════════

void VansGraphics::VansSkinnedMeshLoader::ExtractSkeleton(const aiScene* scene,
                                                           Skeleton& outSkeleton)
{
	outSkeleton.bones.clear();
	outSkeleton.boneNameToIndex.clear();

	// Collect all unique bones from all meshes
	std::unordered_map<std::string, const aiBone*> uniqueBones;
	for (uint32_t m = 0; m < scene->mNumMeshes; m++)
	{
		const aiMesh* mesh = scene->mMeshes[m];
		for (uint32_t b = 0; b < mesh->mNumBones; b++)
		{
			const aiBone* bone = mesh->mBones[b];
			std::string boneName = bone->mName.C_Str();
			if (uniqueBones.find(boneName) == uniqueBones.end())
				uniqueBones[boneName] = bone;
		}
	}

	// Create BoneInfo for each unique bone
	int boneIndex = 0;
	for (const auto& [name, aiBonePtr] : uniqueBones)
	{
		BoneInfo info;
		info.id           = boneIndex;
		info.name         = name;
		info.offsetMatrix = ConvertMat4(aiBonePtr->mOffsetMatrix);
		info.parentIndex  = -1;  // resolved below

		outSkeleton.bones.push_back(info);
		outSkeleton.boneNameToIndex[name] = boneIndex;
		boneIndex++;
	}

	// Resolve parent-child hierarchy by walking the aiNode tree
	BuildHierarchyFromNodeTree(scene->mRootNode, outSkeleton, -1);

	// Store global inverse transform
	outSkeleton.globalInverseTransform = glm::inverse(ConvertMat4(scene->mRootNode->mTransformation));

	VANS_LOG("[VansSkinnedMeshLoader] Skeleton extracted: " << outSkeleton.bones.size() << " bones");
}

// ════════════════════════════════════════════════════════════════
//  BuildHierarchyFromNodeTree
//  Recursively walk aiNode tree, match node names to bone names.
// ════════════════════════════════════════════════════════════════

void VansGraphics::VansSkinnedMeshLoader::BuildHierarchyFromNodeTree(
	const aiNode* node, Skeleton& skeleton, int parentIndex)
{
	if (!node) return;

	std::string nodeName = node->mName.C_Str();
	int currentIndex = -1;

	auto it = skeleton.boneNameToIndex.find(nodeName);
	if (it != skeleton.boneNameToIndex.end())
	{
		currentIndex = it->second;
		skeleton.bones[currentIndex].parentIndex = parentIndex;

		// Add as child of parent
		if (parentIndex >= 0)
			skeleton.bones[parentIndex].children.push_back(currentIndex);
	}

	// If this node is not a bone but has bone children, pass parentIndex through
	int nextParent = (currentIndex >= 0) ? currentIndex : parentIndex;

	for (uint32_t i = 0; i < node->mNumChildren; i++)
		BuildHierarchyFromNodeTree(node->mChildren[i], skeleton, nextParent);
}

// ════════════════════════════════════════════════════════════════
//  ExtractVertexBoneData
// ════════════════════════════════════════════════════════════════

void VansGraphics::VansSkinnedMeshLoader::ExtractVertexBoneData(
	const aiScene* scene,
	const Skeleton& skeleton,
	uint32_t totalVertexCount,
	std::vector<VertexBoneData>& outData)
{
	outData.clear();
	outData.resize(totalVertexCount);

	// Track vertex offset as we process each mesh
	uint32_t vertexOffset = 0;

	for (uint32_t m = 0; m < scene->mNumMeshes; m++)
	{
		const aiMesh* mesh = scene->mMeshes[m];

		for (uint32_t b = 0; b < mesh->mNumBones; b++)
		{
			const aiBone* bone = mesh->mBones[b];
			std::string boneName = bone->mName.C_Str();

			auto it = skeleton.boneNameToIndex.find(boneName);
			if (it == skeleton.boneNameToIndex.end())
				continue;

			int boneID = it->second;

			for (uint32_t w = 0; w < bone->mNumWeights; w++)
			{
				uint32_t vertexID = vertexOffset + bone->mWeights[w].mVertexId;
				float weight      = bone->mWeights[w].mWeight;

				if (vertexID < totalVertexCount)
					outData[vertexID].AddBoneInfluence(boneID, weight);
			}
		}

		vertexOffset += mesh->mNumVertices;
	}

	// Normalize all weights
	for (auto& vbd : outData)
		vbd.Normalize();

	VANS_LOG("[VansSkinnedMeshLoader] Vertex bone data extracted for "
	         << totalVertexCount << " vertices");
}

// ════════════════════════════════════════════════════════════════
//  ExtractClipFromAssimp
// ════════════════════════════════════════════════════════════════

void VansGraphics::VansSkinnedMeshLoader::ExtractClipFromAssimp(
	const aiAnimation* anim,
	const Skeleton& skeleton,
	VansAnimationClip& outClip)
{
	if (!anim) return;

	outClip.clipName = anim->mName.C_Str();

	double ticksPerSecond = (anim->mTicksPerSecond > 0.0) ? anim->mTicksPerSecond : 25.0;
	outClip.ticksPerSecond = (float)ticksPerSecond;
	outClip.duration       = (float)(anim->mDuration / ticksPerSecond);

	uint32_t boneCount = (uint32_t)skeleton.bones.size();
	outClip.boneKeyframes.resize(boneCount);

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

			// ─── Sample position at time t ───
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

			// ─── Sample rotation at time t ───
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

			// ─── Sample scale at time t ───
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

			keyframes.push_back(kf);
		}
	}

	VANS_LOG("[VansSkinnedMeshLoader] Extracted clip \"" << outClip.clipName
	         << "\" (" << outClip.duration << "s, " << anim->mNumChannels << " channels)");
}

// ════════════════════════════════════════════════════════════════
//  Utility functions
// ════════════════════════════════════════════════════════════════

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
