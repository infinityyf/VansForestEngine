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
		info.diffuseTexPath   = ExtractTexturePath(mat, aiTextureType_DIFFUSE,            baseDir);
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
		if (mat->Get(AI_MATKEY_OPACITY, val) == AI_SUCCESS)
			info.opacity = val;
		if (mat->Get(AI_MATKEY_METALLIC_FACTOR, val) == AI_SUCCESS)
			info.metallic = val;
		if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, val) == AI_SUCCESS)
			info.roughness = val;
	}

	return info;
}

// ---------------------------------------------------------------------------
// Helper structs and functions for multi-mesh loading
// ---------------------------------------------------------------------------

struct CollectedAiMesh
{
	aiMesh* mesh;
	aiMatrix4x4 transform;
	std::string nodeName;
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

// ─── Recursively search the node tree for a node whose name matches exactly ─────
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

// ─── Walk node tree and collect every aiMesh* with accumulated node transform ─
// Only collects meshes from nodes that have a non-empty name.
static void CollectAiMeshes(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform, std::vector<CollectedAiMesh>& out)
{
	aiMatrix4x4 accumulatedTransform = parentTransform * node->mTransformation;

	std::string name = node->mName.C_Str();
	if (!name.empty() && node->mNumMeshes > 0)
	{
		for (uint32_t i = 0; i < node->mNumMeshes; i++)
		{
			out.push_back({ scene->mMeshes[node->mMeshes[i]], accumulatedTransform, name });
		}
	}
	for (uint32_t i = 0; i < node->mNumChildren; i++)
		CollectAiMeshes(node->mChildren[i], scene, accumulatedTransform, out);
}

// ===========================================================================
// VansMesh multi-mesh / submesh / animation-bone method implementations
// ===========================================================================

void VansGraphics::VansMesh::LoadMultiMesh(VkDevice& logic_device, VkQueue& queue,
	VansVKCommandBuffer* commandbuffer, const std::string& file_name, bool import_tangent,
	bool supportRayTracing, bool needCPUData)
{
	m_IsMultiMesh = true;
	m_SupportRayTracing = false;

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

	aiMatrix4x4 identityTransform;
	std::vector<CollectedAiMesh> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, identityTransform, allMeshes);
	if (allMeshes.empty())
	{
		VANS_LOG_WARN("[LoadMultiMesh] No submeshes found in: " << file_name);
		return;
	}


	// ── Auto-detect skeletal rig (animations are optional – bones alone are sufficient) ──
	// Count total vertices across the canonical scene mesh list (matches ExtractVertexBoneData)
	bool sceneHasBones = false;
	for (uint32_t m = 0; m < scene->mNumMeshes && !sceneHasBones; m++)
		if (scene->mMeshes[m]->mNumBones > 0)
			sceneHasBones = true;

	if (sceneHasBones)
	{
		uint32_t totalVertices = 0;
		for (uint32_t m = 0; m < scene->mNumMeshes; m++)
			totalVertices += scene->mMeshes[m]->mNumVertices;

		VansSkinnedMeshLoader::ProcessAnimatedMesh(scene, file_name, totalVertices, m_AnimImportResult);

		if (m_AnimImportResult.hasAnimation)
		{
			m_HasAnimation = true;
			VANS_LOG("[LoadMultiMesh] Skeletal rig detected: "
				<< m_AnimImportResult.skeleton.bones.size() << " bones, "
				<< m_AnimImportResult.clips.size() << " clip(s)"
				<< (scene->HasAnimations() ? "" : " (bind-pose only, no animation clips)"));

			// Build mesh-pointer → vertex-offset map aligned with ExtractVertexBoneData's
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

			// ── External animation: replace origin clips with clips from extern FBX ──
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

	for (size_t i = 0; i < allMeshes.size(); ++i)
	{
		const CollectedAiMesh& collectedMesh = allMeshes[i];
		// If animated + skinned (has bones): pass nullptr → identity (Assimp already stores
		//   skinned vertices in bind-pose / model space).
		// If animated + unskinned (no bones, rigid-bind child of a bone): bake the accumulated
		//   node transform so vertices end up in the same model-space as skinned verts.
		// If static: always bake the node transform.
		const aiMatrix4x4* xform;
		if (m_HasAnimation)
			xform = (collectedMesh.mesh->mNumBones == 0) ? &collectedMesh.transform : nullptr;
		else
			xform = &collectedMesh.transform;

		VansMesh* slice = new VansMesh(needCPUData, supportRayTracing);
		if (slice->LoadMeshSubmeshFromScene(logic_device, queue, commandbuffer, scene, collectedMesh.mesh, xform, import_tangent, supportRayTracing))
		{
			slice->m_SourceNodeName = collectedMesh.nodeName;
			m_SubMeshes.push_back(slice);
			m_SubmeshMaterialInfos.push_back(BuildSubmeshMaterialInfo(scene, collectedMesh.mesh, baseDir));
		}
		else
		{
			delete slice;
		}
	}

	VANS_LOG("[LoadMultiMesh] Loaded " << m_SubMeshes.size() << " submeshes from: " << file_name);
}

bool VansGraphics::VansMesh::LoadMeshSubmesh(VkDevice& logic_device, VkQueue& queue,
	VansVKCommandBuffer* commandbuffer, const std::string& file_name,
	uint32_t submeshIndex, bool import_tangent)
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
	aiMatrix4x4 identityTransform;
	std::vector<CollectedAiMesh> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, identityTransform, allMeshes);

	if (submeshIndex >= static_cast<uint32_t>(allMeshes.size()))
	{
		VANS_LOG_ERROR("ERROR: submeshIndex " << submeshIndex << " out of range (total=" << allMeshes.size() << ")");
		return false;
	}

	const CollectedAiMesh& collectedMesh = allMeshes[submeshIndex];
	return LoadMeshSubmeshFromScene(logic_device, queue, commandbuffer, scene, collectedMesh.mesh, &collectedMesh.transform, import_tangent);
}

bool VansGraphics::VansMesh::LoadMeshSubmeshFromScene(VkDevice& logic_device, VkQueue& queue,
	VansVKCommandBuffer* commandbuffer, const aiScene* scene, aiMesh* mesh, const aiMatrix4x4* meshTransform, bool import_tangent, bool supportRayTracing)
{
	if (!scene || !mesh)
	{
		return false;
	}

	aiMatrix4x4 transform;
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

			// 当切线长度为零（UV退化或无切线数据）时，从法线重建正交切线参考系，
			// 避免零向量传入 GPU 后在 normalize(skinMat3 * vec3(0)) 处产生 NaN。
			if (tangent.SquareLength() < 1e-8f)
			{
				aiVector3D n = mesh->mNormals ? mesh->mNormals[i] : aiVector3D(0.f, 1.f, 0.f);
				// 选取与 n 不平行的参考轴
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

	// Upload to GPU (same path as LoadMesh)
	VkDeviceSize vertexBufferSize = m_MeshRawData.size() * sizeof(uint16_t);
	VkDeviceSize indexBufferSize  = m_MeshTriangleIndex.size() * sizeof(uint32_t);
	VkDeviceSize totalUploadSize = vertexBufferSize + indexBufferSize;

	VANS_LOG("[LoadMeshSubmeshFromScene] material='" << m_SourceMaterialName
		<< "' vertices=" << m_VertexCount
		<< " indices=" << m_IndexCount
		<< " triangles=" << (m_IndexCount / 3)
		<< " vertexBytes=" << vertexBufferSize
		<< " indexBytes=" << indexBufferSize
		<< " totalUploadBytes=" << totalUploadSize);

	VansVKBuffer stagingVertex, stagingIndex;
	if (!stagingVertex.CreatVulkanBuffer(logic_device, vertexBufferSize, VK_FORMAT_UNDEFINED,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VANS_LOG_ERROR("[LoadMeshSubmeshFromScene] Failed to create vertex staging buffer. bytes=" << vertexBufferSize);
		return false;
	}
	if (!stagingIndex.CreatVulkanBuffer(logic_device, indexBufferSize, VK_FORMAT_UNDEFINED,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VANS_LOG_ERROR("[LoadMeshSubmeshFromScene] Failed to create index staging buffer. bytes=" << indexBufferSize);
		stagingVertex.DestroyVulkanBuffer(logic_device);
		return false;
	}

	if (!stagingVertex.SetBufferData(m_MeshRawData.data(), 0, vertexBufferSize))
	{
		VANS_LOG_ERROR("[LoadMeshSubmeshFromScene] Failed to upload vertex staging data. bytes=" << vertexBufferSize);
		stagingVertex.DestroyVulkanBuffer(logic_device);
		stagingIndex.DestroyVulkanBuffer(logic_device);
		return false;
	}
	if (!stagingIndex.SetBufferData(m_MeshTriangleIndex.data(), 0, indexBufferSize))
	{
		VANS_LOG_ERROR("[LoadMeshSubmeshFromScene] Failed to upload index staging data. bytes=" << indexBufferSize);
		stagingVertex.DestroyVulkanBuffer(logic_device);
		stagingIndex.DestroyVulkanBuffer(logic_device);
		return false;
	}

	VkBufferUsageFlags vertexUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkBufferUsageFlags indexUsage  = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

	// When ray tracing is requested, add device-address and AS build-input flags
	// so that BuildBLAS can query buffer addresses and use them as geometry input.
	if (m_SupportRayTracing)
	{
		vertexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
			| VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
			| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		indexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
			| VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
			| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}

	if (!m_VertexBuffer.CreatVulkanBuffer(logic_device, vertexBufferSize, VK_FORMAT_R16_SFLOAT,
		vertexUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
	{
		VANS_LOG_ERROR("[LoadMeshSubmeshFromScene] Failed to create GPU vertex buffer. bytes=" << vertexBufferSize);
		stagingVertex.DestroyVulkanBuffer(logic_device);
		stagingIndex.DestroyVulkanBuffer(logic_device);
		return false;
	}
	if (!m_IndexBuffer.CreatVulkanBuffer(logic_device, indexBufferSize, VK_FORMAT_R32_UINT,
		indexUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
	{
		VANS_LOG_ERROR("[LoadMeshSubmeshFromScene] Failed to create GPU index buffer. bytes=" << indexBufferSize);
		stagingVertex.DestroyVulkanBuffer(logic_device);
		stagingIndex.DestroyVulkanBuffer(logic_device);
		return false;
	}

	commandbuffer->BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	VkBufferCopy copyRegion{};
	copyRegion.size = vertexBufferSize;
	vkCmdCopyBuffer(commandbuffer->GetVKCommandBuffer(), stagingVertex.GetNativeBuffer(), m_VertexBuffer.GetNativeBuffer(), 1, &copyRegion);
	copyRegion.size = indexBufferSize;
	vkCmdCopyBuffer(commandbuffer->GetVKCommandBuffer(), stagingIndex.GetNativeBuffer(), m_IndexBuffer.GetNativeBuffer(), 1, &copyRegion);
	commandbuffer->EndCommandBufferRecord();
	VansVKCommandBuffer::SubmitCommands(queue, logic_device, { commandbuffer->GetVKCommandBuffer() }, {}, {}, commandbuffer->m_CommandBufferFinishSubmitFence);
	commandbuffer->ResetCommandBuffer(false);

	stagingVertex.DestroyVulkanBuffer(logic_device);
	stagingIndex.DestroyVulkanBuffer(logic_device);

	m_MeshRawData.clear();
	if (!m_MeshRawPositionDataEnableCPURead)
	{
		m_MeshRawPositionData.clear();
		m_MeshTriangleIndex.clear();
	}

	return true;
}

std::vector<std::string> VansGraphics::VansMesh::GetSubmeshMaterialNames(const std::string& file_name)
{
	std::vector<std::string> result;

	Assimp::Importer importer;
	// No need for tangent calc or UV flip — we only care about material names
	const aiScene* scene = importer.ReadFile(file_name,
		aiProcess_Triangulate | aiProcess_GenNormals);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		VANS_LOG_ERROR("ERROR::ASSIMP (GetSubmeshMaterialNames)::" << importer.GetErrorString());
		return result;
	}

	aiMatrix4x4 identityTransform;
	std::vector<CollectedAiMesh> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, identityTransform, allMeshes);

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

	aiMatrix4x4 identityTransform;
	std::vector<CollectedAiMesh> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, identityTransform, allMeshes);

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

	aiMatrix4x4 identityTransform;
	std::vector<CollectedAiMesh> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, identityTransform, allMeshes);
	return static_cast<uint32_t>(allMeshes.size());
}
