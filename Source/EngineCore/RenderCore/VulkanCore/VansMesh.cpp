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

void ProcessNode(aiNode* node, const aiScene* scene, std::vector<float>& meshRawData, std::vector<int>& meshIndex)
{
	for (uint32_t i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		for (uint32_t i = 0; i < mesh->mNumVertices; i++)
		{
			aiVector3D vertex = mesh->mVertices[i];
			aiVector3D normal = mesh->mNormals[i];
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
		}
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
		ProcessNode(node->mChildren[i], scene, meshRawData, meshIndex);
	}
}

void VansVulkan::VansMesh::LoadMesh(VkDevice& logic_device, const std::string& file_name)
{
	m_LogicalDevice = logic_device;
	m_MeshRawDataCPULoaded = false;
	//tinyobj::attrib_t attribs;
	//std::vector<tinyobj::shape_t> shapes;
	//std::vector<tinyobj::material_t> materials;
	//std::string error;
	//std::string warn;
	//bool result = tinyobj::LoadObj(&attribs, &shapes, &materials, &warn ,&error, file_name.c_str());
	//if (!result) 
	//{
	//	std::cout << "Could not open '" << file_name << "' file.";
	//	if (0 < error.size()) 
	//	{
	//		std::cout << " " << error;
	//	}
	//	std::cout << std::endl;
	//	return;
	//}
	//uint32_t offset = 0;
	//uint32_t tri_index = 0;
	//for (auto& shape : shapes)
	//{
	//	uint32_t part_offset = offset;
	//	for (auto& index : shape.mesh.indices)
	//	{
	//		m_MeshRawData.emplace_back(attribs.vertices[3 * index.vertex_index + 0]);
	//		m_MeshRawData.emplace_back(attribs.vertices[3 * index.vertex_index + 1]);
	//		m_MeshRawData.emplace_back(attribs.vertices[3 * index.vertex_index + 2]);
	//		m_MeshTriangleIndex.push_back(tri_index++);
	//		m_MeshTriangleIndex.push_back(tri_index++);
	//		m_MeshTriangleIndex.push_back(tri_index++);
	//		++offset;
	//		if (attribs.texcoords.size() > 0)
	//		{
	//			m_MeshRawData.emplace_back(attribs.texcoords[2 * index.texcoord_index + 0]);
	//			m_MeshRawData.emplace_back(attribs.texcoords[2 * index.texcoord_index + 1]);
	//		}
	//		else
	//		{
	//			m_MeshRawData.emplace_back(0.0f);
	//			m_MeshRawData.emplace_back(0.0f);
	//		}
	//		if (attribs.normals.size() > 0)
	//		{
	//			m_MeshRawData.emplace_back(attribs.normals[3 * index.normal_index + 0]);
	//			m_MeshRawData.emplace_back(attribs.normals[3 * index.normal_index + 1]);
	//			m_MeshRawData.emplace_back(attribs.normals[3 * index.normal_index + 2]);
	//		}
	//		else
	//		{
	//			
	//			m_MeshRawData.emplace_back(0.0f);
	//			m_MeshRawData.emplace_back(1.0f);
	//			m_MeshRawData.emplace_back(0.0f);
	//		}

	//	}
	//	m_MeshRawDataCPULoaded = true;
	//}


	//用assimp
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(file_name, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cout << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
		return;
	}
	ProcessNode(scene->mRootNode, scene, m_MeshRawData, m_MeshTriangleIndex);
	m_MeshRawDataCPULoaded = true;

	m_VertexInputBindingDescription = 
	{
		0,
		8 * sizeof(float),
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

	//上传数据到GPU
	//如果不使用stagebuffer，直接map,需要创建的memory设置host visible
	m_VertexBuffer.CreatVulkanBuffer(logic_device,
		m_MeshRawData.size() * sizeof(float),
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	m_IndexBuffer.CreatVulkanBuffer(logic_device,
		m_MeshTriangleIndex.size() * sizeof(int),
		VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT| VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	m_VertexBuffer.SetBufferData(m_MeshRawData.data(), 0, m_MeshRawData.size() * sizeof(float));
	m_IndexBuffer.SetBufferData(m_MeshTriangleIndex.data(), 0, m_MeshTriangleIndex.size() * sizeof(int));
}

