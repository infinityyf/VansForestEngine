#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansMesh.h"
#include <iostream>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>


VansVulkan::VertexBufferParameters VansVulkan::VansMesh::GetVertexBufferParameter()
{
	VertexBufferParameters p = 
	{ 
		m_VertexBuffer.m_VansVKBuffer, 
		0
	};
	return p;
}


VansVulkan::IndexBufferParameters VansVulkan::VansMesh::GetIndexBufferParameter()
{
	IndexBufferParameters p =
	{
		m_IndexBuffer.m_VansVKBuffer,
		0,
		VK_INDEX_TYPE_UINT32,
	};
	return p;
}

void ProcessNode(aiNode* node, const aiScene* scene, std::vector<float>& meshRawData, std::vector<int>& meshIndex, int& vertexCount, bool import_tangent)
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
			meshRawData.emplace_back(texCoord.x);
			meshRawData.emplace_back(texCoord.y);
			meshRawData.emplace_back(normal.x);
			meshRawData.emplace_back(normal.y);
			meshRawData.emplace_back(normal.z);
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
		ProcessNode(node->mChildren[i], scene, meshRawData, meshIndex, vertexCount, import_tangent);
	}
}

void VansVulkan::VansMesh::LoadMesh(VkDevice& logic_device, const std::string& file_name, bool import_tangent)
{
	m_LogicalDevice = logic_device;
	m_MeshRawDataCPULoaded = false;
	m_VertexCount = 0;
	//用assimp
	Assimp::Importer importer;
	auto processFlag = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices;
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
	ProcessNode(scene->mRootNode, scene, m_MeshRawData, m_MeshTriangleIndex,m_VertexCount, import_tangent);
	m_MeshRawDataCPULoaded = true;

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
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	m_IndexBuffer.CreatVulkanBuffer(logic_device,
		m_MeshTriangleIndex.size() * sizeof(int),
		VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT| VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT 
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	m_VertexBuffer.SetBufferData(m_MeshRawData.data(), 0, m_MeshRawData.size() * sizeof(float));
	m_IndexBuffer.SetBufferData(m_MeshTriangleIndex.data(), 0, m_MeshTriangleIndex.size() * sizeof(int));
}

