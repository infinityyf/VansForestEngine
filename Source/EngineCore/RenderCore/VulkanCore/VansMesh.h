#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"
#include <assimp/matrix4x4.h>

#include "../VansGraphicsBuffer.h"
#include "../VansAsset.h"
#include "VansVKBuffer.h"
#include <string>
#include <vector>
#include <unordered_map>
using namespace VansGraphics;

struct aiScene;
struct aiMesh;

namespace VansGraphics
{
	// ── FBX/OBJ sub-mesh material info extracted by Assimp ──────────────────
	// Populated during LoadMultiMesh so the scene loader can auto-create
	// materials and textures without manual JSON entries.
	struct FBXSubmeshMaterialInfo
	{
		std::string materialName;        // aiMaterial name (e.g. "car_chassis")

		// Texture file paths extracted from aiMaterial (empty if not present)
		std::string diffuseTexPath;      // aiTextureType_DIFFUSE
		std::string normalTexPath;       // aiTextureType_NORMALS / aiTextureType_HEIGHT
		std::string metallicTexPath;     // aiTextureType_METALNESS
		std::string roughnessTexPath;    // aiTextureType_DIFFUSE_ROUGHNESS / aiTextureType_SHININESS
		std::string aoTexPath;           // aiTextureType_AMBIENT_OCCLUSION / aiTextureType_LIGHTMAP
		std::string opacityTexPath;      // aiTextureType_OPACITY

		// Basic material parameters from aiMaterial
		float opacity = 1.0f;            // AI_MATKEY_OPACITY
		float metallic = 0.0f;
		float roughness = 0.5f;

		// Returns true if the submesh should use a transparent material
		bool IsTransparent() const
		{
			return !opacityTexPath.empty() || opacity < 0.99f;
		}
	};

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

		//VansVKBuffer m_VertexPositionBuffer;

		VansVKBuffer m_IndexBuffer;

	public :

		VansMesh(bool needCPUData = false, bool supportRayTracing = false);

		VertexBufferParameters GetVertexBufferParameter();

		IndexBufferParameters GetIndexBufferParameter();

		uint32_t GetMeshVertexStride() { return m_VertexDataSize; }

		uint32_t GetMeshVertexCount() { return m_VertexCount; }

		uint32_t GetIndexCount() { return m_IndexCount; }

		// Physics mesh data access
		const std::vector<float>& GetMeshRawPositionData() const { return m_MeshRawPositionData; }
		const std::vector<int>& GetMeshTriangleIndex() const { return m_MeshTriangleIndex; }

		VansVKBuffer& GetBLASVertexBuffer() { return m_VertexBuffer; }

		VansVKBuffer& GetIndexBuffer() { return m_IndexBuffer; }

		int GetBLASIndex() { return m_BLASIndex; }

		void SetBLASIndex(int index) { m_BLASIndex = index; }

		~VansMesh()
		{
			m_VertexBuffer.DestroyVulkanBuffer(m_LogicalDevice);
			//m_VertexPositionBuffer.DestroyVulkanBuffer(m_LogicalDevice);
			m_IndexBuffer.DestroyVulkanBuffer(m_LogicalDevice);
		}

	public:

		//attribute data
		std::vector<VkVertexInputAttributeDescription> m_VertexInputAttributeDescriptions;

		//vertex bind data
		std::vector <VkVertexInputBindingDescription> m_VertexInputBindingDescriptions;

	private:

		//mesh data
		std::vector<uint16_t> m_MeshRawData;

		std::vector<float> m_MeshRawPositionData;

		std::vector<int> m_MeshTriangleIndex;

		//标记CPU数据释放生效
		bool m_MeshRawDataCPULoaded;

		//标记是否需要访问CPU数据
		bool m_MeshRawPositionDataEnableCPURead;

		VkDevice m_LogicalDevice;

		uint32_t m_VertexDataSize;

		int m_VertexCount;

		int m_IndexCount;

	public:

		void LoadMesh(VkDevice& logic_device, VkQueue& queue, VansVKCommandBuffer* commandbuffer,const std::string& file_name, bool import_tangent = false);

		// Loads only the specified aiMesh index from a file (used for submesh splitting).
		// Returns false if the index is out of range.
		bool LoadMeshSubmesh(VkDevice& logic_device, VkQueue& queue, VansVKCommandBuffer* commandbuffer,
			const std::string& file_name, uint32_t submeshIndex, bool import_tangent = false);

		// Static helper: returns the list of (aiMesh flat index -> material name) pairs
		// without uploading any GPU data. Used by the scene to enumerate submeshes.
		static std::vector<std::string> GetSubmeshMaterialNames(const std::string& file_name);

		// The MTL/FBX material name that was active when this mesh was loaded as a submesh.
		// Empty for whole-mesh loads.
		std::string m_SourceMaterialName;

		// Multi-mesh support: when true this mesh acts as a container for per-material slices.
		bool m_IsMultiMesh = false;

		// One VansMesh per aiMesh/material group, populated by LoadMultiMesh.
		std::vector<VansMesh*> m_SubMeshes;

		// Per-submesh material info extracted from the FBX/OBJ file (parallel to m_SubMeshes).
		std::vector<FBXSubmeshMaterialInfo> m_SubmeshMaterialInfos;

		// Internal helper to populate this mesh from an already-loaded aiScene/aiMesh (avoids re-reading files per submesh).
		bool LoadMeshSubmeshFromScene(VkDevice& logic_device, VkQueue& queue, VansVKCommandBuffer* commandbuffer,
			const aiScene* scene, aiMesh* mesh, const aiMatrix4x4* meshTransform = nullptr,
			bool import_tangent = false, bool supportRayTracing = false);

		// Loads the whole file then splits it into per-material VansMesh slices stored in m_SubMeshes.
		// Also populates m_SubmeshMaterialInfos with texture paths and material metadata.
		void LoadMultiMesh(VkDevice& logic_device, VkQueue& queue, VansVKCommandBuffer* commandbuffer,
			const std::string& file_name, bool import_tangent = false,
			bool supportRayTracing = false, bool needCPUData = false);

		// Static helper: extract FBXSubmeshMaterialInfo for each submesh from a file without GPU upload.
		static std::vector<FBXSubmeshMaterialInfo> GetSubmeshMaterialInfos(const std::string& file_name);

		// Lightweight probe: open the file with Assimp and return the number of aiMeshes
		// without uploading any GPU data.  Used by the scene loader for auto-detection.
		static uint32_t ProbeSubmeshCount(const std::string& file_name);

		void BuildBLAS(VkDevice& logic_device, VkCommandBuffer& commandBuffer);

		void ReleaseASTempData(VkDevice& logic_device);

		VkAccelerationStructureKHR GetBLAS() { return m_BottomLevelAS; }

		bool m_SupportRayTracing;

		// When true this mesh was loaded as a submesh slice; ray tracing is not supported.
		bool m_IsSubmesh = false;

	private:

		//光线追踪blas
		VkAccelerationStructureKHR m_BottomLevelAS;

		VansVKBuffer m_BottomLevelASBuffer;

		VansVKBuffer m_BLASScratchBuffer;

		//用于记录这个blas在整体中的索引
		int m_BLASIndex;
	};
}