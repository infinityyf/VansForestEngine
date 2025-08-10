
#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"

#include "../VansGraphicsBuffer.h"
#include "../VansAsset.h"
#include "VansVKBuffer.h"
#include <string>
using namespace VansGraphics;

namespace VansVulkan
{
	struct IndexBufferParameters
	{
		VkBuffer Buffer;
		VkDeviceSize MemoryOffset;
		VkIndexType IndexType;
	};

	struct VertexBufferParameters 
	{
		VkBuffer Buffer;
		VkDeviceSize MemoryOffset;
	};

	class VansMesh : public VansAsset
	{
		friend class VansVKCommandBuffer;
		friend class VansRayTracing;

	private:
		VansVKBuffer m_VertexBuffer;

		VansVKBuffer m_IndexBuffer;

	public :
		VertexBufferParameters GetVertexBufferParameter();

		IndexBufferParameters GetIndexBufferParameter();

		uint32_t GetMeshVertexStride() { return m_VertexDataSize; }

		uint32_t GetMeshVertexCount() { return m_VertexCount; }

		uint32_t GetIndexCount() { return m_MeshTriangleIndex.size(); }

		~VansMesh()
		{
			m_VertexBuffer.DestroyVulkanBuffer(m_LogicalDevice);
			m_IndexBuffer.DestroyVulkanBuffer(m_LogicalDevice);
		}

	private:

		//mesh data
		std::vector<float> m_MeshRawData;

		std::vector<int> m_MeshTriangleIndex;

		//标记CPU数据释放生效
		bool m_MeshRawDataCPULoaded;

		//attribute data
		std::vector<VkVertexInputAttributeDescription> m_VertexInputAttributeDescriptions;

		//vertex bind data
		VkVertexInputBindingDescription m_VertexInputBindingDescription;

		VkDevice m_LogicalDevice;

		uint32_t m_VertexDataSize;

		int m_VertexCount;

	public:

		void LoadMesh(VkDevice& logic_device,const std::string& file_name, bool import_tangent = false);
	};
}