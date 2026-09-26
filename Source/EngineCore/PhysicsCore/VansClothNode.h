#pragma once

#include "../AnimationCore/VansClothBindingResolver.h"
#include "../VansNode.h"
#include "VansClothMeshPrep.h"
#include "VansClothSimulation.h"

#include <GLM/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace VansGraphics
{
	class VansAnimationNode;
}

namespace VansEngine
{
	struct VansClothCollisionBinding
	{
		std::string sceneObjectName;
		std::string renderNodeName;
		std::uint32_t transformId = UINT32_MAX;
		float radius = 1.0f;
	};

	struct VansClothNodeConfig
	{
		bool enabled = false;
		VansClothSimulationConfig simulation;
		float attachOffsetY = 0.0f;
		bool followBones = false;
		std::vector<VansClothCollisionBinding> collisionBindings;
	};

	struct VansClothVertexView
	{
		const std::uint16_t* data = nullptr;
		std::size_t elementCount = 0;
		std::uint64_t version = 0;

		std::size_t ByteSize() const { return elementCount * sizeof(std::uint16_t); }
		bool IsValid() const { return data != nullptr && elementCount > 0 && version > 0; }
	};

	// 运行期只负责编排准备后的网格、动画目标、仿真状态与渲染上传快照。
	class VansClothNode final : public VansGraphics::VansNode
	{
	public:
		VansClothNode();
		~VansClothNode() override;

		bool Initialize(
			const VansClothNodeConfig& config,
			VansClothMeshData&& mesh,
			std::uint32_t targetTransformId,
			std::string name,
			std::string& error);
		void Shutdown();

		bool BuildRenderData();
		VansClothVertexView GetRenderData() const;

		void SetCollisionSpheres(const std::vector<glm::vec4>& spheres);
		const std::vector<VansClothCollisionBinding>& GetCollisionBindings() const
		{
			return m_CollisionBindings;
		}
		bool CacheCollisionTransform(std::size_t bindingIndex, std::uint32_t transformId);

		bool BindAnimation(
			const std::vector<VansGraphics::VansClothPinSkinData>& authoredBindings,
			VansGraphics::VansAnimationNode* animationNode,
			std::string& error);
		VansGraphics::VansAnimationNode* GetAnimationNode() const { return m_AnimationNode; }
		bool IsFollowBones() const { return m_FollowBones; }

		void ComputePinnedTargets();
		void WritePinnedParticlesLerped(float alpha);
		void CommitPinnedTargets();

		const std::string& GetName() const { return m_Name; }
		void SetName(std::string name) { m_Name = std::move(name); }

	protected:
		void OnEnable() override;
		void OnDisable() override;

	private:
		glm::vec3 ComputePinnedWorldPosition(std::size_t pinIndex) const;

		VansClothSimulation m_Simulation;
		std::vector<std::uint16_t> m_RenderData;
		std::vector<float> m_RestTexCoords;
		std::vector<std::uint32_t> m_OriginalTriangles;
		std::vector<std::uint32_t> m_OriginalToParticle;
		std::vector<std::uint32_t> m_PinnedParticles;
		std::vector<std::uint32_t> m_PinnedSourceIndices;
		std::vector<glm::vec3> m_PinnedLocalPositions;
		std::vector<VansClothCollisionBinding> m_CollisionBindings;
		std::vector<VansGraphics::VansClothPinSkinData> m_PinnedSkinData;
		std::vector<glm::vec3> m_PreviousPinnedWorldPositions;
		std::vector<glm::vec3> m_TargetPinnedWorldPositions;
		std::vector<glm::vec3> m_InterpolatedPinnedWorldPositions;
		std::vector<glm::vec3> m_ParticlePositions;
		std::vector<glm::vec3> m_ModelPositions;
		std::vector<glm::vec3> m_Normals;
		std::vector<glm::vec3> m_Tangents;
		std::vector<glm::vec3> m_Bitangents;
		VansGraphics::VansAnimationNode* m_AnimationNode = nullptr;
		std::uint32_t m_TargetTransformId = UINT32_MAX;
		std::uint64_t m_RenderDataVersion = 0;
		float m_AttachOffsetY = 0.0f;
		int m_VertexCount = 0;
		int m_PackedVertexStride = 8;
		bool m_HasTangent = false;
		bool m_FollowBones = false;
		bool m_PreviousPinsInitialized = false;
		std::string m_Name;
	};
}
