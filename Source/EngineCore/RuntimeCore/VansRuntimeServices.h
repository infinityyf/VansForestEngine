#pragma once

#include <string>

namespace VansEngine
{
	struct VansAudioDeviceConfig;
}

namespace Vans
{
	enum class VansRuntimeServiceState
	{
		Stopped,
		Starting,
		Ready,
		Simulating,
		Quiesced,
		ServicesStopped
	};

	class VansRuntimeServices final
	{
	public:
		VansRuntimeServices() = default;
		~VansRuntimeServices();

		VansRuntimeServices(const VansRuntimeServices&) = delete;
		VansRuntimeServices& operator=(const VansRuntimeServices&) = delete;

		bool Start(const VansEngine::VansAudioDeviceConfig& audioConfig, std::string& error);
		void StartSimulation();
		void PauseSimulation();
		void StopSimulation();
		void ShutdownServices();
		void ShutdownJobs();
		void Shutdown();

		VansRuntimeServiceState GetState() const { return m_State; }
		bool IsStarted() const;
		bool HasActiveResources() const;
		bool IsAudioAvailable() const { return m_AudioAvailable; }
		bool IsSimulationRunning() const;

	private:
		VansRuntimeServiceState m_State = VansRuntimeServiceState::Stopped;
		bool m_JobSystemStarted = false;
		bool m_PhysicsInitializationAttempted = false;
		bool m_PhysicsInitialized = false;
		bool m_AudioInitializationAttempted = false;
		bool m_AudioAvailable = false;
	};
}
