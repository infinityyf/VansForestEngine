#pragma once
#include <PxPhysicsAPI.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>

using namespace physx;

namespace VansEngine
{
	// PhysX Error Callback
	class VansPhysicsErrorCallback : public PxErrorCallback
	{
	public:
		virtual void reportError(PxErrorCode::Enum code, const char* message, const char* file, int line) override;
	};

	// PhysX Allocator
	class VansPhysicsAllocator : public PxAllocatorCallback
	{
	public:
		virtual void* allocate(size_t size, const char* typeName, const char* filename, int line) override;
		virtual void deallocate(void* ptr) override;
	};

	// Main Physics System
	class VansPhysicsSystem
	{
	public:
		static VansPhysicsSystem& GetInstance();
		
		// Lifecycle
		bool Initialize();
		void Shutdown();
		
		// Simulation Control
		void StartSimulation();
		void StopSimulation();
		bool IsSimulationRunning() const { return m_IsRunning; }
		void SetFixedTimeStep(float deltaTime) { m_FixedTimeStep = deltaTime; }
		float GetFixedTimeStep() const { return m_FixedTimeStep; }
		
		// Scene Access
		PxScene* GetScene() { return m_Scene; }
		PxPhysics* GetPhysics() { return m_Physics; }
		const PxCookingParams* GetCookingParams() { return m_CookingParams; }
		
		// Gravity
		void SetGravity(const PxVec3& gravity);
		PxVec3 GetGravity() const;
		
		// Synchronization
		// Returns the simulation mutex. Lock this to pause/synchronize with the simulation thread.
		std::mutex& GetSimulationMutex();
        
        // Callback to run additional physics logic (like Vehicle updates) before the scene simulation
        // This is called inside the physics thread lock
        using PhysicsStepCallback = std::function<void(float dt)>;
        void SetPreSimulateCallback(PhysicsStepCallback callback) { m_PreSimulateCallback = callback; }

		// Call this from main thread to synchronize with physics thread (wait for current step)
		void FetchResults();
		
		// PVD Debugging Support
		bool IsPvdConnected() const { return m_Pvd && m_Pvd->isConnected(); }
		PxPvd* GetPvd() { return m_Pvd; }
		
		// Mesh Cooking Helper Methods (PhysX 5 API)
		PxConvexMesh* CookConvexMesh(const PxConvexMeshDesc& desc);
		PxTriangleMesh* CookTriangleMesh(const PxTriangleMeshDesc& desc);
		PxHeightField* CookHeightField(const PxHeightFieldDesc& desc);
		
	private:
		VansPhysicsSystem();
		~VansPhysicsSystem();
		VansPhysicsSystem(const VansPhysicsSystem&) = delete;
		VansPhysicsSystem& operator=(const VansPhysicsSystem&) = delete;
		
		// Simulation Thread
		void SimulationThread();
		
		// PhysX Core Objects
		PxFoundation* m_Foundation = nullptr;
		PxPhysics* m_Physics = nullptr;
		PxDefaultCpuDispatcher* m_Dispatcher = nullptr;
		PxScene* m_Scene = nullptr;
		PxMaterial* m_DefaultMaterial = nullptr;
		PxCookingParams* m_CookingParams = nullptr;
		PxPvd* m_Pvd = nullptr; // PhysX Visual Debugger
		
		// Callbacks
		VansPhysicsErrorCallback m_ErrorCallback;
		VansPhysicsAllocator m_Allocator;
		
		// Threading
		std::thread m_SimulationThread;
		std::atomic<bool> m_IsRunning{ false };
		std::atomic<bool> m_ShouldExit{ false };
		std::mutex m_SimulationMutex;
		std::condition_variable m_SimulationCV;
		
		// Timing
		float m_FixedTimeStep = 1.0f / 60.0f; // 60 FPS default
		double m_Accumulator = 0.0;
        
        PhysicsStepCallback m_PreSimulateCallback;
	};
}