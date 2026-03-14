#include "VansSkinnedMeshLoader.h"
#include "VansAnimationClip.h"
#include "../Util/VansLog.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/anim.h>
#include <assimp/postprocess.h>

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

	// Step 1: Check if any mesh has bones.
	// Animations are optional – a bind-pose model (no clips) still needs the full
	// skeleton + bone-weight pipeline so the GPU skinning path is enabled.
	bool sceneHasBones = false;
	for (uint32_t m = 0; m < scene->mNumMeshes && !sceneHasBones; m++)
		if (scene->mMeshes[m]->mNumBones > 0)
			sceneHasBones = true;

	if (!sceneHasBones)
	{
		outResult.hasAnimation = false;
		VANS_LOG("[VansSkinnedMeshLoader] No bones found in: " << fbxFilePath);
		return true;
	}

	VANS_LOG("[VansSkinnedMeshLoader] Bones detected in: " << fbxFilePath
	         << (scene->HasAnimations()
	             ? " (" + std::to_string(scene->mNumAnimations) + " animation clip(s))"
	             : " (bind-pose only, no animation clips)"));

	// Step 2: Extract skeleton
	ExtractSkeleton(scene, outResult.skeleton);

	if (outResult.skeleton.bones.empty())
	{
		VANS_LOG_WARN("[VansSkinnedMeshLoader] Meshes have bones but skeleton extraction yielded nothing!");
		outResult.hasAnimation = false;
		return true;
	}

	// Skeleton successfully extracted – mark as having a skeletal rig.
	// Even without animation clips the bone pipeline must run (identity-pose skinning).
	outResult.hasAnimation = true;

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
				// Validate that cached skeleton matches the freshly extracted one.
				// If the bone count changed (e.g. hierarchy-only bones now included),
				// the cache is stale and must be re-extracted.
				if (cachedSkeleton.bones.size() != outResult.skeleton.bones.size())
				{
					VANS_LOG_WARN("[VansSkinnedMeshLoader] Cached clip bone count ("
					              << cachedSkeleton.bones.size() << ") != current skeleton ("
					              << outResult.skeleton.bones.size() << "), re-extracting: " << vclipPath);
					ExtractClipFromAssimp(anim, outResult.skeleton, clip);
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

// ── Helper: find an aiNode by name in the scene tree ──
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

// ── Helper: compute the offset matrix for a hierarchy-only node ──
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
	// Walk from root → node
	for (int i = (int)chain.size() - 1; i >= 0; i--)
		modelSpaceTransform = modelSpaceTransform * ConvertMat4(chain[i]->mTransformation);

	return glm::inverse(modelSpaceTransform);
}

void VansGraphics::VansSkinnedMeshLoader::ExtractSkeleton(const aiScene* scene,
                                                           Skeleton& outSkeleton)
{
	outSkeleton.bones.clear();
	outSkeleton.boneNameToIndex.clear();

	// ── Phase 1: Collect all unique bones that have vertex weights (from meshes) ──
	std::unordered_map<std::string, const aiBone*> weightedBones;
	for (uint32_t m = 0; m < scene->mNumMeshes; m++)
	{
		const aiMesh* mesh = scene->mMeshes[m];
		for (uint32_t b = 0; b < mesh->mNumBones; b++)
		{
			const aiBone* bone = mesh->mBones[b];
			std::string boneName = bone->mName.C_Str();
			if (weightedBones.find(boneName) == weightedBones.end())
				weightedBones[boneName] = bone;
		}
	}

	// ── Phase 2: For each weighted bone, walk UP the aiNode tree and collect ──
	//    all ancestor nodes until we reach the scene root. This ensures
	//    hierarchy-only bones (like "Bip01") that have no direct vertex
	//    weights but serve as parents in the bone chain are included.
	std::unordered_set<std::string> allBoneNames;
	for (const auto& [name, _] : weightedBones)
		allBoneNames.insert(name);

	// Also collect ancestor names
	for (const auto& [name, _] : weightedBones)
	{
		const aiNode* node = FindAiNodeByName(scene->mRootNode, name);
		if (!node) continue;

		// Walk up from parent (the bone itself is already in the set)
		const aiNode* ancestor = node->mParent;
		while (ancestor && ancestor != scene->mRootNode)
		{
			std::string ancestorName = ancestor->mName.C_Str();
			if (!ancestorName.empty())
				allBoneNames.insert(ancestorName);
			ancestor = ancestor->mParent;
		}
	}

	// Also add bones that are referenced by animation channels but have no weights
	if (scene->HasAnimations())
	{
		for (uint32_t a = 0; a < scene->mNumAnimations; a++)
		{
			const aiAnimation* anim = scene->mAnimations[a];
			for (uint32_t c = 0; c < anim->mNumChannels; c++)
			{
				std::string channelName = anim->mChannels[c]->mNodeName.C_Str();
				if (allBoneNames.count(channelName))
					continue; // already included
				// Only add if it's an ancestor or descendant of an existing bone
				const aiNode* node = FindAiNodeByName(scene->mRootNode, channelName);
				if (node)
					allBoneNames.insert(channelName);
			}
		}
	}

	// ── Phase 3: Create BoneInfo for each bone name ──
	int boneIndex = 0;
	for (const auto& name : allBoneNames)
	{
		BoneInfo info;
		info.id           = boneIndex;
		info.name         = name;
		info.parentIndex  = -1;  // resolved below

		// Use the offset matrix from mesh bone data if available (most accurate).
		// For hierarchy-only bones, compute from the node's accumulated transform.
		auto wbIt = weightedBones.find(name);
		if (wbIt != weightedBones.end())
		{
			info.offsetMatrix = ConvertMat4(wbIt->second->mOffsetMatrix);
		}
		else
		{
			const aiNode* node = FindAiNodeByName(scene->mRootNode, name);
			if (node)
				info.offsetMatrix = ComputeOffsetMatrixFromNode(node);
			else
				info.offsetMatrix = glm::mat4(1.0f);
		}

		outSkeleton.bones.push_back(info);
		outSkeleton.boneNameToIndex[name] = boneIndex;
		boneIndex++;
	}

	// ── Phase 4: Resolve parent-child hierarchy by walking the aiNode tree ──
	BuildHierarchyFromNodeTree(scene->mRootNode, outSkeleton, -1);

	// Store global inverse transform
	outSkeleton.globalInverseTransform = glm::inverse(ConvertMat4(scene->mRootNode->mTransformation));

	// Log which bones are hierarchy-only (no vertex weights)
	int hierarchyOnlyCount = 0;
	for (const auto& bone : outSkeleton.bones)
	{
		if (weightedBones.find(bone.name) == weightedBones.end())
		{
			hierarchyOnlyCount++;
			VANS_LOG("[VansSkinnedMeshLoader] Hierarchy-only bone: \"" << bone.name
			         << "\" (id=" << bone.id << ", parentIndex=" << bone.parentIndex << ")");
		}
	}

	VANS_LOG("[VansSkinnedMeshLoader] Skeleton extracted: " << outSkeleton.bones.size()
	         << " bones (" << hierarchyOnlyCount << " hierarchy-only)");
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
//  Helper: build a map from mesh index (scene->mMeshes[]) to the aiNode that owns it.
//  A mesh can appear in multiple nodes; we record the first one found (depth-first).
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  Helper: walk up the aiNode tree from a given node and return the
//  bone ID of the nearest ancestor whose name matches a bone in the skeleton.
//  Returns -1 if no bone ancestor is found.
// ════════════════════════════════════════════════════════════════

static int FindNearestBoneAncestor(const aiNode* node,
                                   const VansGraphics::Skeleton& skeleton)
{
	// Start from the node itself — its name may already be a bone (e.g. rigid mesh
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

			// FBX files commonly emit bone entries for the full skeleton in every
			// sub-mesh, even when a bone has zero influence on that mesh's vertices.
			// Skip silently — this is expected Assimp behaviour, not an error.
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

	// ── Second pass: rigid-bind fixup for unskinned meshes parented to bones ──
	// Build mesh-index → aiNode map so we can walk up the node tree.
	std::unordered_map<uint32_t, const aiNode*> meshToNode;
	BuildMeshToNodeMap(scene->mRootNode, meshToNode);

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

		// Find the aiNode that owns this mesh, then walk up to find a bone ancestor
		auto nodeIt = meshToNode.find(m);
		if (nodeIt == meshToNode.end())
			continue;

		const aiNode* ownerNode = nodeIt->second;
		int parentBoneID = FindNearestBoneAncestor(ownerNode, skeleton);
		if (parentBoneID < 0)
			continue;   // no bone ancestor found — leave unbound

		// Rigid-bind: assign all vertices to the parent bone with weight 1.0
		for (uint32_t v = start; v < end && v < totalVertexCount; v++)
		{
			outData[v].AddBoneInfluence(parentBoneID, 1.0f);
		}

		VANS_LOG("[VansSkinnedMeshLoader] Rigid-bind fixup: mesh " << m
		         << " (" << mesh->mNumVertices << " vertices) → bone \""
		         << skeleton.bones[parentBoneID].name << "\" (id=" << parentBoneID << ")");
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

// ═══════════════════════════════════════════════════════
//  ExtractExternAnimationClips
//
//  Opens an external FBX file and extracts ONLY animation clips,
//  mapping bone channels to the origin model's skeleton by name.
//  No bone weights are read — those come from the origin model.
//  Clips are cached as .vclip files alongside the external FBX.
// ═══════════════════════════════════════════════════════

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

	// Open the external FBX — only need minimal processing (no tangents, no normals)
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(externFbxPath,
		aiProcess_Triangulate | aiProcess_FlipUVs);

	if (!scene)
	{
		VANS_LOG_ERROR("[VansSkinnedMeshLoader] Failed to load extern animation FBX: "
		               << externFbxPath << " — " << importer.GetErrorString());
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

		if (FileExists(vclipPath))
		{
			Skeleton cachedSkeleton;
			if (VansAnimationClipIO::Load(vclipPath, clip, cachedSkeleton))
			{
				if (cachedSkeleton.bones.size() != originSkeleton.bones.size())
				{
					VANS_LOG_WARN("[VansSkinnedMeshLoader] Cached extern clip bone count ("
					              << cachedSkeleton.bones.size() << ") != origin skeleton ("
					              << originSkeleton.bones.size() << "), re-extracting: " << vclipPath);
					ExtractClipFromAssimp(anim, originSkeleton, clip);
					clip.clipName = clipName;
					VansAnimationClipIO::Save(vclipPath, clip, originSkeleton);
				}
				else
				{
					VANS_LOG("[VansSkinnedMeshLoader] Loaded cached extern clip: " << vclipPath);
				}
			}
			else
			{
				VANS_LOG_WARN("[VansSkinnedMeshLoader] Failed to load cached extern clip, re-extracting: " << vclipPath);
				ExtractClipFromAssimp(anim, originSkeleton, clip);
				clip.clipName = clipName;
				VansAnimationClipIO::Save(vclipPath, clip, originSkeleton);
			}
		}
		else
		{
			// Extract clip using the ORIGIN skeleton so bone indices match
			ExtractClipFromAssimp(anim, originSkeleton, clip);
			clip.clipName = clipName;
			VansAnimationClipIO::Save(vclipPath, clip, originSkeleton);
			VANS_LOG("[VansSkinnedMeshLoader] Extracted and cached extern clip: " << vclipPath);
		}

		outClips.push_back(std::move(clip));
	}

	VANS_LOG("[VansSkinnedMeshLoader] Extern animation import complete: "
	         << outClips.size() << " clip(s) from " << externFbxPath);
	return true;
}
