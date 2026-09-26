#include "VansClothSimulation.h"

#include "VansClothMeshPrep.h"
#include "VansClothSystem.h"
#include "VansPhysics.h"

#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvClothExt/ClothFabricCooker.h>
#include <NvClothExt/ClothMeshDesc.h>

#include <cmath>

namespace VansEngine
{
	VansClothSimulation::~VansClothSimulation()
	{
		Shutdown();
	}

	bool VansClothSimulation::Initialize(
		const VansClothMeshData& mesh,
		const VansClothSimulationConfig& config,
		std::string& error)
	{
		Shutdown();
		error.clear();
		if (mesh.particles.empty() || mesh.particleTriangles.empty())
		{
			error = "Cloth simulation requires prepared particles and triangles";
			return false;
		}
		if (!std::isfinite(config.stiffness) || config.stiffness < 0.0f || config.stiffness > 1.0f ||
			!std::isfinite(config.stiffnessFrequency) || config.stiffnessFrequency <= 0.0f ||
			!std::isfinite(config.damping) || config.damping < 0.0f || config.damping > 1.0f ||
			!std::isfinite(config.friction) || config.friction < 0.0f ||
			!std::isfinite(config.gravity))
		{
			error = "Cloth simulation configuration is invalid";
			return false;
		}

		VansClothSystem& system = VansClothSystem::GetInstance();
		nv::cloth::Factory* factory = system.GetFactory();
		if (!factory || !system.GetSolver())
		{
			error = "Cloth factory and solver are not initialized";
			return false;
		}

		std::vector<physx::PxVec4> particles;
		particles.reserve(mesh.particles.size());
		std::vector<float> inverseMasses;
		inverseMasses.reserve(mesh.particles.size());
		for (const glm::vec4& particle : mesh.particles)
		{
			particles.emplace_back(particle.x, particle.y, particle.z, particle.w);
			inverseMasses.push_back(particle.w);
		}

		nv::cloth::ClothMeshDesc descriptor;
		descriptor.points.data = particles.data();
		descriptor.points.count = static_cast<std::uint32_t>(particles.size());
		descriptor.points.stride = sizeof(physx::PxVec4);
		descriptor.invMasses.data = inverseMasses.data();
		descriptor.invMasses.count = static_cast<std::uint32_t>(inverseMasses.size());
		descriptor.invMasses.stride = sizeof(float);
		descriptor.triangles.data = mesh.particleTriangles.data();
		descriptor.triangles.count = static_cast<std::uint32_t>(mesh.particleTriangles.size() / 3u);
		descriptor.triangles.stride = 3u * sizeof(std::uint32_t);

		const physx::PxVec3 gravityDirection(
			0.0f, config.gravity < 0.0f ? -1.0f : 1.0f, 0.0f);
		m_Fabric = NvClothCookFabricFromMesh(
			factory, descriptor, gravityDirection, nullptr, true);
		if (!m_Fabric)
		{
			error = "NvCloth fabric cooking failed";
			return false;
		}

		const nv::cloth::Range<const physx::PxVec4> particleRange(
			particles.data(), particles.data() + particles.size());
		m_Cloth = factory->createCloth(particleRange, *m_Fabric);
		if (!m_Cloth)
		{
			error = "NvCloth instance creation failed";
			Shutdown();
			return false;
		}

		m_Cloth->setStiffnessFrequency(config.stiffnessFrequency);
		m_Cloth->setGravity(physx::PxVec3(0.0f, config.gravity, 0.0f));
		m_Cloth->setDamping(physx::PxVec3(config.damping));
		m_Cloth->setFriction(config.friction);
		m_Cloth->setLinearInertia(physx::PxVec3(0.0f));
		m_Cloth->setAngularInertia(physx::PxVec3(0.0f));
		m_Cloth->setCentrifugalInertia(physx::PxVec3(0.0f));
		m_Cloth->enableContinuousCollision(true);
		m_Cloth->setCollisionMassScale(1.0f);

		std::vector<nv::cloth::PhaseConfig> phaseConfigs(m_Fabric->getNumPhases());
		for (std::uint32_t phaseIndex = 0;
			phaseIndex < static_cast<std::uint32_t>(phaseConfigs.size()); ++phaseIndex)
		{
			phaseConfigs[phaseIndex].mPhaseIndex = static_cast<std::uint16_t>(phaseIndex);
			phaseConfigs[phaseIndex].mStiffness = config.stiffness;
		}
		m_Cloth->setPhaseConfig(nv::cloth::Range<nv::cloth::PhaseConfig>(
			phaseConfigs.data(), phaseConfigs.data() + phaseConfigs.size()));
		return true;
	}

	void VansClothSimulation::Shutdown()
	{
		SetEnabled(false);
		if (m_Cloth)
		{
			NV_CLOTH_DELETE(m_Cloth);
			m_Cloth = nullptr;
		}
		if (m_Fabric)
		{
			m_Fabric->decRefCount();
			m_Fabric = nullptr;
		}
		m_RegisteredWithSolver = false;
	}

	bool VansClothSimulation::SetEnabled(bool enabled)
	{
		if (m_RegisteredWithSolver == enabled)
			return true;
		nv::cloth::Solver* solver = VansClothSystem::GetInstance().GetSolver();
		if (!m_Cloth || !solver)
			return !enabled;
		if (enabled)
			solver->addCloth(m_Cloth);
		else
			solver->removeCloth(m_Cloth);
		m_RegisteredWithSolver = enabled;
		return true;
	}

	void VansClothSimulation::SetPinnedParticles(
		const std::vector<std::uint32_t>& particleIndices,
		const std::vector<glm::vec3>& positions)
	{
		if (!m_Cloth || particleIndices.size() != positions.size())
			return;
		nv::cloth::MappedRange<physx::PxVec4> current = m_Cloth->getCurrentParticles();
		nv::cloth::MappedRange<physx::PxVec4> previous = m_Cloth->getPreviousParticles();
		for (std::size_t index = 0; index < particleIndices.size(); ++index)
		{
			const std::uint32_t particleIndex = particleIndices[index];
			if (particleIndex >= current.size())
				continue;
			const glm::vec3& position = positions[index];
			current[particleIndex].x = position.x;
			current[particleIndex].y = position.y;
			current[particleIndex].z = position.z;
			previous[particleIndex].x = position.x;
			previous[particleIndex].y = position.y;
			previous[particleIndex].z = position.z;
		}
	}

	void VansClothSimulation::SetCollisionSpheres(const std::vector<glm::vec4>& spheres)
	{
		if (!m_Cloth)
			return;
		std::vector<physx::PxVec4> nativeSpheres;
		nativeSpheres.reserve(spheres.size());
		for (const glm::vec4& sphere : spheres)
			nativeSpheres.emplace_back(sphere.x, sphere.y, sphere.z, sphere.w);
		if (nativeSpheres.empty())
		{
			const nv::cloth::Range<const physx::PxVec4> empty;
			m_Cloth->setSpheres(empty, empty);
			return;
		}
		const nv::cloth::Range<const physx::PxVec4> range(
			nativeSpheres.data(), nativeSpheres.data() + nativeSpheres.size());
		m_Cloth->setSpheres(range, range);
	}

	void VansClothSimulation::ReadParticlePositions(std::vector<glm::vec3>& positions) const
	{
		positions.clear();
		if (!m_Cloth)
			return;
		nv::cloth::MappedRange<physx::PxVec4> particles = m_Cloth->getCurrentParticles();
		positions.reserve(particles.size());
		for (const physx::PxVec4& particle : particles)
			positions.emplace_back(particle.x, particle.y, particle.z);
	}
}
