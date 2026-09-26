#pragma once

#include "../VansScene.h"

#include "../../SceneCore/VansSceneParticleComponentConfig.h"

#include <functional>
#include <memory>
#include <string>

class VansScriptParticleComponent;

namespace VansGraphics
{
	class VansParticleAsset;

	class VansSceneParticleReservation
	{
	public:
		VansSceneParticleReservation() = default;
		~VansSceneParticleReservation();
		VansSceneParticleReservation(const VansSceneParticleReservation&) = delete;
		VansSceneParticleReservation& operator=(const VansSceneParticleReservation&) = delete;
		VansSceneParticleReservation(VansSceneParticleReservation&& other) noexcept;
		VansSceneParticleReservation& operator=(VansSceneParticleReservation&& other) noexcept;

		bool IsValid() const { return m_Instance.IsValid(); }
		const std::string& Error() const { return m_Error; }

	private:
		friend class VansSceneParticleComponentBuilder;
		void Reset();
		void Release();

		VansParticleManager* m_Manager = nullptr;
		std::shared_ptr<const VansParticleAsset> m_Asset;
		Vans::VansGenerationHandle m_Instance;
		std::string m_AssetGuid;
		std::string m_Error;
	};

	struct VansSceneParticleBuildResult
	{
		bool success = false;
		std::string error;
		VansScriptParticleComponent* component = nullptr;
	};

	class VansSceneParticleComponentBuilder
	{
	public:
		static VansSceneParticleReservation Reserve(
			VansScene& scene,
			const Vans::VansSceneParticleComponentConfig& particleConfig);

		static VansSceneParticleBuildResult Build(
			VansScriptObject& object,
			const Vans::VansSceneParticleComponentConfig& particleConfig,
			VansSceneParticleReservation& reservation,
			const std::function<void()>& ensureObjectTransform);
	};
}
