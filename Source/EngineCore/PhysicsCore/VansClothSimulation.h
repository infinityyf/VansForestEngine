#pragma once

#include <GLM/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace nv::cloth
{
	class Cloth;
	class Fabric;
}

namespace VansEngine
{
	struct VansClothMeshData;

	struct VansClothSimulationConfig
	{
		float stiffness = 0.8f;
		float stiffnessFrequency = 60.0f;
		float damping = 0.1f;
		float friction = 0.0f;
		float gravity = -9.81f;
	};

	// Owns the complete NvCloth fabric/cloth lifecycle and solver registration.
	class VansClothSimulation final
	{
	public:
		VansClothSimulation() = default;
		~VansClothSimulation();
		VansClothSimulation(const VansClothSimulation&) = delete;
		VansClothSimulation& operator=(const VansClothSimulation&) = delete;

		bool Initialize(
			const VansClothMeshData& mesh,
			const VansClothSimulationConfig& config,
			std::string& error);
		void Shutdown();

		bool SetEnabled(bool enabled);
		bool IsEnabled() const { return m_RegisteredWithSolver; }
		bool IsReady() const { return m_Cloth != nullptr; }

		void SetPinnedParticles(
			const std::vector<std::uint32_t>& particleIndices,
			const std::vector<glm::vec3>& positions);
		void SetCollisionSpheres(const std::vector<glm::vec4>& spheres);
		void ReadParticlePositions(std::vector<glm::vec3>& positions) const;

	private:
		nv::cloth::Fabric* m_Fabric = nullptr;
		nv::cloth::Cloth* m_Cloth = nullptr;
		bool m_RegisteredWithSolver = false;
	};
}
