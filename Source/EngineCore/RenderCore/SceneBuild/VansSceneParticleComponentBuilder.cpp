#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include "VansSceneParticleComponentBuilder.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../ScriptCore/VansScriptContext.h"

namespace VansGraphics
{
VansSceneParticleReservation::~VansSceneParticleReservation()
{
	Reset();
}

VansSceneParticleReservation::VansSceneParticleReservation(
	VansSceneParticleReservation&& other) noexcept
{
	*this = std::move(other);
}

VansSceneParticleReservation& VansSceneParticleReservation::operator=(
	VansSceneParticleReservation&& other) noexcept
{
	if (this == &other)
		return *this;
	Reset();
	m_Manager = other.m_Manager;
	m_Asset = std::move(other.m_Asset);
	m_Instance = other.m_Instance;
	m_AssetGuid = std::move(other.m_AssetGuid);
	m_Error = std::move(other.m_Error);
	other.m_Manager = nullptr;
	other.m_Instance = {};
	return *this;
}

void VansSceneParticleReservation::Reset()
{
	if (m_Manager && m_Instance.IsValid())
		m_Manager->Destroy(m_Instance);
	m_Manager = nullptr;
	m_Asset.reset();
	m_Instance = {};
	m_AssetGuid.clear();
}

void VansSceneParticleReservation::Release()
{
	m_Manager = nullptr;
	m_Asset.reset();
	m_Instance = {};
	m_AssetGuid.clear();
}

VansSceneParticleReservation VansSceneParticleComponentBuilder::Reserve(
	VansScene& scene,
	const Vans::VansSceneParticleComponentConfig& config)
{
	VansSceneParticleReservation reservation;
	if (config.assetGuid.empty())
	{
		reservation.m_Error = "Particle component has no asset GUID";
		return reservation;
	}

	Vans::VansAssetGuid assetGuid;
	if (!Vans::VansAssetGuid::TryParse(config.assetGuid, assetGuid))
	{
		reservation.m_Error = "Particle asset GUID is invalid: '" + config.assetGuid + "'";
		return reservation;
	}

	reservation.m_Asset = Vans::VansProjectManager::Get().GetAssetObjectRepository()
		.ResolveLatest<VansParticleAsset>(assetGuid);
	if (!reservation.m_Asset)
	{
		reservation.m_Error = "Particle memory asset is unavailable: '" + config.assetGuid + "'";
		return reservation;
	}

	reservation.m_Manager = &scene.GetParticleManager();
	reservation.m_Instance = reservation.m_Manager->Create(reservation.m_Asset);
	if (!reservation.m_Instance.IsValid())
	{
		reservation.m_Error = "Particle instance capacity rejected asset: '" + config.assetGuid + "'";
		reservation.m_Manager = nullptr;
		reservation.m_Asset.reset();
		return reservation;
	}
	reservation.m_AssetGuid = assetGuid.ToString();
	return reservation;
}

VansSceneParticleBuildResult VansSceneParticleComponentBuilder::Build(
	VansScriptObject& object,
	const Vans::VansSceneParticleComponentConfig& config,
	VansSceneParticleReservation& reservation,
	const std::function<void()>& ensureObjectTransform)
{
	VansSceneParticleBuildResult result;
	if (!reservation.IsValid() || !reservation.m_Manager || !reservation.m_Asset)
	{
		result.error = reservation.Error().empty()
			? "Particle instance was not reserved" : reservation.Error();
		return result;
	}

	ensureObjectTransform();
	const VansParticleRuntime* runtime = reservation.m_Manager->Resolve(reservation.m_Instance);
	if (!runtime || !Vans::VansTransformStore::IsAllocated(object.m_TransformID))
	{
		result.error = "Particle reservation or entity Transform is unavailable";
		return result;
	}

	auto component = std::make_unique<VansScriptParticleComponent>();
	component->m_PlayOnAwake = config.playOnAwake;
	component->m_Manager = reservation.m_Manager;
	component->m_Instance = reservation.m_Instance;
	component->m_ParticleAssetGuid = reservation.m_AssetGuid;
	component->m_ParticleAsset = reservation.m_Asset;
	if (!reservation.m_Manager->SetOwnerWorldTransform(reservation.m_Instance,
		Vans::VansTransformStore::Read(object.m_TransformID).GetModelMatrix()))
	{
		result.error = "Particle owner Transform could not be initialized";
		return result;
	}
	if (component->m_PlayOnAwake)
		component->Play();

	reservation.Release();
	result.component = component.release();
	object.AddComponent(result.component);
	result.success = true;
	return result;
}
}
