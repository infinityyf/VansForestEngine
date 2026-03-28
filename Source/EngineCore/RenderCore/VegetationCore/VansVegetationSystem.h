#pragma once

#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace VansGraphics
{
	// ========================================================================
	// GPU-driven vegetation rendering system.
	//
	// Shared across all vegetation types (grass, brush, tree, etc.).
	// - Owns all GPU buffers for instanced vegetation
	// - Dispatches bone simulation + vertex skinning compute passes
	// - Issues indirect draw calls into the GBuffer
	// ========================================================================

	// Per-instance data uploaded once at init
	struct GrassInstance
	{
		glm::vec3 position;     // world XZ, Y = ground
		float     scale;        // random in [0.8, 1.2]
		float     rotation;     // random in [0, 2π]
		uint32_t  padding[3];   // align to 32 bytes
	};

	// Per-bone simulation state (GPU only after init)
	struct GrassBone
	{
		glm::vec4 position;     // world-space joint position
		glm::vec4 velocity;     // for Verlet integration (prevPosition)
		glm::vec4 restOffset;   // rest-pose offset from parent
	};

	// Push constants for bone simulation compute
	struct GrassSimPushConstants
	{
		float deltaTime;
		float time;
		float windStrength;
		float windFrequency;
		float windDirX;
		float windDirY;
		float stiffness;
		float damping;
		float softness;        // 0.0 = rigid (hard), 1.0 = fully soft
	};

	// Push constants for skinning compute
	struct GrassSkinningPushConstants
	{
		uint32_t instanceCount;
		uint32_t vertexCount;
		uint32_t boneCount;
		float    grassHeight;
	};

	// Template vertex for the grass blade mesh
	struct GrassVertex
	{
		glm::vec4 position;     // xyz = position, w = 0
		glm::vec4 normal;       // xyz = normal,   w = 0
		glm::vec2 uv;
		glm::vec2 padding;
	};

	class VansVegetationSystem
	{
	public:
		VansVegetationSystem() = default;
		~VansVegetationSystem();

		// ── Initialisation ──────────────────────────────────────────────
		void Init(VkDevice device, uint32_t instanceCount = 10000,
		          uint32_t boneCountPerInstance = 6);

		// ── Per-frame update: dispatches compute passes ────────────────
		void Update(VansVKCommandBuffer& computeCmd, float deltaTime, float time,
		            const glm::vec2& windDirection = glm::vec2(1.0f, 0.0f),
		            float windStrength = 2.0f, float windFrequency = 0.5f,
		            float stiffness = 15.0f, float damping = 0.92f,
		            float softness = 0.2f);

		// ── Draw: issues indirect indexed draw ─────────────────────────
		void Draw(VansVKCommandBuffer& graphicsCmd, VansGraphicsShader& shader,
		          GlobalStateData& globalState,
		          const std::vector<VkDescriptorSetLayout>& descSetLayouts,
		          const std::vector<VkDescriptorSet>& descSets,
		          int pushConstantMaterialIndex, int pushConstantTransformIndex);

		// ── Cleanup ────────────────────────────────────────────────────
		void Cleanup(VkDevice device);

		// ── Accessors ──────────────────────────────────────────────────
		VansVKBuffer& GetInstanceBuffer()         { return m_InstanceBuffer; }
		VansVKBuffer& GetSkinnedVertexBuffer()    { return m_SkinnedVertexBuffer; }
		VansVKBuffer& GetSkinnedNormalBuffer()    { return m_SkinnedNormalBuffer; }
		VansVKBuffer& GetIndirectDrawBuffer()     { return m_IndirectDrawBuffer; }

		uint32_t GetInstanceCount() const         { return m_InstanceCount; }
		uint32_t GetBoneCountPerInstance() const  { return m_BoneCountPerInstance; }
		uint32_t GetVertexCount() const           { return m_VertexCount; }
		uint32_t GetIndexCount() const            { return m_IndexCount; }

		// Descriptor sets for the compute passes
		VkDescriptorSetLayout m_BoneSimLayout      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_BoneSimDescSets;

		VkDescriptorSetLayout m_SkinningLayout     = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SkinningDescSets;

		// Descriptor set for draw pass (Set 3: skinned SSBOs)
		VkDescriptorSetLayout m_VegDrawLayout      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_VegDrawDescSets;

		VkDescriptorSetLayout GetVegDrawLayout() const { return m_VegDrawLayout; }
		VkDescriptorSet GetVegDrawDescSet() const { return m_VegDrawDescSets.empty() ? VK_NULL_HANDLE : m_VegDrawDescSets[0]; }

	private:
		// ── Init helpers ────────────────────────────────────────────────
		void CreateTemplateMesh(VkDevice device);
		void CreateInstanceBuffer(VkDevice device);
		void CreateBoneBuffer(VkDevice device);
		void CreateBoneMatrixBuffer(VkDevice device);
		void CreateSkinnedBuffers(VkDevice device);
		void CreateIndirectDrawBuffer(VkDevice device);
		void CreateDescriptorSets();
		void LoadComputeShaders(VkDevice device);
		void WriteBoneSimDescriptors();
		void WriteSkinningDescriptors();
		void WriteDrawDescriptors();

		// ── Configuration ───────────────────────────────────────────────
		uint32_t m_InstanceCount        = 0;
		uint32_t m_BoneCountPerInstance = 0;
		uint32_t m_VertexCount          = 0;
		uint32_t m_IndexCount           = 0;
		float    m_BladeHeight          = 0.5f;
		float    m_BladeWidthRoot       = 0.04f;

		// ── GPU Buffers ──────────────────────────────────────────────────
		VansVKBuffer m_InstanceBuffer;
		VansVKBuffer m_BoneBuffer;
		VansVKBuffer m_BoneMatrixBuffer;
		VansVKBuffer m_SkinnedVertexBuffer;
		VansVKBuffer m_SkinnedNormalBuffer;
		VansVKBuffer m_TemplateMeshBuffer;     // vertex data as SSBO for compute
		VansVKBuffer m_TemplateIndexBuffer;    // index buffer for draw
		VansVKBuffer m_TemplateVertexBuffer;   // vertex buffer for draw
		VansVKBuffer m_IndirectDrawBuffer;

		// ── Compute Shaders ──────────────────────────────────────────────
		VansComputeShader* m_BoneSimShader   = nullptr;
		VansComputeShader* m_SkinningShader  = nullptr;

		VkDevice m_Device = VK_NULL_HANDLE;
	};
}
