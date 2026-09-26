#include "VansRuntimeServices.h"

#include "../AudioCore/VansAudioSystem.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../Util/VansJobSystem.h"

namespace Vans
{
	VansRuntimeServices::~VansRuntimeServices()
	{
		Shutdown();
	}

	bool VansRuntimeServices::Start(
		const VansEngine::VansAudioDeviceConfig& audioConfig,
		std::string& error)
	{
		if (IsStarted())
			return true;
		if (m_State != VansRuntimeServiceState::Stopped)
		{
			error = "Runtime services are not stopped";
			return false;
		}

		m_State = VansRuntimeServiceState::Starting;
		VansJobSystem::Get().Initialize();
		m_JobSystemStarted = true;

		m_PhysicsInitializationAttempted = true;
		m_PhysicsInitialized = VansEngine::VansPhysicsSystem::GetInstance().Initialize();
		if (!m_PhysicsInitialized)
		{
			error = "Physics system initialization failed";
			ShutdownServices();
			ShutdownJobs();
			m_State = VansRuntimeServiceState::Stopped;
			return false;
		}

		m_AudioInitializationAttempted = true;
		m_AudioAvailable = VansEngine::VansAudioSystem::GetInstance().Initialize(audioConfig);
		if (!m_AudioAvailable && audioConfig.m_HrtfMode == VansEngine::VansAudioHrtfMode::Required)
		{
			error = "Required HRTF audio configuration could not be initialized";
			ShutdownServices();
			ShutdownJobs();
			m_State = VansRuntimeServiceState::Stopped;
			return false;
		}
		m_State = VansRuntimeServiceState::Ready;
		error.clear();
		return true;
	}

	void VansRuntimeServices::StartSimulation()
	{
		if (!m_PhysicsInitialized)
			return;
		VansEngine::VansPhysicsSystem::GetInstance().StartSimulation();
		m_State = VansRuntimeServiceState::Simulating;
	}

	void VansRuntimeServices::PauseSimulation()
	{
		if (!m_PhysicsInitialized)
			return;
		VansEngine::VansPhysicsSystem::GetInstance().PauseSimulation();
		m_State = VansRuntimeServiceState::Quiesced;
	}

	void VansRuntimeServices::StopSimulation()
	{
		if (!m_PhysicsInitialized)
			return;
		VansEngine::VansPhysicsSystem::GetInstance().StopSimulation();
		m_State = VansRuntimeServiceState::Quiesced;
	}

	void VansRuntimeServices::ShutdownServices()
	{
		if (m_PhysicsInitializationAttempted)
		{
			VansEngine::VansPhysicsSystem::GetInstance().StopSimulation();
			VansEngine::VansPhysicsSystem::GetInstance().Shutdown();
			m_PhysicsInitializationAttempted = false;
			m_PhysicsInitialized = false;
		}

		if (m_AudioInitializationAttempted)
		{
			VansEngine::VansAudioSystem::GetInstance().Shutdown();
			m_AudioInitializationAttempted = false;
			m_AudioAvailable = false;
		}

		if (m_JobSystemStarted)
			m_State = VansRuntimeServiceState::ServicesStopped;
		else
			m_State = VansRuntimeServiceState::Stopped;
	}

	void VansRuntimeServices::ShutdownJobs()
	{
		if (m_JobSystemStarted)
		{
			VansJobSystem::Get().Shutdown();
			m_JobSystemStarted = false;
		}
		m_State = VansRuntimeServiceState::Stopped;
	}

	void VansRuntimeServices::Shutdown()
	{
		PauseSimulation();
		ShutdownServices();
		ShutdownJobs();
		m_State = VansRuntimeServiceState::Stopped;
	}

	bool VansRuntimeServices::IsStarted() const
	{
		return m_State == VansRuntimeServiceState::Ready ||
			m_State == VansRuntimeServiceState::Simulating ||
			m_State == VansRuntimeServiceState::Quiesced;
	}

	bool VansRuntimeServices::HasActiveResources() const
	{
		return m_JobSystemStarted || m_PhysicsInitializationAttempted ||
			m_AudioInitializationAttempted;
	}

	bool VansRuntimeServices::IsSimulationRunning() const
	{
		return m_PhysicsInitialized &&
			VansEngine::VansPhysicsSystem::GetInstance().IsSimulationRunning();
	}
}
