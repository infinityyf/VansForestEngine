#pragma once

#include "VansAnimationTypes.h"
#include "../AssetCore/VansSkeletalMeshImportSettings.h"
#include <string>

struct aiScene;
struct aiAnimation;
struct aiNode;

namespace VansGraphics
{
	class VansMesh;

	// -----------------------------------------------------------------------
	// VansSkinnedMeshLoader
	//
	// Extension to the Assimp import pipeline. After Assimp loads an FBX/glTF
	// file, this class extracts skeletal animation data:
	//   - Skeleton (bone hierarchy + offset matrices)
	//   - Per-vertex bone influences (IDs + weights)
	//   - Animation clips (one per aiAnimation)
	//
	// Serialized .vclip publication is intentionally outside this decoder. Model
	// loading and preview extraction produce in-memory results and never write assets.
	// -----------------------------------------------------------------------

	class VansSkinnedMeshLoader
	{
	public:
		// 从原始模型资源构建 Skeleton。模型 .meta 必须由项目/导入边界预先解析为
		// 内存设置；AnimationCore 不得自行访问配置文件。
		static bool LoadSkeletonFromModelAsset(
			const std::string& modelPath,
			const Vans::VansSkeletalMeshImportSettings& importSettings,
			Skeleton& outSkeleton,
			std::string& error);

		// Main entry point: process an already-loaded aiScene for animation data.
		// If the model has animations, fills outResult with skeleton + clips + bone weights.
		static bool ProcessAnimatedMesh(const aiScene* scene,
		                                const std::string& fbxFilePath,
		                                uint32_t totalVertexCount,
		                                float scaleFactor,
		                                VansAnimationImportResult& outResult,
		                                const Vans::VansSkeletalMeshImportSettings& importSettings =
		                                    Vans::VansSkeletalMeshImportSettings{});

		// Extract skeleton from the aiScene (bone hierarchy, offset matrices, parent-child).
		static void ExtractSkeleton(const aiScene* scene,
		                            Skeleton& outSkeleton,
		                            float scaleFactor = 1.0f,
		                            const Vans::VansSkeletalMeshImportSettings& importSettings =
		                                Vans::VansSkeletalMeshImportSettings{});

		// Extract per-vertex bone data (IDs + weights) from all meshes in the scene.
		static void ExtractVertexBoneData(const aiScene* scene,
		                                  const Skeleton& skeleton,
		                                  uint32_t totalVertexCount,
		                                  std::vector<VertexBoneData>& outData,
		                                  const Vans::VansSkeletalMeshImportSettings& importSettings =
		                                      Vans::VansSkeletalMeshImportSettings{});

		// Extract a single animation clip from an aiAnimation.
		static void ExtractClipFromAssimp(const aiAnimation* anim,
		                                  const Skeleton& skeleton,
		                                  VansAnimationClip& outClip,
		                                  const aiScene* scene = nullptr,
		                                  float scaleFactor = 1.0f);

		// Load animation clips from an external FBX file, mapping bone channels
		// to an existing origin-model skeleton. Only animation data is extracted,
		// without bone weights. Returns true if at least one clip was extracted.
		// The caller decides whether and when to serialize the result.
		static bool ExtractExternAnimationClips(
		    const std::string& externFbxPath,
		    const Skeleton& originSkeleton,
		    std::vector<VansAnimationClip>& outClips);

	private:
		// Utility: get base filename without extension.
		static std::string GetFileBaseName(const std::string& filePath);
	};

}  // namespace VansGraphics
