#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansMesh.h"
#include "VansVKCommandBuffer.h"
#include "../../Util/VansLog.h"
#include "../../AnimationCore/VansSkinnedMeshLoader.h"
#include <iostream>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/matrix3x3.h>
#include <GLM/glm.hpp>
#include <GLM/gtc/packing.hpp>
#include <filesystem>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cctype>

namespace
{
	bool HasMeshGpuUploadTarget(
		VkDevice logic_device,
		VkQueue queue,
		VansGraphics::VansVKCommandBuffer* commandbuffer)
	{
		return logic_device != VK_NULL_HANDLE &&
			queue != VK_NULL_HANDLE &&
			commandbuffer != nullptr;
	}
}

static uint16_t FloatToHalf(float f)
{
	return glm::packHalf1x16(f);
}

// ---------------------------------------------------------------------------
// Helper: extract texture path from aiMaterial for a given texture type.
// Resolves relative paths against the directory containing the source file.
// ---------------------------------------------------------------------------
static std::string ExtractTexturePath(const aiMaterial* mat, aiTextureType type, const std::string& baseDir)
{
	if (mat->GetTextureCount(type) == 0)
		return {};

	aiString aiPath;
	if (mat->GetTexture(type, 0, &aiPath) != AI_SUCCESS)
		return {};

	std::string raw = aiPath.C_Str();
	if (raw.empty())
		return {};

	// Build absolute path relative to the model file directory
	std::filesystem::path texPath(raw);
	if (texPath.is_relative())
		texPath = std::filesystem::path(baseDir) / texPath;

	// Normalise separators
	return texPath.lexically_normal().string();
}

static float Clamp01(float value)
{
	return std::max(0.0f, std::min(1.0f, value));
}

static float RoughnessFromShininess(float shininess)
{
	if (shininess <= 0.0f)
		return 0.5f;
	return std::max(0.045f, std::min(1.0f, std::sqrt(2.0f / (shininess + 2.0f))));
}

static bool ContainsToken(std::string text, const char* token)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
		return static_cast<char>(std::tolower(value));
	});
	return text.find(token) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Helper: build FBXSubmeshMaterialInfo from an aiMaterial.
// ---------------------------------------------------------------------------
static VansGraphics::FBXSubmeshMaterialInfo BuildSubmeshMaterialInfo(const aiScene* scene, const aiMesh* mesh, const std::string& baseDir)
{
	VansGraphics::FBXSubmeshMaterialInfo info;

	if (mesh->mMaterialIndex < scene->mNumMaterials)
	{
		const aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

		aiString matName;
		mat->Get(AI_MATKEY_NAME, matName);
		info.materialName = matName.C_Str();

		// Extract texture paths
		info.diffuseTexPath   = ExtractTexturePath(mat, aiTextureType_BASE_COLOR,         baseDir);
		if (info.diffuseTexPath.empty())
			info.diffuseTexPath = ExtractTexturePath(mat, aiTextureType_DIFFUSE,          baseDir);
		info.normalTexPath    = ExtractTexturePath(mat, aiTextureType_NORMALS,             baseDir);
		if (info.normalTexPath.empty())
			info.normalTexPath = ExtractTexturePath(mat, aiTextureType_HEIGHT,             baseDir);
		info.metallicTexPath  = ExtractTexturePath(mat, aiTextureType_METALNESS,           baseDir);
		info.roughnessTexPath = ExtractTexturePath(mat, aiTextureType_DIFFUSE_ROUGHNESS,   baseDir);
		if (info.roughnessTexPath.empty())
			info.roughnessTexPath = ExtractTexturePath(mat, aiTextureType_SHININESS,       baseDir);
		info.aoTexPath        = ExtractTexturePath(mat, aiTextureType_AMBIENT_OCCLUSION,   baseDir);
		if (info.aoTexPath.empty())
			info.aoTexPath     = ExtractTexturePath(mat, aiTextureType_LIGHTMAP,           baseDir);
		info.opacityTexPath   = ExtractTexturePath(mat, aiTextureType_OPACITY,             baseDir);

		// Scalar parameters
		float val;
		aiColor3D color;
		if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
			info.diffuseColor = { color.r, color.g, color.b };
		if (mat->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS)
			info.specularColor = { color.r, color.g, color.b };
		if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
			info.emissiveColor = { color.r, color.g, color.b };
		if (mat->Get(AI_MATKEY_OPACITY, val) == AI_SUCCESS)
			info.opacity = Clamp01(val);
		if (mat->Get(AI_MATKEY_TRANSPARENCYFACTOR, val) == AI_SUCCESS)
			info.opacity = Clamp01(info.opacity * (1.0f - Clamp01(val)));
		if (mat->Get(AI_MATKEY_METALLIC_FACTOR, val) == AI_SUCCESS)
			info.metallic = Clamp01(val);
		if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, val) == AI_SUCCESS)
			info.roughness = std::max(0.045f, Clamp01(val));
		if (mat->Get(AI_MATKEY_SHININESS, val) == AI_SUCCESS)
		{
			info.shininess = val;
			float roughnessCheck;
			if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessCheck) != AI_SUCCESS)
				info.roughness = RoughnessFromShininess(info.shininess);
		}
		if (mat->Get(AI_MATKEY_SHININESS_STRENGTH, val) == AI_SUCCESS)
			info.specularFactor = Clamp01(val);
		if (mat->Get(AI_MATKEY_REFLECTIVITY, val) == AI_SUCCESS)
			info.reflectionFactor = Clamp01(val);

		if (info.metallic <= 0.0f &&
			(ContainsToken(info.materialName, "metal") ||
			 ContainsToken(info.materialName, "chrome") ||
			 ContainsToken(info.materialName, "metallic")))
		{
			info.metallic = 1.0f;
			info.roughness = std::min(info.roughness, 0.35f);
		}
	}

	return info;
}

// ---------------------------------------------------------------------------
// Helper structs and functions for multi-mesh loading
// ---------------------------------------------------------------------------

struct CollectedAiMesh
{
	aiMesh* mesh;
	uint32_t meshIndex;
	aiMatrix4x4 transform;
	std::string nodeName;
	std::string nodePath;
};

static aiVector3D TransformPosition(const aiMatrix4x4& transform, const aiVector3D& position)
{
	return transform * position;
}

static aiVector3D TransformDirection(const aiMatrix4x4& transform, const aiVector3D& direction)
{
	aiMatrix3x3 normalMatrix(transform);
	normalMatrix.Inverse().Transpose();
	aiVector3D transformed = normalMatrix * direction;
	if (transformed.SquareLength() > 0.0f)
	{
		transformed.Normalize();
	}
	return transformed;
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

static glm::mat4 ConvertAiMatrixToGlmScaled(const aiMatrix4x4& transform, float scaleFactor)
{
	glm::mat4 result(
		transform.a1, transform.b1, transform.c1, transform.d1,
		transform.a2, transform.b2, transform.c2, transform.d2,
		transform.a3, transform.b3, transform.c3, transform.d3,
		transform.a4, transform.b4, transform.c4, transform.d4
	);
	result[3].x *= scaleFactor;
	result[3].y *= scaleFactor;
	result[3].z *= scaleFactor;
	return result;
}

static bool IsNearlyIdentityMatrix(const aiMatrix4x4& transform, float epsilon = 1.0e-4f)
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

static std::string ToLowerAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

static bool ShouldBakeSkinnedMeshNodeTransform(const Vans::VansSkeletalMeshImportSettings& skeletalImport)
{
	const std::string policy = ToLowerAscii(skeletalImport.meshNodeTransformPolicy);
	return policy == "bakeskinned" ||
		policy == "bakeskinnedmeshes" ||
		policy == "bakeall" ||
		policy == "bakenodetransform";
}

static bool IsAutoMeshNodeTransformPolicy(const Vans::VansSkeletalMeshImportSettings& skeletalImport)
{
	const std::string policy = ToLowerAscii(skeletalImport.meshNodeTransformPolicy);
	return policy.empty() ||
		policy == "auto" ||
		policy == "automatic";
}

static bool ShouldBakeSkinnedMeshNodeTransform(
	const Vans::VansSkeletalMeshImportSettings& skeletalImport,
	const std::vector<CollectedAiMesh>& meshes)
{
	if (ShouldBakeSkinnedMeshNodeTransform(skeletalImport))
		return true;

	if (!IsAutoMeshNodeTransformPolicy(skeletalImport))
		return false;

	for (const CollectedAiMesh& collectedMesh : meshes)
	{
		if (collectedMesh.mesh &&
			collectedMesh.mesh->mNumBones > 0 &&
			!IsNearlyIdentityMatrix(collectedMesh.transform))
		{
			return true;
		}
	}

	return false;
}

static bool HasNodeTransformChannelForMesh(
	const VansGraphics::VansAnimationImportResult& animImport,
	const CollectedAiMesh& collectedMesh)
{
	for (const VansGraphics::VansAnimationClip& clip : animImport.clips)
	{
		for (const VansGraphics::NodeTransformChannel& channel : clip.nodeTransformChannels)
		{
			if (!collectedMesh.nodePath.empty() && channel.nodePath == collectedMesh.nodePath)
				return true;
			if (!collectedMesh.nodeName.empty() && channel.nodeName == collectedMesh.nodeName)
				return true;
		}
	}
	return false;
}

// Recursively search the node tree for a node whose name matches exactly.
static bool FindNodeByName(const aiNode* node, const std::string& targetName)
{
	if (!node) return false;
	if (std::string(node->mName.C_Str()) == targetName)
		return true;
	for (uint32_t i = 0; i < node->mNumChildren; i++)
		if (FindNodeByName(node->mChildren[i], targetName))
			return true;
	return false;
}

// Walk the node tree and collect every aiMesh* with its accumulated node transform.
// Only collects meshes from nodes that have a non-empty name.
static void CollectAiMeshes(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform, const std::string& parentPath, std::vector<CollectedAiMesh>& out)
{
	aiMatrix4x4 accumulatedTransform = parentTransform * node->mTransformation;

	std::string name = node->mName.C_Str();
	std::string nodePath = parentPath.empty() ? name : parentPath + "/" + name;
	if (!name.empty() && node->mNumMeshes > 0)
	{
		for (uint32_t i = 0; i < node->mNumMeshes; i++)
		{
			out.push_back({ scene->mMeshes[node->mMeshes[i]], node->mMeshes[i], accumulatedTransform, name, nodePath });
		}
	}
	for (uint32_t i = 0; i < node->mNumChildren; i++)
		CollectAiMeshes(node->mChildren[i], scene, accumulatedTransform, nodePath, out);
}

// ===========================================================================
// VansMesh multi-mesh / submesh / animation-bone method implementations
// ===========================================================================

void VansGraphics::VansMesh::LoadMultiMesh(VkDevice& logic_device, VkQueue& queue,
	VansVKCommandBuffer* commandbuffer, const std::string& file_name, bool import_tangent,
	bool supportRayTracing, bool needCPUData, float scaleFactor,
	const Vans::VansSkeletalMeshImportSettings& skeletalImport,
	const std::string& cachePath,
	bool trustCacheWithoutSource)
{
	m_IsMultiMesh = true;
	m_SupportRayTracing = false;
	m_LogicalDevice = logic_device;
	m_MeshRawPositionDataEnableCPURead = needCPUData;

	if (trustCacheWithoutSource &&
		TryLoadMeshCache(logic_device, queue, commandbuffer,
			cachePath, file_name, import_tangent, supportRayTracing,
			needCPUData, true, scaleFactor, trustCacheWithoutSource))
	{
		return;
	}

	Assimp::Importer importer;
	auto processFlag = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_GenSmoothNormals;
	if (import_tangent)
	{
		processFlag |= aiProcess_CalcTangentSpace;
	}
	const aiScene* scene = importer.ReadFile(file_name, processFlag);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		VANS_LOG_ERROR("ERROR::ASSIMP (LoadMultiMesh)::" << importer.GetErrorString());
		return;
	}

	// Derive base directory for resolving relative texture paths
	std::string baseDir = std::filesystem::path(file_name).parent_path().string();

	aiMatrix4x4 identityTransform = MakeIdentityAiMatrix();
	std::vector<CollectedAiMesh> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, identityTransform, "", allMeshes);
	if (allMeshes.empty())
	{
		VANS_LOG_WARN("[LoadMultiMesh] No submeshes found in: " << file_name);
		return;
	}


	// Auto-detect skeletal rigs. Animation clips are optional; bones alone are sufficient.
	// Count total vertices across the canonical scene mesh list (matches ExtractVertexBoneData)
	bool sceneHasBones = false;
	for (uint32_t m = 0; m < scene->mNumMeshes && !sceneHasBones; m++)
		if (scene->mMeshes[m]->mNumBones > 0)
			sceneHasBones = true;

	if (!sceneHasBones && !scene->HasAnimations() &&
		TryLoadMeshCache(logic_device, queue, commandbuffer,
			cachePath, file_name, import_tangent, supportRayTracing,
			needCPUData, true, scaleFactor, trustCacheWithoutSource))
	{
		return;
	}

	if (sceneHasBones || scene->HasAnimations())
	{
		uint32_t totalVertices = 0;
		for (uint32_t m = 0; m < scene->mNumMeshes; m++)
			totalVertices += scene->mMeshes[m]->mNumVertices;

		VansSkinnedMeshLoader::ProcessAnimatedMesh(scene, file_name, totalVertices, scaleFactor, m_AnimImportResult,
			skeletalImport);

		if (m_AnimImportResult.hasAnimation)
		{
			m_HasAnimation = true;
			VANS_LOG("[LoadMultiMesh] Skeletal rig detected: "
				<< m_AnimImportResult.skeleton.bones.size() << " bones, "
				<< m_AnimImportResult.clips.size() << " clip(s)"
				<< (scene->HasAnimations() ? "" : " (bind-pose only, no animation clips)"));

			// Build a mesh-pointer to vertex-offset map aligned with ExtractVertexBoneData's
			// scene->mMeshes[] iteration order.
			std::unordered_map<const aiMesh*, uint32_t> meshVertexOffset;
			{
				uint32_t off = 0;
				for (uint32_t m = 0; m < scene->mNumMeshes; m++)
				{
					meshVertexOffset[scene->mMeshes[m]] = off;
					off += scene->mMeshes[m]->mNumVertices;
				}
			}

			// Record per-slice vertex offsets (used by the render node to push the offset
			// into the shader so it can index VansAnimationNode's bone weight buffer correctly).
			// m_SubMeshBoneData is still populated for reference.
			// The actual GPU upload happens in ExpandMultiMeshToRenderNodes after
			// VansAnimationNode is created and InitGPUResources has been called.
			const auto& vbd = m_AnimImportResult.vertexBoneData;
			for (size_t i = 0; i < allMeshes.size(); ++i)
			{
				const aiMesh* mesh = allMeshes[i].mesh;
				auto it = meshVertexOffset.find(mesh);
				uint32_t start = (it != meshVertexOffset.end()) ? it->second : 0;
				uint32_t end   = start + mesh->mNumVertices;
				if (end <= static_cast<uint32_t>(vbd.size()))
					m_SubMeshBoneData.emplace_back(vbd.begin() + start, vbd.begin() + end);
				else
					m_SubMeshBoneData.emplace_back();
			}

			// External animation: replace origin clips with clips from an external FBX.
			// Only animation clips are read from the extern file; bone weights and
			// skeleton come from the origin model. The extern clips are mapped to
			// the origin skeleton by bone name.
			if (!m_ExternAnimationPath.empty())
			{
				std::vector<VansAnimationClip> externClips;
				if (VansSkinnedMeshLoader::ExtractExternAnimationClips(
					m_ExternAnimationPath, m_AnimImportResult.skeleton, externClips))
				{
					// Replace origin clips with extern clips
					m_AnimImportResult.clips = std::move(externClips);
					VANS_LOG("[LoadMultiMesh] Replaced origin animation clips with "
						<< m_AnimImportResult.clips.size()
						<< " extern clip(s) from: " << m_ExternAnimationPath);
				}
				else
				{
					VANS_LOG_WARN("[LoadMultiMesh] Failed to load extern animation from: "
						<< m_ExternAnimationPath << ". Keeping origin clips.");
				}
			}

		}
	}

	m_HasNodeTransformAnimation = false;
	for (const VansAnimationClip& clip : m_AnimImportResult.clips)
	{
		if (!clip.nodeTransformChannels.empty())
		{
			m_HasNodeTransformAnimation = true;
			break;
		}
	}

	const bool canWriteStaticCache = !cachePath.empty() && !sceneHasBones && !scene->HasAnimations();
	const bool bakeSkinnedMeshNodeTransform =
		ShouldBakeSkinnedMeshNodeTransform(skeletalImport, allMeshes);
	for (size_t i = 0; i < allMeshes.size(); ++i)
	{
		const CollectedAiMesh& collectedMesh = allMeshes[i];
		const bool hasNodeTransformChannel =
			HasNodeTransformChannelForMesh(m_AnimImportResult, collectedMesh);
		// If animated + skinned (has bones): the default policy preserves Assimp's
		//   vertex/bind space. Some multi-skinned-mesh FBX files keep vertices in
		//   each mesh node's local space; "bakeSkinned" normalizes those vertices
		//   into model space and the skeleton loader adjusts inverse-bind offsets
		//   to match.
		// If animated + unskinned (no bones, rigid-bind child of a bone): bake the accumulated
		//   node transform so vertices end up in the same model-space as skinned verts.
		// If static: always bake the node transform.
		const aiMatrix4x4* xform;
		if (hasNodeTransformChannel)
			xform = nullptr;
		else if (m_HasAnimation)
			xform = (collectedMesh.mesh->mNumBones == 0 || bakeSkinnedMeshNodeTransform) ? &collectedMesh.transform : nullptr;
		else
			xform = &collectedMesh.transform;

		if (skeletalImport.diagnostics && sceneHasBones)
		{
			VANS_LOG("[SkeletalImport] Submesh[" << i
				<< "] aiMesh=" << collectedMesh.meshIndex
				<< " node=\"" << collectedMesh.nodeName << "\""
				<< " vertices=" << collectedMesh.mesh->mNumVertices
				<< " bones=" << collectedMesh.mesh->mNumBones
				<< " nodeTransform="
				<< (IsNearlyIdentityMatrix(collectedMesh.transform) ? "identity" : "nonIdentity")
				<< " bakedNodeTransform=" << (xform != nullptr ? "true" : "false"));
		}

		VansMesh* slice = new VansMesh(needCPUData, supportRayTracing);
		if (slice->LoadMeshSubmeshFromScene(logic_device, queue, commandbuffer, scene, collectedMesh.mesh, xform, import_tangent, supportRayTracing, scaleFactor, canWriteStaticCache))
		{
			slice->m_SourceNodeName = collectedMesh.nodeName;
			slice->m_SourceNodePath = collectedMesh.nodePath;
			slice->m_SourceNodeBindModelTransform =
				ConvertAiMatrixToGlmScaled(collectedMesh.transform, scaleFactor);
			slice->m_HasNodeTransformAnimation = hasNodeTransformChannel;
			m_SubMeshes.push_back(slice);
			m_SubmeshMaterialInfos.push_back(BuildSubmeshMaterialInfo(scene, collectedMesh.mesh, baseDir));
		}
		else
		{
			delete slice;
		}
	}

	if (canWriteStaticCache && SaveMeshCache(cachePath, file_name, import_tangent, true, scaleFactor))
	{
		for (VansMesh* slice : m_SubMeshes)
			if (slice)
				slice->ReleaseCpuImportDataAfterUpload();
	}

	VANS_LOG("[LoadMultiMesh] Loaded " << m_SubMeshes.size() << " submeshes from: " << file_name);
}

bool VansGraphics::VansMesh::LoadMeshSubmesh(VkDevice& logic_device, VkQueue& queue,
	VansVKCommandBuffer* commandbuffer, const std::string& file_name,
	uint32_t submeshIndex, bool import_tangent, float scaleFactor)
{
	VANS_LOG("Load Submesh [" << submeshIndex << "] from : " << file_name);

	m_LogicalDevice = logic_device;
	m_MeshRawDataCPULoaded = false;
	m_VertexCount = 0;
	m_IsSubmesh = true;
	m_SupportRayTracing = false;

	Assimp::Importer importer;
	auto processFlag = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_GenSmoothNormals;
	if (import_tangent)
		processFlag |= aiProcess_CalcTangentSpace;

	const aiScene* scene = importer.ReadFile(file_name, processFlag);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		VANS_LOG_ERROR("ERROR::ASSIMP (LoadMeshSubmesh)::" << importer.GetErrorString());
		return false;
	}

	// Collect all aiMesh* in traversal order
	aiMatrix4x4 identityTransform = MakeIdentityAiMatrix();
	std::vector<CollectedAiMesh> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, identityTransform, "", allMeshes);

	if (submeshIndex >= static_cast<uint32_t>(allMeshes.size()))
	{
		VANS_LOG_ERROR("ERROR: submeshIndex " << submeshIndex << " out of range (total=" << allMeshes.size() << ")");
		return false;
	}

	const CollectedAiMesh& collectedMesh = allMeshes[submeshIndex];
	return LoadMeshSubmeshFromScene(logic_device, queue, commandbuffer, scene, collectedMesh.mesh, &collectedMesh.transform, import_tangent, false, scaleFactor);
}

bool VansGraphics::VansMesh::LoadMeshSubmeshFromScene(VkDevice& logic_device, VkQueue& queue,
	VansVKCommandBuffer* commandbuffer, const aiScene* scene, aiMesh* mesh, const aiMatrix4x4* meshTransform, bool import_tangent, bool supportRayTracing, float scaleFactor, bool keepImportDataAfterUpload)
{
	if (!scene || !mesh)
	{
		return false;
	}

	aiMatrix4x4 transform = MakeIdentityAiMatrix();
	if (meshTransform)
	{
		transform = *meshTransform;
	}
	aiMatrix3x3 normalTransform(transform);
	normalTransform.Inverse().Transpose();

	// reset state per submesh
	m_MeshRawData.clear();
	m_MeshRawPositionData.clear();
	m_MeshTriangleIndex.clear();
	m_VertexInputAttributeDescriptions.clear();
	m_VertexInputBindingDescriptions.clear();
	ResetLocalBounds();

	m_LogicalDevice = logic_device;
	m_MeshRawDataCPULoaded = false;
	m_VertexCount = 0;
	m_IsSubmesh = true;
	m_SupportRayTracing = supportRayTracing;

	// Store source material name
	if (mesh->mMaterialIndex < scene->mNumMaterials)
	{
		aiString matName;
		scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME, matName);
		m_SourceMaterialName = matName.C_Str();
	}

	// Pack vertices (indices always start at 0 for a single-mesh load)
	for (uint32_t i = 0; i < mesh->mNumVertices; i++)
	{
		aiVector3D vertex  = mesh->mVertices[i];
		aiVector3D normal  = mesh->mNormals ? mesh->mNormals[i] : aiVector3D(0, 1, 0);
		vertex = TransformPosition(transform, vertex);
		vertex *= scaleFactor;
		ExpandLocalBounds(glm::vec3(vertex.x, vertex.y, vertex.z));
		normal = TransformDirection(transform, normal);
		aiVector3D texCoord(0, 0, 0);
		if (mesh->mTextureCoords[0])
			texCoord = mesh->mTextureCoords[0][i];

		m_MeshRawData.emplace_back(FloatToHalf(vertex.x));
		m_MeshRawData.emplace_back(FloatToHalf(vertex.y));
		m_MeshRawData.emplace_back(FloatToHalf(vertex.z));

		m_MeshRawPositionData.emplace_back(vertex.x);
		m_MeshRawPositionData.emplace_back(vertex.y);
		m_MeshRawPositionData.emplace_back(vertex.z);
		m_MeshRawPositionData.emplace_back(0.0f);

		m_MeshRawData.emplace_back(FloatToHalf(texCoord.x));
		m_MeshRawData.emplace_back(FloatToHalf(texCoord.y));
		m_MeshRawData.emplace_back(FloatToHalf(normal.x));
		m_MeshRawData.emplace_back(FloatToHalf(normal.y));
		m_MeshRawData.emplace_back(FloatToHalf(normal.z));

		m_MeshRawPositionData.emplace_back(normal.x);
		m_MeshRawPositionData.emplace_back(normal.y);
		m_MeshRawPositionData.emplace_back(normal.z);
		m_MeshRawPositionData.emplace_back(0.0f);

		if (import_tangent)
		{
			aiVector3D tangent(0, 0, 0), bitangent(0, 0, 0);
			if (mesh->mTangents)   tangent   = mesh->mTangents[i];
			if (mesh->mBitangents) bitangent = mesh->mBitangents[i];

			// If tangent length is zero (degenerate UVs or missing tangent data), rebuild an
			// orthonormal tangent frame from the normal to avoid GPU NaNs after normalize().
			if (tangent.SquareLength() < 1e-8f)
			{
				aiVector3D n = mesh->mNormals ? mesh->mNormals[i] : aiVector3D(0.f, 1.f, 0.f);
				// Choose a reference axis that is not parallel to n.
				aiVector3D up = (std::abs(n.y) < 0.999f) ? aiVector3D(0.f, 1.f, 0.f) : aiVector3D(1.f, 0.f, 0.f);
				// tangent = cross(up, n)
				tangent = aiVector3D(
					up.y * n.z - up.z * n.y,
					up.z * n.x - up.x * n.z,
					up.x * n.y - up.y * n.x);
				tangent.NormalizeSafe();
				// bitangent = cross(n, tangent)
				bitangent = aiVector3D(
					n.y * tangent.z - n.z * tangent.y,
					n.z * tangent.x - n.x * tangent.z,
					n.x * tangent.y - n.y * tangent.x);
			}

			tangent = TransformDirection(transform, tangent);
			bitangent = TransformDirection(transform, bitangent);
			m_MeshRawData.emplace_back(FloatToHalf(tangent.x));
			m_MeshRawData.emplace_back(FloatToHalf(tangent.y));
			m_MeshRawData.emplace_back(FloatToHalf(tangent.z));
			m_MeshRawData.emplace_back(FloatToHalf(bitangent.x));
			m_MeshRawData.emplace_back(FloatToHalf(bitangent.y));
			m_MeshRawData.emplace_back(FloatToHalf(bitangent.z));
		}
	}
	m_VertexCount = mesh->mNumVertices;
	RebuildLocalBoundsFromRawPositions();

	for (uint32_t i = 0; i < mesh->mNumFaces; i++)
	{
		m_MeshTriangleIndex.push_back(mesh->mFaces[i].mIndices[0]);
		m_MeshTriangleIndex.push_back(mesh->mFaces[i].mIndices[1]);
		m_MeshTriangleIndex.push_back(mesh->mFaces[i].mIndices[2]);
	}
	m_MeshRawDataCPULoaded = true;
	m_IndexCount = static_cast<int>(m_MeshTriangleIndex.size());

	m_VertexDataSize = 8 * sizeof(uint16_t);
	if (import_tangent)
		m_VertexDataSize += 6 * sizeof(uint16_t);

	m_VertexInputBindingDescriptions =
	{
		{ 0, m_VertexDataSize, VK_VERTEX_INPUT_RATE_VERTEX }
	};
	m_VertexInputAttributeDescriptions =
	{
		{ 0, 0, VK_FORMAT_R16G16B16_SFLOAT, 0 },
		{ 1, 0, VK_FORMAT_R16G16_SFLOAT,    3 * sizeof(uint16_t) },
		{ 2, 0, VK_FORMAT_R16G16B16_SFLOAT, 5 * sizeof(uint16_t) }
	};
	if (import_tangent)
	{
		m_VertexInputAttributeDescriptions.push_back({ 3, 0, VK_FORMAT_R16G16B16_SFLOAT, 8  * sizeof(uint16_t) });
		m_VertexInputAttributeDescriptions.push_back({ 4, 0, VK_FORMAT_R16G16B16_SFLOAT, 11 * sizeof(uint16_t) });
	}

	if (!HasMeshGpuUploadTarget(logic_device, queue, commandbuffer))
		return true;

	return UploadRawMeshToGpu(logic_device, queue, commandbuffer,
		"LoadMeshSubmeshFromScene", keepImportDataAfterUpload);

}

std::vector<std::string> VansGraphics::VansMesh::GetSubmeshMaterialNames(const std::string& file_name)
{
	std::vector<std::string> result;

	Assimp::Importer importer;
	// No tangent calculation or UV flip needed; this path only reads material names.
	const aiScene* scene = importer.ReadFile(file_name,
		aiProcess_Triangulate | aiProcess_GenNormals);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		VANS_LOG_ERROR("ERROR::ASSIMP (GetSubmeshMaterialNames)::" << importer.GetErrorString());
		return result;
	}

	aiMatrix4x4 identityTransform = MakeIdentityAiMatrix();
	std::vector<CollectedAiMesh> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, identityTransform, "", allMeshes);

	result.reserve(allMeshes.size());
	for (const CollectedAiMesh& collectedMesh : allMeshes)
	{
		std::string matName;
		if (collectedMesh.mesh->mMaterialIndex < scene->mNumMaterials)
		{
			aiString aiMatName;
			scene->mMaterials[collectedMesh.mesh->mMaterialIndex]->Get(AI_MATKEY_NAME, aiMatName);
			matName = aiMatName.C_Str();
		}
		result.push_back(matName);
	}

	return result;
}

std::vector<VansGraphics::FBXSubmeshMaterialInfo> VansGraphics::VansMesh::GetSubmeshMaterialInfos(const std::string& file_name)
{
	std::vector<FBXSubmeshMaterialInfo> result;

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(file_name,
		aiProcess_Triangulate | aiProcess_GenNormals);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		VANS_LOG_ERROR("ERROR::ASSIMP (GetSubmeshMaterialInfos)::" << importer.GetErrorString());
		return result;
	}

	std::string baseDir = std::filesystem::path(file_name).parent_path().string();

	aiMatrix4x4 identityTransform = MakeIdentityAiMatrix();
	std::vector<CollectedAiMesh> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, identityTransform, "", allMeshes);

	result.reserve(allMeshes.size());
	for (const CollectedAiMesh& collectedMesh : allMeshes)
	{
		result.push_back(BuildSubmeshMaterialInfo(scene, collectedMesh.mesh, baseDir));
	}

	return result;
}

uint32_t VansGraphics::VansMesh::ProbeSubmeshCount(const std::string& file_name)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(file_name,
		aiProcess_Triangulate | aiProcess_GenNormals);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
		return 0;

	aiMatrix4x4 identityTransform = MakeIdentityAiMatrix();
	std::vector<CollectedAiMesh> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, identityTransform, "", allMeshes);
	return static_cast<uint32_t>(allMeshes.size());
}
