#include "VansClothNode.h"

#include "../AnimationCore/VansAnimationController.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../SceneRuntime/Transform/VansTransformStore.h"

#include <GLM/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace VansEngine
{
	namespace
	{
		std::uint16_t PackHalf(float value)
		{
			return glm::packHalf1x16(value);
		}
	}

	VansClothNode::VansClothNode()
	{
		m_Enabled = false;
	}

	VansClothNode::~VansClothNode()
	{
		Shutdown();
	}

	bool VansClothNode::Initialize(
		const VansClothNodeConfig& config,
		VansClothMeshData&& mesh,
		std::uint32_t targetTransformId,
		std::string name,
		std::string& error)
	{
		Shutdown();
		error.clear();
		if (!Vans::VansTransformStore::IsAllocated(targetTransformId))
		{
			error = "Cloth target Transform is not allocated";
			return false;
		}

		if (!m_Simulation.Initialize(mesh, config.simulation, error))
			return false;

		m_TargetTransformId = targetTransformId;
		m_Name = std::move(name);
		m_VertexCount = mesh.vertexCount;
		m_PackedVertexStride = mesh.packedVertexStride;
		m_HasTangent = mesh.hasTangent;
		m_AttachOffsetY = config.attachOffsetY;
		m_FollowBones = config.followBones;
		m_RestTexCoords = std::move(mesh.texCoords);
		m_OriginalTriangles = std::move(mesh.originalTriangles);
		m_OriginalToParticle = std::move(mesh.originalToParticle);
		m_PinnedParticles = std::move(mesh.pinnedParticles);
		m_PinnedSourceIndices = std::move(mesh.pinnedSourceIndices);
		m_PinnedLocalPositions = std::move(mesh.pinnedLocalPositions);
		m_CollisionBindings = config.collisionBindings;

		const std::size_t vertexCount = static_cast<std::size_t>(m_VertexCount);
		m_RenderData.assign(vertexCount * static_cast<std::size_t>(m_PackedVertexStride), 0u);
		m_ModelPositions.resize(vertexCount);
		m_Normals.resize(vertexCount);
		if (m_HasTangent)
		{
			m_Tangents.resize(vertexCount);
			m_Bitangents.resize(vertexCount);
		}

		SetEnabled(config.enabled);
		if (config.enabled && !IsEnabled())
		{
			error = "Cloth could not register with the solver";
			Shutdown();
			return false;
		}
		return true;
	}

	void VansClothNode::Shutdown()
	{
		if (IsEnabled())
			SetEnabled(false);
		m_Simulation.Shutdown();
		m_RenderData.clear();
		m_RestTexCoords.clear();
		m_OriginalTriangles.clear();
		m_OriginalToParticle.clear();
		m_PinnedParticles.clear();
		m_PinnedSourceIndices.clear();
		m_PinnedLocalPositions.clear();
		m_CollisionBindings.clear();
		m_PinnedSkinData.clear();
		m_PreviousPinnedWorldPositions.clear();
		m_TargetPinnedWorldPositions.clear();
		m_InterpolatedPinnedWorldPositions.clear();
		m_ParticlePositions.clear();
		m_ModelPositions.clear();
		m_Normals.clear();
		m_Tangents.clear();
		m_Bitangents.clear();
		m_AnimationNode = nullptr;
		m_TargetTransformId = UINT32_MAX;
		m_RenderDataVersion = 0;
		m_AttachOffsetY = 0.0f;
		m_VertexCount = 0;
		m_PackedVertexStride = 8;
		m_HasTangent = false;
		m_FollowBones = false;
		m_PreviousPinsInitialized = false;
		m_Name.clear();
		m_Enabled = false;
	}

	void VansClothNode::OnEnable()
	{
		if (!m_Simulation.SetEnabled(true))
			m_Enabled = false;
	}

	void VansClothNode::OnDisable()
	{
		m_Simulation.SetEnabled(false);
	}

	bool VansClothNode::BindAnimation(
		const std::vector<VansGraphics::VansClothPinSkinData>& authoredBindings,
		VansGraphics::VansAnimationNode* animationNode,
		std::string& error)
	{
		error.clear();
		if (!m_FollowBones)
		{
			error = "Cloth bone binding requires followBones";
			return false;
		}
		if (!animationNode)
		{
			error = "Cloth bone binding requires an AnimationNode";
			return false;
		}
		VansGraphics::VansAnimationController* controller = animationNode->GetController();
		if (!controller)
		{
			error = "Cloth bone binding requires an AnimationController";
			return false;
		}

		std::vector<VansGraphics::VansClothPinSkinData> resolved;
		resolved.reserve(m_PinnedSourceIndices.size());
		for (std::uint32_t sourceIndex : m_PinnedSourceIndices)
		{
			if (sourceIndex >= authoredBindings.size())
			{
				error = "Prepared cloth pin references an invalid authored binding";
				return false;
			}
			resolved.push_back(authoredBindings[sourceIndex]);
		}

		const auto& cachedGlobals = controller->GetCachedGlobalTransforms();
		const glm::mat4 clothWorld =
			Vans::VansTransformStore::Read(m_TargetTransformId).GetModelMatrix();
		const glm::mat4 rootWorld = Vans::VansTransformStore::Read(
			animationNode->GetTransformID()).GetModelMatrix();
		for (std::size_t pinIndex = 0; pinIndex < resolved.size(); ++pinIndex)
		{
			auto& skin = resolved[pinIndex];
			const glm::vec4 vertexWorld = clothWorld *
				glm::vec4(m_PinnedLocalPositions[pinIndex], 1.0f);
			for (std::uint32_t influenceIndex = 0;
				influenceIndex < skin.boneCount; ++influenceIndex)
			{
				auto& influence = skin.boneWeights[influenceIndex];
				if (influence.boneIndex < 0 ||
					influence.boneIndex >= static_cast<int>(cachedGlobals.size()))
				{
					error = "Cloth bone binding index exceeds the animation pose";
					return false;
				}
				const glm::mat4 boneWorld = rootWorld * cachedGlobals[influence.boneIndex];
				influence.boneLocalOffset = glm::vec3(glm::inverse(boneWorld) * vertexWorld);
			}
		}

		m_PinnedSkinData = std::move(resolved);
		m_AnimationNode = animationNode;
		return true;
	}

	glm::vec3 VansClothNode::ComputePinnedWorldPosition(std::size_t pinIndex) const
	{
		if (m_FollowBones && m_AnimationNode && pinIndex < m_PinnedSkinData.size())
		{
			const auto& skin = m_PinnedSkinData[pinIndex];
			if (skin.boneCount > 0)
			{
				const VansGraphics::VansAnimationController* controller =
					m_AnimationNode->GetController();
				if (controller)
				{
					const auto& cachedGlobals = controller->GetCachedGlobalTransforms();
					const glm::mat4 rootWorld = Vans::VansTransformStore::Read(
						m_AnimationNode->GetTransformID()).GetModelMatrix();
					glm::vec4 world(0.0f);
					for (std::uint32_t influenceIndex = 0;
						influenceIndex < skin.boneCount; ++influenceIndex)
					{
						const auto& influence = skin.boneWeights[influenceIndex];
						if (influence.boneIndex < 0 ||
							influence.boneIndex >= static_cast<int>(cachedGlobals.size()))
						{
							continue;
						}
						const glm::mat4 boneWorld = rootWorld * cachedGlobals[influence.boneIndex];
						world += influence.weight *
							(boneWorld * glm::vec4(influence.boneLocalOffset, 1.0f));
					}
					world.y += m_AttachOffsetY;
					return glm::vec3(world);
				}
			}
		}

		const glm::mat4 model =
			Vans::VansTransformStore::Read(m_TargetTransformId).GetModelMatrix();
		glm::vec4 world = model * glm::vec4(m_PinnedLocalPositions[pinIndex], 1.0f);
		world.y += m_AttachOffsetY;
		return glm::vec3(world);
	}

	void VansClothNode::ComputePinnedTargets()
	{
		if (!m_Simulation.IsReady() || m_PinnedParticles.empty() ||
			!Vans::VansTransformStore::IsAllocated(m_TargetTransformId))
		{
			return;
		}
		m_TargetPinnedWorldPositions.resize(m_PinnedParticles.size());
		for (std::size_t pinIndex = 0; pinIndex < m_PinnedParticles.size(); ++pinIndex)
			m_TargetPinnedWorldPositions[pinIndex] = ComputePinnedWorldPosition(pinIndex);
		if (!m_PreviousPinsInitialized)
		{
			m_PreviousPinnedWorldPositions = m_TargetPinnedWorldPositions;
			m_PreviousPinsInitialized = true;
		}
	}

	void VansClothNode::WritePinnedParticlesLerped(float alpha)
	{
		if (m_TargetPinnedWorldPositions.size() != m_PreviousPinnedWorldPositions.size())
			return;
		m_InterpolatedPinnedWorldPositions.resize(m_TargetPinnedWorldPositions.size());
		for (std::size_t pinIndex = 0;
			pinIndex < m_InterpolatedPinnedWorldPositions.size(); ++pinIndex)
		{
			m_InterpolatedPinnedWorldPositions[pinIndex] =
				m_PreviousPinnedWorldPositions[pinIndex] + alpha *
				(m_TargetPinnedWorldPositions[pinIndex] - m_PreviousPinnedWorldPositions[pinIndex]);
		}
		m_Simulation.SetPinnedParticles(
			m_PinnedParticles, m_InterpolatedPinnedWorldPositions);
	}

	void VansClothNode::CommitPinnedTargets()
	{
		if (!m_TargetPinnedWorldPositions.empty())
			m_PreviousPinnedWorldPositions = m_TargetPinnedWorldPositions;
	}

	void VansClothNode::SetCollisionSpheres(const std::vector<glm::vec4>& spheres)
	{
		m_Simulation.SetCollisionSpheres(spheres);
	}

	bool VansClothNode::CacheCollisionTransform(
		std::size_t bindingIndex,
		std::uint32_t transformId)
	{
		if (bindingIndex >= m_CollisionBindings.size() ||
			!Vans::VansTransformStore::IsAllocated(transformId))
		{
			return false;
		}
		m_CollisionBindings[bindingIndex].transformId = transformId;
		return true;
	}

	bool VansClothNode::BuildRenderData()
	{
		if (!m_Simulation.IsReady() || m_RenderData.empty() ||
			!Vans::VansTransformStore::IsAllocated(m_TargetTransformId))
		{
			return false;
		}

		m_Simulation.ReadParticlePositions(m_ParticlePositions);
		if (m_ParticlePositions.empty())
			return false;
		const glm::mat4 inverseModel = glm::inverse(
			Vans::VansTransformStore::Read(m_TargetTransformId).GetModelMatrix());
		for (int vertexIndex = 0; vertexIndex < m_VertexCount; ++vertexIndex)
		{
			const std::uint32_t particleIndex = m_OriginalToParticle[vertexIndex];
			if (particleIndex >= m_ParticlePositions.size())
				return false;
			m_ModelPositions[vertexIndex] = glm::vec3(
				inverseModel * glm::vec4(m_ParticlePositions[particleIndex], 1.0f));
		}

		std::fill(m_Normals.begin(), m_Normals.end(), glm::vec3(0.0f));
		if (m_HasTangent)
		{
			std::fill(m_Tangents.begin(), m_Tangents.end(), glm::vec3(0.0f));
			std::fill(m_Bitangents.begin(), m_Bitangents.end(), glm::vec3(0.0f));
		}
		for (std::size_t triangle = 0; triangle < m_OriginalTriangles.size(); triangle += 3u)
		{
			const std::uint32_t a = m_OriginalTriangles[triangle + 0u];
			const std::uint32_t b = m_OriginalTriangles[triangle + 1u];
			const std::uint32_t c = m_OriginalTriangles[triangle + 2u];
			const glm::vec3 edgeA = m_ModelPositions[b] - m_ModelPositions[a];
			const glm::vec3 edgeB = m_ModelPositions[c] - m_ModelPositions[a];
			const glm::vec3 normal = glm::cross(edgeA, edgeB);
			m_Normals[a] += normal;
			m_Normals[b] += normal;
			m_Normals[c] += normal;

			if (m_HasTangent)
			{
				const glm::vec2 uvA(m_RestTexCoords[a * 2u], m_RestTexCoords[a * 2u + 1u]);
				const glm::vec2 uvB(m_RestTexCoords[b * 2u], m_RestTexCoords[b * 2u + 1u]);
				const glm::vec2 uvC(m_RestTexCoords[c * 2u], m_RestTexCoords[c * 2u + 1u]);
				const glm::vec2 deltaA = uvB - uvA;
				const glm::vec2 deltaB = uvC - uvA;
				const float determinant = deltaA.x * deltaB.y - deltaB.x * deltaA.y;
				const float inverse = std::abs(determinant) > 1.0e-8f ? 1.0f / determinant : 0.0f;
				const glm::vec3 tangent = inverse * (deltaB.y * edgeA - deltaA.y * edgeB);
				const glm::vec3 bitangent = inverse * (-deltaB.x * edgeA + deltaA.x * edgeB);
				m_Tangents[a] += tangent;
				m_Tangents[b] += tangent;
				m_Tangents[c] += tangent;
				m_Bitangents[a] += bitangent;
				m_Bitangents[b] += bitangent;
				m_Bitangents[c] += bitangent;
			}
		}

		for (int vertexIndex = 0; vertexIndex < m_VertexCount; ++vertexIndex)
		{
			const glm::vec3 position = m_ModelPositions[vertexIndex];
			glm::vec3 normal = m_Normals[vertexIndex];
			const float normalLength = glm::length(normal);
			if (normalLength > 1.0e-6f)
				normal /= normalLength;
			const int base = vertexIndex * m_PackedVertexStride;
			m_RenderData[base + 0] = PackHalf(position.x);
			m_RenderData[base + 1] = PackHalf(position.y);
			m_RenderData[base + 2] = PackHalf(position.z);
			m_RenderData[base + 3] = PackHalf(m_RestTexCoords[vertexIndex * 2 + 0]);
			m_RenderData[base + 4] = PackHalf(m_RestTexCoords[vertexIndex * 2 + 1]);
			m_RenderData[base + 5] = PackHalf(normal.x);
			m_RenderData[base + 6] = PackHalf(normal.y);
			m_RenderData[base + 7] = PackHalf(normal.z);

			if (m_HasTangent)
			{
				glm::vec3 tangent = m_Tangents[vertexIndex] -
					normal * glm::dot(normal, m_Tangents[vertexIndex]);
				const float tangentLength = glm::length(tangent);
				if (tangentLength > 1.0e-6f)
					tangent /= tangentLength;
				glm::vec3 bitangent = m_Bitangents[vertexIndex] -
					normal * glm::dot(normal, m_Bitangents[vertexIndex]) -
					tangent * glm::dot(tangent, m_Bitangents[vertexIndex]);
				const float bitangentLength = glm::length(bitangent);
				if (bitangentLength > 1.0e-6f)
					bitangent /= bitangentLength;
				const float handedness = bitangentLength > 1.0e-6f &&
					glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f
					? -1.0f : 1.0f;
				m_RenderData[base + 8] = PackHalf(tangent.x);
				m_RenderData[base + 9] = PackHalf(tangent.y);
				m_RenderData[base + 10] = PackHalf(tangent.z);
				m_RenderData[base + 11] = PackHalf(handedness);
			}
		}

		++m_RenderDataVersion;
		return true;
	}

	VansClothVertexView VansClothNode::GetRenderData() const
	{
		return {
			m_RenderData.empty() ? nullptr : m_RenderData.data(),
			m_RenderData.size(),
			m_RenderDataVersion };
	}
}
