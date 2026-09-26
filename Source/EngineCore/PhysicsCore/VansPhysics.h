#pragma once

#include "VansPhysicsTiming.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include <glm/vec3.hpp>

namespace VansEngine
{
	class VansPhysicsNativeAccess;

	// Engine-facing physics lifecycle and scheduling facade. PhysX objects are
	// deliberately hidden behind the PhysicsCore-only native access bridge.
	class VansPhysicsSystem
	{
	public:
		static VansPhysicsSystem& GetInstance();

		bool Initialize();
		void Shutdown();

		void StartSimulation();
		void StopSimulation();
		void PauseSimulation();
		void ResumeSimulation();
		bool IsSimulationRunning() const { return m_IsRunning; }

		bool SetTiming(const VansPhysicsTiming& timing);
		VansPhysicsTiming GetTiming() const;

		void SetGravity(const glm::vec3& gravity);
		glm::vec3 GetGravity() const;

		std::mutex& GetSimulationMutex();

		using PhysicsStepCallback = std::function<void(float dt)>;
		void SetPreSimulateCallback(PhysicsStepCallback callback);

		bool IsPvdConnected() const;

	private:
		friend class VansPhysicsNativeAccess;
		struct NativeState;

		VansPhysicsSystem();
		~VansPhysicsSystem();
		VansPhysicsSystem(const VansPhysicsSystem&) = delete;
		VansPhysicsSystem& operator=(const VansPhysicsSystem&) = delete;

		void SimulationThread();

		std::unique_ptr<NativeState> m_Native;
		std::thread m_SimulationThread;
		std::atomic<bool> m_IsRunning{ false };
		std::atomic<bool> m_ShouldExit{ false };
		std::atomic<bool> m_IsPaused{ false };
		std::mutex m_SimulationMutex;
		std::condition_variable m_SimulationCV;

		std::atomic<float> m_FixedTimeStep{ 1.0f / 60.0f };
		std::atomic<std::uint32_t> m_MaximumSubsteps{ 8 };
		std::atomic<float> m_ClothFrameTime{ 0.03f };
		std::atomic<std::uint32_t> m_ClothSubsteps{ 8 };
		double m_Accumulator = 0.0;
		PhysicsStepCallback m_PreSimulateCallback;
	};
}
