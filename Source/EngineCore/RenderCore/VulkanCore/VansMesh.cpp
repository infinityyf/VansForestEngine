#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansMesh.h"
#include <iostream>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>


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

void ProcessNode(aiNode* node, const aiScene* scene, std::vector<float>& meshRawData, std::vector<float>& meshRawPositionData, std::vector<int>& meshIndex, int& vertexCount, bool import_tangent)
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
			meshRawData.emplace_back(vertex.x);
			meshRawData.emplace_back(vertex.y);
			meshRawData.emplace_back(vertex.z);

			meshRawPositionData.emplace_back(vertex.x);
			meshRawPositionData.emplace_back(vertex.y);
			meshRawPositionData.emplace_back(vertex.z);
			meshRawPositionData.emplace_back(0.0);

			meshRawData.emplace_back(texCoord.x);
			meshRawData.emplace_back(texCoord.y);
			meshRawData.emplace_back(normal.x);
			meshRawData.emplace_back(normal.y);
			meshRawData.emplace_back(normal.z);

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
				meshRawData.emplace_back(tangent.x);
				meshRawData.emplace_back(tangent.y);
				meshRawData.emplace_back(tangent.z);
				meshRawData.emplace_back(bitangent.x);
				meshRawData.emplace_back(bitangent.y);
				meshRawData.emplace_back(bitangent.z);
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

void VansGraphics::VansMesh::LoadMesh(VkDevice& logic_device, const std::string& file_name, bool import_tangent, bool supportRayTracing)
{
	std::cout << "Load Mesh : " << file_name << std::endl;
	m_LogicalDevice = logic_device;
	m_MeshRawDataCPULoaded = false;
	m_SupportRayTracing = supportRayTracing;
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

	m_VertexDataSize = 8 * sizeof(float);
	if (import_tangent)
	{
		m_VertexDataSize += 6 * sizeof(float);
	}
	m_VertexInputBindingDescription = 
	{
		0,
		m_VertexDataSize,
		VK_VERTEX_INPUT_RATE_VERTEX
	};

	m_VertexInputAttributeDescriptions =
	{
		{
			 0,
			 0,
			 VK_FORMAT_R32G32B32_SFLOAT,
			 0
		 },
		 {
			 1,
			 0,
			 VK_FORMAT_R32G32_SFLOAT,
			 3 * sizeof(float)
		 },
		 {
			 2,
			 0,
			 VK_FORMAT_R32G32B32_SFLOAT,
			 5 * sizeof(float)
		 }
	};
	if (import_tangent)
	{
		m_VertexInputAttributeDescriptions.push_back(
			{
				3,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				8 * sizeof(float)
			}
		);
		m_VertexInputAttributeDescriptions.push_back(
			{
				4,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				11 * sizeof(float)
			}
		);
	}

	//上传数据到GPU
	//如果不使用stagebuffer，直接map,需要创建的memory设置host visible
	m_VertexBuffer.CreatVulkanBuffer(logic_device,
		m_MeshRawData.size() * sizeof(float),
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	//m_VertexPositionBuffer.CreatVulkanBuffer(logic_device,
	//	m_MeshRawPositionData.size() * sizeof(float),
	//	VK_FORMAT_R32G32B32_SFLOAT,
	//	VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
	//	| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|
	//	VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	//	VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	m_IndexBuffer.CreatVulkanBuffer(logic_device,
		m_MeshTriangleIndex.size() * sizeof(int),
		VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT| VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT 
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	m_VertexBuffer.SetBufferData(m_MeshRawData.data(), 0, m_MeshRawData.size() * sizeof(float));
	//m_VertexPositionBuffer.SetBufferData(m_MeshRawPositionData.data(), 0, m_MeshRawPositionData.size() * sizeof(float));
	m_IndexBuffer.SetBufferData(m_MeshTriangleIndex.data(), 0, m_MeshTriangleIndex.size() * sizeof(int));

	//释放CPU端内存数据
	m_MeshRawData.clear();
	m_MeshRawPositionData.clear();
	m_MeshTriangleIndex.clear();


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
	triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
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

