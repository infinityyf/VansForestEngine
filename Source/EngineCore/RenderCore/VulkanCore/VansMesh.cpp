#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansMesh.h"
#include "VansVKCommandBuffer.h"
#include "VansVKDevice.h"
#include "../../Util/VansFileFingerprint.h"
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
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <type_traits>

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

	constexpr std::array<char, 8> kMeshCacheMagic = { 'V', 'A', 'N', 'S', 'M', 'S', 'H', '\0' };
	constexpr uint32_t kMeshCacheVersion = 4;
	constexpr uint32_t kMeshCacheFlagMultiMesh = 1u << 0;
	constexpr uint64_t kMeshCacheMaxVectorItems = 256ull * 1024ull * 1024ull;

	struct MeshCacheFileStamp
	{
		uint64_t size = 0;
		int64_t writeTime = 0;
		uint64_t contentHash = 0;
	};

	struct MeshCacheHeader
	{
		char magic[8] = {};
		uint32_t version = 0;
		uint32_t flags = 0;
		uint32_t submeshCount = 0;
		uint32_t importTangent = 0;
		float scaleFactor = 1.0f;
		uint64_t sourceSize = 0;
		int64_t sourceWriteTime = 0;
		uint64_t sourceHash = 0;
	};

	struct MeshCacheChunkHeader
	{
		int32_t vertexCount = 0;
		int32_t indexCount = 0;
		uint32_t vertexDataSize = 0;
		uint32_t reserved = 0;
	};

	static_assert(std::is_trivially_copyable_v<MeshCacheHeader>);
	static_assert(std::is_trivially_copyable_v<MeshCacheChunkHeader>);

	bool GetMeshCacheFileStamp(const std::filesystem::path& path, MeshCacheFileStamp& stamp)
	{
		Vans::VansFileFingerprint fingerprint;
		if (!Vans::ComputeFileFingerprint(path, fingerprint))
			return false;
		stamp.size = fingerprint.size;
		stamp.writeTime = fingerprint.writeTime;
		stamp.contentHash = fingerprint.contentHash;
		return true;
	}

	template<typename T>
	bool WritePod(std::ostream& out, const T& value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		out.write(reinterpret_cast<const char*>(&value), sizeof(T));
		return out.good();
	}

	template<typename T>
	bool ReadPod(std::istream& in, T& value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		in.read(reinterpret_cast<char*>(&value), sizeof(T));
		return in.good();
	}

	bool WriteString(std::ostream& out, const std::string& value)
	{
		const uint32_t size = static_cast<uint32_t>(std::min<size_t>(value.size(), UINT32_MAX));
		if (!WritePod(out, size))
			return false;
		if (size > 0)
			out.write(value.data(), size);
		return out.good();
	}

	bool ReadString(std::istream& in, std::string& value)
	{
		uint32_t size = 0;
		if (!ReadPod(in, size))
			return false;
		value.resize(size);
		if (size > 0)
			in.read(value.data(), size);
		return in.good();
	}

	template<typename T>
	bool WriteVector(std::ostream& out, const std::vector<T>& values)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		const uint64_t count = static_cast<uint64_t>(values.size());
		if (!WritePod(out, count))
			return false;
		if (!values.empty())
			out.write(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(T));
		return out.good();
	}

	template<typename T>
	bool ReadVector(std::istream& in, std::vector<T>& values)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		uint64_t count = 0;
		if (!ReadPod(in, count) || count > kMeshCacheMaxVectorItems)
			return false;
		values.resize(static_cast<size_t>(count));
		if (!values.empty())
			in.read(reinterpret_cast<char*>(values.data()), values.size() * sizeof(T));
		return in.good();
	}

	bool WriteMaterialInfo(std::ostream& out, const VansGraphics::FBXSubmeshMaterialInfo& info)
	{
		return WriteString(out, info.materialName)
			&& WriteString(out, info.diffuseTexPath)
			&& WriteString(out, info.normalTexPath)
			&& WriteString(out, info.metallicTexPath)
			&& WriteString(out, info.roughnessTexPath)
			&& WriteString(out, info.aoTexPath)
			&& WriteString(out, info.opacityTexPath)
			&& WritePod(out, info.diffuseColor)
			&& WritePod(out, info.specularColor)
			&& WritePod(out, info.emissiveColor)
			&& WritePod(out, info.opacity)
			&& WritePod(out, info.metallic)
			&& WritePod(out, info.roughness)
			&& WritePod(out, info.specularFactor)
			&& WritePod(out, info.shininess)
			&& WritePod(out, info.reflectionFactor);
	}

	bool ReadMaterialInfo(std::istream& in, VansGraphics::FBXSubmeshMaterialInfo& info)
	{
		return ReadString(in, info.materialName)
			&& ReadString(in, info.diffuseTexPath)
			&& ReadString(in, info.normalTexPath)
			&& ReadString(in, info.metallicTexPath)
			&& ReadString(in, info.roughnessTexPath)
			&& ReadString(in, info.aoTexPath)
			&& ReadString(in, info.opacityTexPath)
			&& ReadPod(in, info.diffuseColor)
			&& ReadPod(in, info.specularColor)
			&& ReadPod(in, info.emissiveColor)
			&& ReadPod(in, info.opacity)
			&& ReadPod(in, info.metallic)
			&& ReadPod(in, info.roughness)
			&& ReadPod(in, info.specularFactor)
			&& ReadPod(in, info.shininess)
			&& ReadPod(in, info.reflectionFactor);
	}

	bool MeshCacheHeaderMatches(
		const MeshCacheHeader& header,
		const MeshCacheFileStamp& sourceStamp,
		bool importTangent,
		bool expectMultiMesh,
		float scaleFactor,
		bool trustCacheWithoutSource)
	{
		if (std::memcmp(header.magic, kMeshCacheMagic.data(), kMeshCacheMagic.size()) != 0)
			return false;
		if (header.version != kMeshCacheVersion)
			return false;
		if (((header.flags & kMeshCacheFlagMultiMesh) != 0) != expectMultiMesh)
			return false;
		if ((header.importTangent != 0) != importTangent)
			return false;
		if (std::abs(header.scaleFactor - scaleFactor) > 0.0001f)
			return false;
		if (trustCacheWithoutSource)
			return header.submeshCount > 0;
		return header.sourceSize == sourceStamp.size &&
			header.sourceWriteTime == sourceStamp.writeTime &&
			header.sourceHash == sourceStamp.contentHash &&
			header.submeshCount > 0;
	}

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

void VansGraphics::VansMesh::ConfigureVertexInputLayout(bool import_tangent)
{
	m_VertexDataSize = 8 * sizeof(uint16_t);
	if (import_tangent)
		m_VertexDataSize += 6 * sizeof(uint16_t);

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
}

void VansGraphics::VansMesh::ReleaseCpuImportDataAfterUpload()
{
	m_MeshRawData.clear();
	if (!m_MeshRawPositionDataEnableCPURead)
	{
		m_MeshRawPositionData.clear();
		m_MeshRawTexCoordData.clear();
		m_MeshTriangleIndex.clear();
	}
}

bool VansGraphics::VansMesh::UploadRawMeshToGpu(
	VkDevice& logic_device,
	VkQueue& queue,
	VansVKCommandBuffer* commandbuffer,
	const char* context,
	bool keepImportDataAfterUpload)
{
	const char* label = context ? context : "VansMesh";
	if (commandbuffer == nullptr || m_MeshRawData.empty() || m_MeshTriangleIndex.empty())
	{
		VANS_LOG_ERROR("[" << label << "] Cannot upload empty mesh data");
		return false;
	}

	VkDeviceSize vertexBufferSize = m_MeshRawData.size() * sizeof(uint16_t);
	VkDeviceSize indexBufferSize = m_MeshTriangleIndex.size() * sizeof(uint32_t);

	VansVKBuffer stagingVertexBuffer;
	if (!stagingVertexBuffer.CreatVulkanBuffer(logic_device,
		vertexBufferSize,
		VK_FORMAT_UNDEFINED,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VANS_LOG_ERROR("[" << label << "] Failed to create vertex staging buffer. bytes=" << vertexBufferSize);
		return false;
	}

	VansVKBuffer stagingIndexBuffer;
	if (!stagingIndexBuffer.CreatVulkanBuffer(logic_device,
		indexBufferSize,
		VK_FORMAT_UNDEFINED,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VANS_LOG_ERROR("[" << label << "] Failed to create index staging buffer. bytes=" << indexBufferSize);
		stagingVertexBuffer.DestroyVulkanBuffer(logic_device);
		return false;
	}

	if (!stagingVertexBuffer.SetBufferData(m_MeshRawData.data(), 0, vertexBufferSize) ||
		!stagingIndexBuffer.SetBufferData(m_MeshTriangleIndex.data(), 0, indexBufferSize))
	{
		VANS_LOG_ERROR("[" << label << "] Failed to upload staging mesh data");
		stagingVertexBuffer.DestroyVulkanBuffer(logic_device);
		stagingIndexBuffer.DestroyVulkanBuffer(logic_device);
		return false;
	}

	VkBufferUsageFlags vertexUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkBufferUsageFlags indexUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (m_SupportRayTracing)
	{
		vertexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
			| VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
			| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		indexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
			| VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
			| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}

	if (!m_VertexBuffer.CreatVulkanBuffer(logic_device,
		vertexBufferSize,
		VK_FORMAT_R16_SFLOAT,
		vertexUsage,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
	{
		VANS_LOG_ERROR("[" << label << "] Failed to create GPU vertex buffer. bytes=" << vertexBufferSize);
		stagingVertexBuffer.DestroyVulkanBuffer(logic_device);
		stagingIndexBuffer.DestroyVulkanBuffer(logic_device);
		return false;
	}

	if (!m_IndexBuffer.CreatVulkanBuffer(logic_device,
		indexBufferSize,
		VK_FORMAT_R32_UINT,
		indexUsage,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
	{
		VANS_LOG_ERROR("[" << label << "] Failed to create GPU index buffer. bytes=" << indexBufferSize);
		stagingVertexBuffer.DestroyVulkanBuffer(logic_device);
		stagingIndexBuffer.DestroyVulkanBuffer(logic_device);
		return false;
	}

	if (!commandbuffer->BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
	{
		VANS_LOG_ERROR("[" << label << "] Failed to begin mesh GPU upload command buffer");
		stagingVertexBuffer.DestroyVulkanBuffer(logic_device);
		stagingIndexBuffer.DestroyVulkanBuffer(logic_device);
		return false;
	}

	commandbuffer->CopyBuffer(stagingVertexBuffer.GetNativeBuffer(), m_VertexBuffer.GetNativeBuffer(), 0, 0, vertexBufferSize);
	commandbuffer->CopyBuffer(stagingIndexBuffer.GetNativeBuffer(), m_IndexBuffer.GetNativeBuffer(), 0, 0, indexBufferSize);

	if (!commandbuffer->EndCommandBufferRecord()
		|| !VansVKCommandBuffer::SubmitCommands(queue, logic_device, { commandbuffer->GetVKCommandBuffer() }, {}, {}, commandbuffer->m_CommandBufferFinishSubmitFence)
		|| !commandbuffer->ResetCommandBuffer(false))
	{
		VANS_LOG_ERROR("[" << label << "] Failed to submit mesh GPU upload");
		stagingVertexBuffer.DestroyVulkanBuffer(logic_device);
		stagingIndexBuffer.DestroyVulkanBuffer(logic_device);
		return false;
	}

	stagingVertexBuffer.DestroyVulkanBuffer(logic_device);
	stagingIndexBuffer.DestroyVulkanBuffer(logic_device);

	if (!keepImportDataAfterUpload)
		ReleaseCpuImportDataAfterUpload();
	return true;
}

bool VansGraphics::VansMesh::TryLoadMeshCache(
	VkDevice& logic_device,
	VkQueue& queue,
	VansVKCommandBuffer* commandbuffer,
	const std::string& cachePath,
	const std::string& sourcePath,
	bool import_tangent,
	bool supportRayTracing,
	bool needCPUData,
	bool expectMultiMesh,
	float scaleFactor,
	bool trustCacheWithoutSource)
{
	if (cachePath.empty())
		return false;

	MeshCacheFileStamp sourceStamp{};
	if (!trustCacheWithoutSource && !GetMeshCacheFileStamp(sourcePath, sourceStamp))
		return false;

	std::ifstream in(cachePath, std::ios::binary);
	if (!in.is_open())
		return false;

	MeshCacheHeader header{};
	if (!ReadPod(in, header) ||
		!MeshCacheHeaderMatches(header, sourceStamp, import_tangent, expectMultiMesh, scaleFactor, trustCacheWithoutSource))
	{
		return false;
	}

	for (auto* subMesh : m_SubMeshes)
		delete subMesh;
	m_SubMeshes.clear();
	m_SubmeshMaterialInfos.clear();
	m_HasAnimation = false;
	m_AnimImportResult = {};

	auto readMeshChunk = [&](VansMesh& mesh) -> bool
	{
		MeshCacheChunkHeader chunk{};
		if (!ReadPod(in, chunk) || chunk.vertexCount <= 0 || chunk.indexCount <= 0)
			return false;

		mesh.m_VertexCount = chunk.vertexCount;
		mesh.m_IndexCount = chunk.indexCount;
		mesh.m_LogicalDevice = logic_device;
		mesh.m_MeshRawPositionDataEnableCPURead = needCPUData;
		mesh.m_SupportRayTracing = supportRayTracing;
		mesh.m_MeshRawDataCPULoaded = true;
		if (!ReadString(in, mesh.m_SourceMaterialName) ||
			!ReadString(in, mesh.m_SourceNodeName) ||
			!ReadString(in, mesh.m_SourceNodePath) ||
			!ReadPod(in, mesh.m_SourceNodeBindModelTransform) ||
			!ReadVector(in, mesh.m_MeshRawData) ||
			!ReadVector(in, mesh.m_MeshRawPositionData) ||
			!ReadVector(in, mesh.m_MeshRawTexCoordData) ||
			!ReadVector(in, mesh.m_MeshTriangleIndex))
		{
			return false;
		}

		mesh.ConfigureVertexInputLayout(import_tangent);
		if (chunk.vertexDataSize != 0 && chunk.vertexDataSize != mesh.m_VertexDataSize)
			return false;
		mesh.RebuildLocalBoundsFromRawPositions();
		if (!HasMeshGpuUploadTarget(logic_device, queue, commandbuffer))
			return true;
		return mesh.UploadRawMeshToGpu(logic_device, queue, commandbuffer, "MeshCache", false);
	};

	if (expectMultiMesh)
	{
		m_IsMultiMesh = true;
		m_SupportRayTracing = false;
		for (uint32_t i = 0; i < header.submeshCount; ++i)
		{
			VansMesh* slice = new VansMesh(needCPUData, supportRayTracing);
			slice->m_IsSubmesh = true;
			if (!readMeshChunk(*slice))
			{
				delete slice;
				for (auto* subMesh : m_SubMeshes)
					delete subMesh;
				m_SubMeshes.clear();
				m_SubmeshMaterialInfos.clear();
				return false;
			}
			FBXSubmeshMaterialInfo info;
			if (!ReadMaterialInfo(in, info))
			{
				delete slice;
				for (auto* subMesh : m_SubMeshes)
					delete subMesh;
				m_SubMeshes.clear();
				m_SubmeshMaterialInfos.clear();
				return false;
			}
			m_SubMeshes.push_back(slice);
			m_SubmeshMaterialInfos.push_back(std::move(info));
		}
	}
	else
	{
		m_IsMultiMesh = false;
		m_SupportRayTracing = supportRayTracing;
		if (!readMeshChunk(*this))
			return false;
	}

	VANS_LOG("[MeshCache] Hit " << cachePath);
	return true;
}

bool VansGraphics::VansMesh::IsMeshCacheCurrent(
	const std::string& cachePath,
	const std::string& sourcePath,
	bool import_tangent,
	bool expectMultiMesh,
	float scaleFactor)
{
	if (cachePath.empty())
		return false;

	MeshCacheFileStamp sourceStamp{};
	if (!GetMeshCacheFileStamp(sourcePath, sourceStamp))
		return false;

	std::ifstream in(cachePath, std::ios::binary);
	if (!in.is_open())
		return false;

	MeshCacheHeader header{};
	return ReadPod(in, header) &&
		MeshCacheHeaderMatches(
			header,
			sourceStamp,
			import_tangent,
			expectMultiMesh,
			scaleFactor,
			false);
}

VansGraphics::VansMeshCacheBuildStatus VansGraphics::VansMesh::BuildMeshCache(
	const std::string& file_name,
	bool import_tangent,
	bool expectMultiMesh,
	float scaleFactor,
	const Vans::VansSkeletalMeshImportSettings& skeletalImport,
	const std::string& cachePath,
	std::string& error)
{
	error.clear();
	if (cachePath.empty())
	{
		error = "Mesh cache path is empty";
		return VansMeshCacheBuildStatus::Failed;
	}

	if (IsMeshCacheCurrent(cachePath, file_name, import_tangent, expectMultiMesh, scaleFactor))
		return VansMeshCacheBuildStatus::Current;

	std::error_code ec;
	if (!std::filesystem::is_regular_file(file_name, ec))
	{
		error = "Mesh source does not exist: " + file_name;
		return VansMeshCacheBuildStatus::Failed;
	}

	VkDevice nullDevice = VK_NULL_HANDLE;
	VkQueue nullQueue = VK_NULL_HANDLE;
	VansVKCommandBuffer* nullCommandBuffer = nullptr;
	VansMesh mesh(/*needCPUData=*/true, /*supportRayTracing=*/false);
	if (expectMultiMesh)
	{
		mesh.LoadMultiMesh(
			nullDevice,
			nullQueue,
			nullCommandBuffer,
			file_name,
			import_tangent,
			/*supportRayTracing=*/false,
			/*needCPUData=*/true,
			scaleFactor,
			skeletalImport,
			cachePath,
			/*trustCacheWithoutSource=*/false);
	}
	else
	{
		mesh.LoadMesh(
			nullDevice,
			nullQueue,
			nullCommandBuffer,
			file_name,
			import_tangent,
			cachePath,
			/*trustCacheWithoutSource=*/false);
	}

	// Skeletal meshes retain their rig and skinning payload in the indexed package
	// resource artifact. The static .vmesh representation is deliberately not used.
	if (expectMultiMesh && !mesh.m_AnimImportResult.skeleton.bones.empty())
		return VansMeshCacheBuildStatus::NotEligible;

	if (!IsMeshCacheCurrent(cachePath, file_name, import_tangent, expectMultiMesh, scaleFactor))
	{
		error = "Failed to build current mesh cache: " + cachePath;
		return VansMeshCacheBuildStatus::Failed;
	}
	return VansMeshCacheBuildStatus::Cooked;
}

bool VansGraphics::VansMesh::SaveMeshCache(
	const std::string& cachePath,
	const std::string& sourcePath,
	bool import_tangent,
	bool expectMultiMesh,
	float scaleFactor) const
{
	if (cachePath.empty())
		return false;

	MeshCacheFileStamp sourceStamp{};
	if (!GetMeshCacheFileStamp(sourcePath, sourceStamp))
		return false;

	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(cachePath).parent_path(), ec);
	if (ec)
		return false;

	const std::filesystem::path temporaryPath = std::filesystem::path(cachePath).string() + ".tmp";
	std::ofstream out(temporaryPath, std::ios::binary | std::ios::trunc);
	if (!out.is_open())
		return false;

	MeshCacheHeader header{};
	std::memcpy(header.magic, kMeshCacheMagic.data(), kMeshCacheMagic.size());
	header.version = kMeshCacheVersion;
	header.flags = expectMultiMesh ? kMeshCacheFlagMultiMesh : 0u;
	header.submeshCount = expectMultiMesh ? static_cast<uint32_t>(m_SubMeshes.size()) : 1u;
	header.importTangent = import_tangent ? 1u : 0u;
	header.scaleFactor = scaleFactor;
	header.sourceSize = sourceStamp.size;
	header.sourceWriteTime = sourceStamp.writeTime;
	header.sourceHash = sourceStamp.contentHash;
	if (!WritePod(out, header))
		return false;

	auto writeMeshChunk = [&](const VansMesh& mesh) -> bool
	{
		MeshCacheChunkHeader chunk{};
		chunk.vertexCount = mesh.m_VertexCount;
		chunk.indexCount = mesh.m_IndexCount;
		chunk.vertexDataSize = mesh.m_VertexDataSize;
		return WritePod(out, chunk)
			&& WriteString(out, mesh.m_SourceMaterialName)
			&& WriteString(out, mesh.m_SourceNodeName)
			&& WriteString(out, mesh.m_SourceNodePath)
			&& WritePod(out, mesh.m_SourceNodeBindModelTransform)
			&& WriteVector(out, mesh.m_MeshRawData)
			&& WriteVector(out, mesh.m_MeshRawPositionData)
			&& WriteVector(out, mesh.m_MeshRawTexCoordData)
			&& WriteVector(out, mesh.m_MeshTriangleIndex);
	};

	if (expectMultiMesh)
	{
		for (size_t i = 0; i < m_SubMeshes.size(); ++i)
		{
			if (!m_SubMeshes[i] || !writeMeshChunk(*m_SubMeshes[i]))
				return false;
			const FBXSubmeshMaterialInfo info = i < m_SubmeshMaterialInfos.size()
				? m_SubmeshMaterialInfos[i]
				: FBXSubmeshMaterialInfo{};
			if (!WriteMaterialInfo(out, info))
				return false;
		}
	}
	else if (!writeMeshChunk(*this))
	{
		return false;
	}

	out.close();
	if (!out.good())
		return false;
	std::filesystem::rename(temporaryPath, cachePath, ec);
	if (ec)
	{
		ec.clear();
		std::filesystem::remove(cachePath, ec);
		ec.clear();
		std::filesystem::rename(temporaryPath, cachePath, ec);
		if (ec)
			return false;
	}

	VANS_LOG("[MeshCache] Wrote " << cachePath);
	return true;
}

uint16_t FloatToHalf(float f) 
{
	// Convert float32 to float16 using glm's pack helper.
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

void VansGraphics::VansMesh::LoadMesh(VkDevice& logic_device, VkQueue& queue, VansVKCommandBuffer* commandbuffer, const std::string& file_name, bool import_tangent, const std::string& cachePath, bool trustCacheWithoutSource)
{
	VANS_LOG("Load Mesh : " << file_name);
	m_LogicalDevice = logic_device;
	m_MeshRawDataCPULoaded = false;
	m_VertexCount = 0;
	ResetLocalBounds();
	if (TryLoadMeshCache(logic_device, queue, commandbuffer,
		cachePath, file_name, import_tangent, m_SupportRayTracing,
		m_MeshRawPositionDataEnableCPURead, false, 1.0f, trustCacheWithoutSource))
	{
		return;
	}
	//鐢╝ssimp
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

	ConfigureVertexInputLayout(import_tangent);
	SaveMeshCache(cachePath, file_name, import_tangent, false, 1.0f);
	if (!HasMeshGpuUploadTarget(logic_device, queue, commandbuffer))
		return;
	UploadRawMeshToGpu(logic_device, queue, commandbuffer, "VansMesh", false);
}

void VansGraphics::VansMesh::BuildBLAS(VansVKDevice& device, VansVKCommandBuffer& commandBuffer)
{
	VkDevice logic_device = device.GetLogicDevice();
	// Get the vertex buffer address.
	VkDeviceAddress vertexBufferAddress = m_VertexBuffer.GetDeviceAddress(logic_device);
	VkDeviceAddress indexBufferAddress = m_IndexBuffer.GetDeviceAddress(logic_device);

	// 瀹氫箟鍑犱綍鏁版嵁
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



	// Compute build size.
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

	//缁檅las鍒涘缓buffer
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
		buildSizesInfo.buildScratchSize + device.GetAccelerationStructureScratchAlignment() - 1,
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	const VkDeviceSize scratchAlignment = device.GetAccelerationStructureScratchAlignment();
	const VkDeviceAddress scratchBaseAddress = m_BLASScratchBuffer.GetDeviceAddress(logic_device);
	buildGeometryInfo.scratchData.deviceAddress =
		(scratchBaseAddress + scratchAlignment - 1) & ~(scratchAlignment - 1);

	// Create acceleration structure.
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
// InitFromRawData: build a mesh from pre-computed vertex and index arrays.
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
