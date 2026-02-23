#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansMesh.h"
#include "VansVKCommandBuffer.h"
#include <iostream>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

VansGraphics::VertexBufferParameters VansGraphics::VansMesh::GetVertexBufferParameter()
{
	VertexBufferParameters p = 
	{ 
		m_VertexBuffer.m_VansVKBuffer, 
		0
	};
	return p;
}


VansGraphics::IndexBufferParameters VansGraphics::VansMesh::GetIndexBufferParameter()
{
	IndexBufferParameters p =
	{
		m_IndexBuffer.m_VansVKBuffer,
		0,
		VK_INDEX_TYPE_UINT32,
	};
	return p;
}

uint16_t FloatToHalf(float f) 
{
	// 这里需要一个 float16 转换算法，或者使用 glm::packHalf1x16
	return glm::packHalf1x16(f);
}

void ProcessNode(aiNode* node, const aiScene* scene, std::vector<uint16_t>& meshRawData, std::vector<float>& meshRawPositionData, std::vector<int>& meshIndex, int& vertexCount, bool import_tangent)
{
	for (uint32_t i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		for (uint32_t i = 0; i < mesh->mNumVertices; i++)
		{
			aiVector3D vertex = mesh->mVertices[i];
			aiVector3D normal = mesh->mNormals[i];
			aiVector3D tangent(0, 0, 0);
			aiVector3D bitangent(0, 0, 0);
			aiVector3D texCoord(0,0,0);
			if (mesh->mTextureCoords[0]!=nullptr)
			{
				texCoord = mesh->mTextureCoords[0][i];
			}
			meshRawData.emplace_back(FloatToHalf(vertex.x));
			meshRawData.emplace_back(FloatToHalf(vertex.y));
			meshRawData.emplace_back(FloatToHalf(vertex.z));

			meshRawPositionData.emplace_back(vertex.x);
			meshRawPositionData.emplace_back(vertex.y);
			meshRawPositionData.emplace_back(vertex.z);
			meshRawPositionData.emplace_back(0.0);

			meshRawData.emplace_back(FloatToHalf(texCoord.x));
			meshRawData.emplace_back(FloatToHalf(texCoord.y));
			meshRawData.emplace_back(FloatToHalf(normal.x));
			meshRawData.emplace_back(FloatToHalf(normal.y));
			meshRawData.emplace_back(FloatToHalf(normal.z));

			meshRawPositionData.emplace_back(normal.x);
			meshRawPositionData.emplace_back(normal.y);
			meshRawPositionData.emplace_back(normal.z);
			meshRawPositionData.emplace_back(0.0);

			if (import_tangent)
			{
				if (mesh->mTangents != nullptr)
				{
					tangent = mesh->mTangents[i];
				}
				if (mesh->mBitangents != nullptr)
				{
					bitangent = mesh->mBitangents[i];
				}
				meshRawData.emplace_back(FloatToHalf(tangent.x));
				meshRawData.emplace_back(FloatToHalf(tangent.y));
				meshRawData.emplace_back(FloatToHalf(tangent.z));
				meshRawData.emplace_back(FloatToHalf(bitangent.x));
				meshRawData.emplace_back(FloatToHalf(bitangent.y));
				meshRawData.emplace_back(FloatToHalf(bitangent.z));
			}
		}
		vertexCount += mesh->mNumVertices;
		for (uint32_t i = 0; i < mesh->mNumFaces; i++)
		{
			aiFace face = mesh->mFaces[i];
			meshIndex.push_back(face.mIndices[0]);
			meshIndex.push_back(face.mIndices[1]);
			meshIndex.push_back(face.mIndices[2]);
		}
	}

	for (uint32_t i = 0; i < node->mNumChildren; i++)
	{
		ProcessNode(node->mChildren[i], scene, meshRawData, meshRawPositionData, meshIndex, vertexCount, import_tangent);
	}
}

// ─── Walk node tree and collect every aiMesh* in ProcessNode order ───────────
static void CollectAiMeshes(aiNode* node, const aiScene* scene, std::vector<aiMesh*>& out)
{
	for (uint32_t i = 0; i < node->mNumMeshes; i++)
		out.push_back(scene->mMeshes[node->mMeshes[i]]);
	for (uint32_t i = 0; i < node->mNumChildren; i++)
		CollectAiMeshes(node->mChildren[i], scene, out);
}

VansGraphics::VansMesh::VansMesh(bool needCPUData, bool supportRayTracing)
{
	m_MeshRawPositionDataEnableCPURead = needCPUData;
	m_SupportRayTracing = supportRayTracing;
}

void VansGraphics::VansMesh::LoadMesh(VkDevice& logic_device, VkQueue& queue, VansVKCommandBuffer* commandbuffer, const std::string& file_name, bool import_tangent)
{
	std::cout << "Load Mesh : " << file_name << std::endl;
	m_LogicalDevice = logic_device;
	m_MeshRawDataCPULoaded = false;
	m_VertexCount = 0;
	//用assimp
	Assimp::Importer importer;
	auto processFlag = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_GenSmoothNormals;
	if (import_tangent)
	{
		processFlag |= aiProcess_CalcTangentSpace;
	}
	const aiScene* scene = importer.ReadFile(file_name, processFlag);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cout << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
		return;
	}
	ProcessNode(scene->mRootNode, scene, m_MeshRawData, m_MeshRawPositionData, m_MeshTriangleIndex,m_VertexCount, import_tangent);
	m_MeshRawDataCPULoaded = true;

	m_IndexCount = m_MeshTriangleIndex.size();

	m_VertexDataSize = 8 * sizeof(uint16_t);
	if (import_tangent)
	{
		m_VertexDataSize += 6 * sizeof(uint16_t);
	}
	m_VertexInputBindingDescriptions = 
	{
		{
			0,
			m_VertexDataSize,
			VK_VERTEX_INPUT_RATE_VERTEX
		}
	};

	m_VertexInputAttributeDescriptions =
	{
		{
			 0,
			 0,
			 VK_FORMAT_R16G16B16_SFLOAT,
			 0
		 },
		 {
			 1,
			 0,
			 VK_FORMAT_R16G16_SFLOAT,
			 3 * sizeof(uint16_t)
		 },
		 {
			 2,
			 0,
			 VK_FORMAT_R16G16B16_SFLOAT,
			 5 * sizeof(uint16_t)
		 }
	};
	if (import_tangent)
	{
		m_VertexInputAttributeDescriptions.push_back(
			{
				3,
				0,
				VK_FORMAT_R16G16B16_SFLOAT,
				8 * sizeof(uint16_t)
			}
		);
		m_VertexInputAttributeDescriptions.push_back(
			{
				4,
				0,
				VK_FORMAT_R16G16B16_SFLOAT,
				11 * sizeof(uint16_t)
			}
		);
	}

	VkDeviceSize vertexBufferSize = m_MeshRawData.size() * sizeof(uint16_t);
    VkDeviceSize indexBufferSize = m_MeshTriangleIndex.size() * sizeof(uint32_t); // 注意：之前是 sizeof(int)，建议明确使用 uint32_t

    // =================================================================================
    // 1. 创建 Staging Buffers (CPU 可写，主机可见)
    // =================================================================================
    
    // 假设 VansVKBuffer 有一个 CreateStagingBuffer 辅助函数，或者直接使用 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    VansVKBuffer stagingVertexBuffer;
    stagingVertexBuffer.CreatVulkanBuffer(logic_device,
        vertexBufferSize,
        VK_FORMAT_UNDEFINED, // Staging buffer 格式通常不重要
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // 关键：作为传输源
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VansVKBuffer stagingIndexBuffer;
    stagingIndexBuffer.CreatVulkanBuffer(logic_device,
        indexBufferSize,
        VK_FORMAT_UNDEFINED,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 2. 将数据拷贝到 Staging Buffers
    stagingVertexBuffer.SetBufferData(m_MeshRawData.data(), 0, vertexBufferSize);
    stagingIndexBuffer.SetBufferData(m_MeshTriangleIndex.data(), 0, indexBufferSize);

    // =================================================================================
    // 3. 创建真正的 GPU Buffers (Device Local，性能最高)
    // =================================================================================
    
    VkBufferUsageFlags vertexUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkBufferUsageFlags indexUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    vertexUsage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        indexUsage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    m_VertexBuffer.CreatVulkanBuffer(logic_device,
        vertexBufferSize,
        VK_FORMAT_R16_SFLOAT,
        vertexUsage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); // 关键：显卡专用内存

    m_IndexBuffer.CreatVulkanBuffer(logic_device,
        indexBufferSize,
        VK_FORMAT_R32_UINT,
        indexUsage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // =================================================================================
    // 4. 执行 Copy 命令 (Staging -> Device Local)
    // =================================================================================
    
    // 这里你需要一个即时执行的 CommandBuffer (Single Time Command)
    // 假设你有这样的工具函数 VansVKFunctions::BeginSingleTimeCommands / EndSingleTimeCommands
	commandbuffer->BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VkBufferCopy copyRegion{};
    copyRegion.size = vertexBufferSize;
    vkCmdCopyBuffer(commandbuffer->GetVKCommandBuffer(), stagingVertexBuffer.GetNativeBuffer(), m_VertexBuffer.GetNativeBuffer(), 1, &copyRegion);

    copyRegion.size = indexBufferSize;
    vkCmdCopyBuffer(commandbuffer->GetVKCommandBuffer(), stagingIndexBuffer.GetNativeBuffer(), m_IndexBuffer.GetNativeBuffer(), 1, &copyRegion);
	
	commandbuffer->EndCommandBufferRecord();
	VansVKCommandBuffer::SubmitCommands(queue, logic_device, { commandbuffer->GetVKCommandBuffer() }, {}, {}, commandbuffer->m_CommandBufferFinishSubmitFence);
	commandbuffer->ResetCommandBuffer(false);

    // =================================================================================
    // 5. 清理 Staging Buffers
    // =================================================================================
    stagingVertexBuffer.DestroyVulkanBuffer(logic_device);
    stagingIndexBuffer.DestroyVulkanBuffer(logic_device);

    // 释放CPU端内存数据
    m_MeshRawData.clear();
	if (!m_MeshRawPositionDataEnableCPURead)
	{
		m_MeshRawPositionData.clear();
		m_MeshTriangleIndex.clear();
	}
}

void VansGraphics::VansMesh::BuildBLAS(VkDevice& logic_device, VkCommandBuffer& commandBuffer)
{
	// 获取顶点缓冲区地址
	VkBufferDeviceAddressInfo addressInfo{};
	addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addressInfo.buffer = m_VertexBuffer.GetNativeBuffer();
	addressInfo.pNext = nullptr;
	VkDeviceAddress vertexBufferAddress = vkGetBufferDeviceAddressKHR(logic_device, &addressInfo);

	addressInfo.buffer = m_IndexBuffer.GetNativeBuffer();
	VkDeviceAddress indexBufferAddress = vkGetBufferDeviceAddressKHR(logic_device, &addressInfo);

	// 定义几何数据
	VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
	triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	triangles.vertexFormat = VK_FORMAT_R16G16B16_SFLOAT;
	triangles.vertexData.deviceAddress = vertexBufferAddress;
	triangles.vertexStride = m_VertexDataSize;
	triangles.maxVertex = GetMeshVertexCount() - 1;
	triangles.indexType = VK_INDEX_TYPE_UINT32;
	triangles.indexData.deviceAddress = indexBufferAddress;
	triangles.transformData.deviceAddress = 0;

	VkAccelerationStructureGeometryKHR geometry{};
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.geometry.triangles = triangles;
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;



	// 计算构建大小
	VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{};
	buildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildGeometryInfo.geometryCount = 1;
	buildGeometryInfo.pGeometries = &geometry;

	VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
	buildRangeInfo.firstVertex = 0;
	buildRangeInfo.primitiveCount = GetIndexCount() / 3;
	buildRangeInfo.primitiveOffset = 0;
	buildRangeInfo.transformOffset = 0;

	// primitive counts array: number of primitives (triangles)
	uint32_t primCount = GetIndexCount() / 3; // indexed triangles
	uint32_t primitiveCounts[1] = { primCount };

	VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
	buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vkGetAccelerationStructureBuildSizesKHR(logic_device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildGeometryInfo, primitiveCounts, &buildSizesInfo);

	//给blas创建buffer
	m_BottomLevelASBuffer.CreatVulkanBuffer(
		logic_device,
		buildSizesInfo.accelerationStructureSize,
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkAccelerationStructureCreateInfoKHR accelCreateInfo = {};
	accelCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	accelCreateInfo.buffer = m_BottomLevelASBuffer.GetNativeBuffer();
	accelCreateInfo.size = buildSizesInfo.accelerationStructureSize;
	accelCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

	vkCreateAccelerationStructureKHR(logic_device, &accelCreateInfo, nullptr, &m_BottomLevelAS);

	buildGeometryInfo.dstAccelerationStructure = m_BottomLevelAS;

	m_BLASScratchBuffer.CreatVulkanBuffer(
		logic_device,
		buildSizesInfo.buildScratchSize,
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkBufferDeviceAddressInfo bufferAddressInfo;
	bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	bufferAddressInfo.buffer = m_BLASScratchBuffer.GetNativeBuffer();
	bufferAddressInfo.pNext = nullptr;
	buildGeometryInfo.scratchData.deviceAddress = vkGetBufferDeviceAddressKHR(logic_device, &bufferAddressInfo);

	//创建加速结构
	const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &buildRangeInfo;
	vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildGeometryInfo, &pRangeInfo);
}

void VansGraphics::VansMesh::ReleaseASTempData(VkDevice& logic_device)
{
	m_BLASScratchBuffer.DestroyVulkanBuffer(logic_device);
}

void VansGraphics::VansMesh::LoadMultiMesh(VkDevice& logic_device, VkQueue& queue,
	VansVKCommandBuffer* commandbuffer, const std::string& file_name, bool import_tangent)
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
		std::cout << "ERROR::ASSIMP (LoadMultiMesh)::" << importer.GetErrorString() << std::endl;
		return;
	}

	std::vector<aiMesh*> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, allMeshes);
	if (allMeshes.empty())
	{
		std::cout << "[LoadMultiMesh] No submeshes found in: " << file_name << std::endl;
		return;
	}

	for (aiMesh* mesh : allMeshes)
	{
		VansMesh* slice = new VansMesh(/*needCPUData=*/false, /*supportRayTracing=*/false);
		if (slice->LoadMeshSubmeshFromScene(logic_device, queue, commandbuffer, scene, mesh, import_tangent))
		{
			m_SubMeshes.push_back(slice);
		}
		else
		{
			delete slice;
		}
	}

	std::cout << "[LoadMultiMesh] Loaded " << m_SubMeshes.size() << " submeshes from: " << file_name << std::endl;
}

bool VansGraphics::VansMesh::LoadMeshSubmesh(VkDevice& logic_device, VkQueue& queue,
	VansVKCommandBuffer* commandbuffer, const std::string& file_name,
	uint32_t submeshIndex, bool import_tangent)
{
	std::cout << "Load Submesh [" << submeshIndex << "] from : " << file_name << std::endl;

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
		std::cout << "ERROR::ASSIMP (LoadMeshSubmesh)::" << importer.GetErrorString() << std::endl;
		return false;
	}

	// Collect all aiMesh* in traversal order
	std::vector<aiMesh*> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, allMeshes);

	if (submeshIndex >= static_cast<uint32_t>(allMeshes.size()))
	{
		std::cout << "ERROR: submeshIndex " << submeshIndex << " out of range (total=" << allMeshes.size() << ")" << std::endl;
		return false;
	}

	aiMesh* mesh = allMeshes[submeshIndex];
	return LoadMeshSubmeshFromScene(logic_device, queue, commandbuffer, scene, mesh, import_tangent);
}

bool VansGraphics::VansMesh::LoadMeshSubmeshFromScene(VkDevice& logic_device, VkQueue& queue,
	VansVKCommandBuffer* commandbuffer, const aiScene* scene, aiMesh* mesh, bool import_tangent)
{
	if (!scene || !mesh)
	{
		return false;
	}

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
	m_SupportRayTracing = false;

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

	VansVKBuffer stagingVertex, stagingIndex;
	stagingVertex.CreatVulkanBuffer(logic_device, vertexBufferSize, VK_FORMAT_UNDEFINED,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	stagingIndex.CreatVulkanBuffer(logic_device, indexBufferSize, VK_FORMAT_UNDEFINED,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	stagingVertex.SetBufferData(m_MeshRawData.data(), 0, vertexBufferSize);
	stagingIndex.SetBufferData(m_MeshTriangleIndex.data(), 0, indexBufferSize);

	// Submesh nodes never need ray tracing acceleration structures
	VkBufferUsageFlags vertexUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkBufferUsageFlags indexUsage  = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

	m_VertexBuffer.CreatVulkanBuffer(logic_device, vertexBufferSize, VK_FORMAT_R16_SFLOAT,
		vertexUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	m_IndexBuffer.CreatVulkanBuffer(logic_device, indexBufferSize, VK_FORMAT_R32_UINT,
		indexUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

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
		std::cout << "ERROR::ASSIMP (GetSubmeshMaterialNames)::" << importer.GetErrorString() << std::endl;
		return result;
	}

	std::vector<aiMesh*> allMeshes;
	CollectAiMeshes(scene->mRootNode, scene, allMeshes);

	result.reserve(allMeshes.size());
	for (aiMesh* mesh : allMeshes)
	{
		std::string matName;
		if (mesh->mMaterialIndex < scene->mNumMaterials)
		{
			aiString aiMatName;
			scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME, aiMatName);
			matName = aiMatName.C_Str();
		}
		result.push_back(matName);
	}

	return result;
}
