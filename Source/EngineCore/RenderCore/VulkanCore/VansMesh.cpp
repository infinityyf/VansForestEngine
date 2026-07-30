#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansMesh.h"
#include "VansVKCommandBuffer.h"
#include "VansVKDevice.h"
#include "../../Util/VansLog.h"
#include <iostream>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <GLM/glm.hpp>
#include <GLM/gtc/packing.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
	glm::vec3 NormalizeOrFallback(const glm::vec3& value, const glm::vec3& fallback)
	{
		const float len = glm::length(value);
		return len > 1.0e-6f ? value / len : fallback;
	}

	glm::vec3 PickLeastParallelAxis(const glm::vec3& axis)
	{
		const glm::vec3 absAxis = glm::abs(axis);
		if (absAxis.x <= absAxis.y && absAxis.x <= absAxis.z)
			return glm::vec3(1.0f, 0.0f, 0.0f);
		if (absAxis.y <= absAxis.z)
			return glm::vec3(0.0f, 1.0f, 0.0f);
		return glm::vec3(0.0f, 0.0f, 1.0f);
	}

	glm::vec3 OrthogonalFallback(const glm::vec3& axis)
	{
		const glm::vec3 ref = PickLeastParallelAxis(axis);
		return NormalizeOrFallback(glm::cross(axis, ref), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::vec3 PowerIterateSymmetric(const glm::mat3& matrix, const glm::vec3& seed)
	{
		glm::vec3 axis = NormalizeOrFallback(seed, glm::vec3(1.0f, 0.0f, 0.0f));
		for (uint32_t i = 0; i < 18; ++i)
		{
			const glm::vec3 next = matrix * axis;
			const float len = glm::length(next);
			if (len <= 1.0e-8f)
				break;
			axis = next / len;
		}
		return axis;
	}

	glm::vec3 MaxDiagonalAxis(const glm::mat3& matrix)
	{
		if (matrix[1][1] >= matrix[0][0] && matrix[1][1] >= matrix[2][2])
			return glm::vec3(0.0f, 1.0f, 0.0f);
		if (matrix[2][2] >= matrix[0][0])
			return glm::vec3(0.0f, 0.0f, 1.0f);
		return glm::vec3(1.0f, 0.0f, 0.0f);
	}

	glm::mat3 DeflateSymmetric(const glm::mat3& matrix, const glm::vec3& axis, float eigenValue)
	{
		glm::mat3 result = matrix;
		for (int c = 0; c < 3; ++c)
		{
			for (int r = 0; r < 3; ++r)
			{
				result[c][r] -= eigenValue * axis[c] * axis[r];
			}
		}
		return result;
	}

	std::array<glm::vec3, 3> MakeOrthonormalBasis(glm::vec3 axis0, glm::vec3 axis1)
	{
		axis0 = NormalizeOrFallback(axis0, glm::vec3(1.0f, 0.0f, 0.0f));
		axis1 = axis1 - axis0 * glm::dot(axis1, axis0);
		axis1 = NormalizeOrFallback(axis1, OrthogonalFallback(axis0));
		glm::vec3 axis2 = NormalizeOrFallback(glm::cross(axis0, axis1), glm::vec3(0.0f, 0.0f, 1.0f));
		axis1 = NormalizeOrFallback(glm::cross(axis2, axis0), axis1);
		return { axis0, axis1, axis2 };
	}

	VansGraphics::VansMeshLocalOBB BuildOBBForBasis(
		const std::vector<glm::vec3>& positions,
		const std::array<glm::vec3, 3>& axes)
	{
		VansGraphics::VansMeshLocalOBB obb;
		if (positions.empty())
			return obb;

		glm::vec3 minProj((std::numeric_limits<float>::max)());
		glm::vec3 maxProj(-(std::numeric_limits<float>::max)());
		for (const glm::vec3& p : positions)
		{
			const glm::vec3 proj(glm::dot(p, axes[0]), glm::dot(p, axes[1]), glm::dot(p, axes[2]));
			minProj = glm::min(minProj, proj);
			maxProj = glm::max(maxProj, proj);
		}

		const glm::vec3 centerProj = (minProj + maxProj) * 0.5f;
		obb.center = axes[0] * centerProj.x + axes[1] * centerProj.y + axes[2] * centerProj.z;
		obb.axes = axes;
		obb.halfExtent = glm::max((maxProj - minProj) * 0.5f, glm::vec3(0.0f));
		obb.valid = true;
		return obb;
	}

	float OBBVolumeScore(const VansGraphics::VansMeshLocalOBB& obb)
	{
		if (!obb.IsValid())
			return (std::numeric_limits<float>::max)();
		const glm::vec3 e = glm::max(obb.halfExtent, glm::vec3(1.0e-5f));
		return e.x * e.y * e.z;
	}
}

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

void VansGraphics::VansMesh::ResetLocalBounds()
{
	m_HasLocalBounds = false;
	m_LocalBoundsMin = glm::vec3(0.0f);
	m_LocalBoundsMax = glm::vec3(0.0f);
	m_LocalOBB = VansMeshLocalOBB{};
}

void VansGraphics::VansMesh::ExpandLocalBounds(const glm::vec3& point)
{
	if (!m_HasLocalBounds)
	{
		m_LocalBoundsMin = point;
		m_LocalBoundsMax = point;
		m_HasLocalBounds = true;
		return;
	}

	m_LocalBoundsMin = glm::min(m_LocalBoundsMin, point);
	m_LocalBoundsMax = glm::max(m_LocalBoundsMax, point);
}

void VansGraphics::VansMesh::RebuildLocalBoundsFromRawPositions()
{
	ResetLocalBounds();
	if (m_VertexCount <= 0 || m_MeshRawPositionData.empty())
	{
		return;
	}

	size_t stride = 0;
	const size_t vertexCount = static_cast<size_t>(m_VertexCount);
	if (m_MeshRawPositionData.size() >= vertexCount * 8)
		stride = 8;
	else if (m_MeshRawPositionData.size() >= vertexCount * 4)
		stride = 4;
	else if (m_MeshRawPositionData.size() >= vertexCount * 3)
		stride = 3;
	else
		return;

	std::vector<glm::vec3> positions;
	positions.reserve(vertexCount);
	for (size_t i = 0; i < vertexCount; ++i)
	{
		const size_t offset = i * stride;
		const glm::vec3 position(
			m_MeshRawPositionData[offset + 0],
			m_MeshRawPositionData[offset + 1],
			m_MeshRawPositionData[offset + 2]);
		ExpandLocalBounds(position);
		positions.push_back(position);
	}
	RebuildLocalOBBFromPositions(positions);
}

void VansGraphics::VansMesh::RebuildLocalOBBFromPositions(const std::vector<glm::vec3>& positions)
{
	m_LocalOBB = VansMeshLocalOBB{};
	if (positions.empty())
		return;

	glm::vec3 mean(0.0f);
	for (const glm::vec3& p : positions)
		mean += p;
	mean /= static_cast<float>(positions.size());

	glm::mat3 covariance(0.0f);
	for (const glm::vec3& p : positions)
	{
		const glm::vec3 d = p - mean;
		covariance[0][0] += d.x * d.x;
		covariance[0][1] += d.x * d.y;
		covariance[0][2] += d.x * d.z;
		covariance[1][0] += d.y * d.x;
		covariance[1][1] += d.y * d.y;
		covariance[1][2] += d.y * d.z;
		covariance[2][0] += d.z * d.x;
		covariance[2][1] += d.z * d.y;
		covariance[2][2] += d.z * d.z;
	}
	covariance /= static_cast<float>(positions.size());

	std::array<std::array<glm::vec3, 3>, 2> candidateAxes = {
		std::array<glm::vec3, 3>{
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f)
		},
		std::array<glm::vec3, 3>{
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f)
		}
	};

	const glm::vec3 axis0 = PowerIterateSymmetric(covariance, MaxDiagonalAxis(covariance));
	const float eigen0 = glm::dot(axis0, covariance * axis0);
	const glm::mat3 deflated = DeflateSymmetric(covariance, axis0, eigen0);
	const glm::vec3 axis1Seed = OrthogonalFallback(axis0);
	const glm::vec3 axis1 = PowerIterateSymmetric(deflated, axis1Seed);
	candidateAxes[1] = MakeOrthonormalBasis(axis0, axis1);

	VansMeshLocalOBB best;
	float bestScore = (std::numeric_limits<float>::max)();
	for (const auto& axes : candidateAxes)
	{
		VansMeshLocalOBB candidate = BuildOBBForBasis(positions, axes);
		const float score = OBBVolumeScore(candidate);
		if (score < bestScore)
		{
			best = candidate;
			bestScore = score;
		}
	}
	m_LocalOBB = best;
}

uint16_t FloatToHalf(float f) 
{
	// 这里需要一个 float16 转换算法，或者使用 glm::packHalf1x16
	return glm::packHalf1x16(f);
}

void ProcessNode(aiNode* node, const aiScene* scene, std::vector<uint16_t>& meshRawData, std::vector<float>& meshRawPositionData, std::vector<float>& meshRawTexCoordData, std::vector<int>& meshIndex, int& vertexCount, bool import_tangent)
{
	for (uint32_t i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		int baseVertex = vertexCount;
		for (uint32_t i = 0; i < mesh->mNumVertices; i++)
		{
			aiVector3D vertex = mesh->mVertices[i];
			aiVector3D normal = (mesh->mNormals && mesh->mNormals[i].SquareLength() > 1e-6f)
				? mesh->mNormals[i] : aiVector3D(0, 1, 0);
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
			meshRawPositionData.emplace_back(0.0f);

			meshRawTexCoordData.emplace_back(texCoord.x);
			meshRawTexCoordData.emplace_back(texCoord.y);

			meshRawData.emplace_back(FloatToHalf(texCoord.x));
			meshRawData.emplace_back(FloatToHalf(texCoord.y));
			meshRawData.emplace_back(FloatToHalf(normal.x));
			meshRawData.emplace_back(FloatToHalf(normal.y));
			meshRawData.emplace_back(FloatToHalf(normal.z));

			meshRawPositionData.emplace_back(normal.x);
			meshRawPositionData.emplace_back(normal.y);
			meshRawPositionData.emplace_back(normal.z);
			meshRawPositionData.emplace_back(0.0f);

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
			meshIndex.push_back(baseVertex + face.mIndices[0]);
			meshIndex.push_back(baseVertex + face.mIndices[1]);
			meshIndex.push_back(baseVertex + face.mIndices[2]);
		}
	}

	for (uint32_t i = 0; i < node->mNumChildren; i++)
	{
		ProcessNode(node->mChildren[i], scene, meshRawData, meshRawPositionData, meshRawTexCoordData, meshIndex, vertexCount, import_tangent);
	}
}



VansGraphics::VansMesh::VansMesh(bool needCPUData, bool supportRayTracing)
{
	m_MeshRawPositionDataEnableCPURead = needCPUData;
	m_SupportRayTracing = supportRayTracing;
}

void VansGraphics::VansMesh::LoadMesh(VkDevice& logic_device, VkQueue& queue, VansVKCommandBuffer* commandbuffer, const std::string& file_name, bool import_tangent)
{
	VANS_LOG("Load Mesh : " << file_name);
	m_LogicalDevice = logic_device;
	m_MeshRawDataCPULoaded = false;
	m_VertexCount = 0;
	ResetLocalBounds();
	//用assimp
	Assimp::Importer importer;
	auto processFlag = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals;
	if (import_tangent)
	{
		processFlag |= aiProcess_CalcTangentSpace;
	}
	const aiScene* scene = importer.ReadFile(file_name, processFlag);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		VANS_LOG_ERROR("ERROR::ASSIMP::" << importer.GetErrorString());
		return;
	}
	ProcessNode(scene->mRootNode, scene, m_MeshRawData, m_MeshRawPositionData, m_MeshRawTexCoordData, m_MeshTriangleIndex, m_VertexCount, import_tangent);
	RebuildLocalBoundsFromRawPositions();
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
	if (!commandbuffer->BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
	{
		VANS_LOG_ERROR("[VansMesh] Failed to begin mesh GPU buffer upload command buffer.");
		stagingVertexBuffer.DestroyVulkanBuffer(logic_device);
		stagingIndexBuffer.DestroyVulkanBuffer(logic_device);
		return;
	}
	commandbuffer->CopyBuffer(stagingVertexBuffer.GetNativeBuffer(), m_VertexBuffer.GetNativeBuffer(), 0, 0, vertexBufferSize);
	commandbuffer->CopyBuffer(stagingIndexBuffer.GetNativeBuffer(), m_IndexBuffer.GetNativeBuffer(), 0, 0, indexBufferSize);
	
	if (!commandbuffer->EndCommandBufferRecord()
		|| !VansVKCommandBuffer::SubmitCommands(queue, logic_device, { commandbuffer->GetVKCommandBuffer() }, {}, {}, commandbuffer->m_CommandBufferFinishSubmitFence)
		|| !commandbuffer->ResetCommandBuffer(false))
	{
		VANS_LOG_ERROR("[VansMesh] Failed to submit mesh GPU buffer upload.");
		stagingVertexBuffer.DestroyVulkanBuffer(logic_device);
		stagingIndexBuffer.DestroyVulkanBuffer(logic_device);
		return;
	}

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
		m_MeshRawTexCoordData.clear();
		m_MeshTriangleIndex.clear();
	}
}

void VansGraphics::VansMesh::BuildBLAS(VansVKDevice& device, VansVKCommandBuffer& commandBuffer)
{
	VkDevice logic_device = device.GetLogicDevice();
	// 获取顶点缓冲区地址
	VkDeviceAddress vertexBufferAddress = m_VertexBuffer.GetDeviceAddress(logic_device);
	VkDeviceAddress indexBufferAddress = m_IndexBuffer.GetDeviceAddress(logic_device);

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
	device.GetAccelerationStructureBuildSizes(&buildGeometryInfo, primitiveCounts, &buildSizesInfo);

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

	device.CreateAccelerationStructure(&accelCreateInfo, &m_BottomLevelAS);

	buildGeometryInfo.dstAccelerationStructure = m_BottomLevelAS;

	m_BLASScratchBuffer.CreatVulkanBuffer(
		logic_device,
		buildSizesInfo.buildScratchSize,
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	buildGeometryInfo.scratchData.deviceAddress = m_BLASScratchBuffer.GetDeviceAddress(logic_device);

	//创建加速结构
	const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &buildRangeInfo;
	commandBuffer.BuildAccelerationStructures(&buildGeometryInfo, pRangeInfo);
}

void VansGraphics::VansMesh::DestroyBLAS(VansVKDevice& device)
{
	VkDevice logic_device = device.GetLogicDevice();
	if (m_BottomLevelAS != VK_NULL_HANDLE)
	{
		device.DestroyAccelerationStructure(m_BottomLevelAS);
		m_BottomLevelAS = VK_NULL_HANDLE;
	}
	m_BottomLevelASBuffer.DestroyVulkanBuffer(logic_device);
	m_BLASScratchBuffer.DestroyVulkanBuffer(logic_device);
}

void VansGraphics::VansMesh::ReleaseASTempData(VkDevice& logic_device)
{
	m_BLASScratchBuffer.DestroyVulkanBuffer(logic_device);
}

// ============================================================================
// InitFromRawData — build a mesh from pre-computed vertex + index arrays
// ============================================================================
void VansGraphics::VansMesh::InitFromRawData(
	VkDevice device,
	const void* vertexData, uint32_t vertexCount, uint32_t vertexStride,
	const uint32_t* indexData, uint32_t indexCount,
	const std::vector<VkVertexInputBindingDescription>& bindings,
	const std::vector<VkVertexInputAttributeDescription>& attribs,
	const std::vector<float>& rawPositionData)
{
	m_LogicalDevice = device;
	m_VertexCount   = static_cast<int>(vertexCount);
	m_IndexCount    = static_cast<int>(indexCount);
	m_VertexDataSize = vertexStride;

	m_VertexInputBindingDescriptions   = bindings;
	m_VertexInputAttributeDescriptions = attribs;

	if (!rawPositionData.empty())
	{
		m_MeshRawPositionData = rawPositionData;
		RebuildLocalBoundsFromRawPositions();
	}
	else
	{
		ResetLocalBounds();
	}

	// Vertex buffer
	VkDeviceSize vbSize = static_cast<VkDeviceSize>(vertexStride) * vertexCount;
	m_VertexBuffer.CreatVulkanBuffer(device, vbSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_VertexBuffer.SetBufferData(vertexData, 0, vbSize);

	// Index buffer
	VkDeviceSize ibSize = sizeof(uint32_t) * indexCount;
	m_IndexBuffer.CreatVulkanBuffer(device, ibSize, VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_IndexBuffer.SetBufferData(indexData, 0, ibSize);
}
