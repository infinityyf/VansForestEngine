#pragma once

#include "VansAnimationTypes.h"
#include <string>

struct aiScene;
struct aiAnimation;
struct aiNode;

namespace VansGraphics
{
	class VansMesh;

	// ────────────────────────────────────────────────────────────────
	//  VansSkinnedMeshLoader
	//
	//  Extension to the Assimp import pipeline. After Assimp loads an FBX/glTF file,
	//  this class extracts skeletal animation data:
	//    - Skeleton (bone hierarchy + offset matrices)
	//    - Per-vertex bone influences (IDs + weights)
	//    - Animation clips (one per aiAnimation)
	//
	//  It also handles .vclip caching: if cached clips exist on disk next to the FBX,
	//  they are loaded directly instead of re-extracting from Assimp.
	// ────────────────────────────────────────────────────────────────

	class VansSkinnedMeshLoader
	{
	public:
		// Main entry point: process an already-loaded aiScene for animation data.
		// If the model has animations, fills outResult with skeleton + clips + bone weights.
		// If cached .vclip files exist, loads from those instead of extracting.
		static bool ProcessAnimatedMesh(const aiScene* scene,
		                                const std::string& fbxFilePath,
		                                uint32_t totalVertexCount,
		                                VansAnimationImportResult& outResult);

		// Extract skeleton from the aiScene (bone hierarchy, offset matrices, parent-child).
		static void ExtractSkeleton(const aiScene* scene, Skeleton& outSkeleton);

		// Extract per-vertex bone data (IDs + weights) from all meshes in the scene.
		static void ExtractVertexBoneData(const aiScene* scene,
		                                  const Skeleton& skeleton,
		                                  uint32_t totalVertexCount,
		                                  std::vector<VertexBoneData>& outData);

		// Extract a single animation clip from an aiAnimation.
		static void ExtractClipFromAssimp(const aiAnimation* anim,
		                                  const Skeleton& skeleton,
		                                  VansAnimationClip& outClip);

		// Load animation clips from an external FBX file, mapping bone channels
		// to an existing (origin model) skeleton. Only animation data is extracted
		// — no bone weights. Clips are cached as .vclip files alongside the
		// external FBX. Returns true if at least one clip was extracted.
		static bool ExtractExternAnimationClips(
		    const std::string& externFbxPath,
		    const Skeleton& originSkeleton,
		    std::vector<VansAnimationClip>& outClips);

	private:
		// Recursively walk aiNode tree to resolve bone parent-child hierarchy.
		static void BuildHierarchyFromNodeTree(const aiNode* node,
		                                       Skeleton& skeleton,
		                                       int parentIndex = -1);

		// Utility: get parent directory from a file path.
		static std::string GetParentDirectory(const std::string& filePath);

		// Utility: get base filename without extension.
		static std::string GetFileBaseName(const std::string& filePath);

		// Utility: check if a file exists on disk.
		static bool FileExists(const std::string& filePath);
	};

}  // namespace VansGraphics
