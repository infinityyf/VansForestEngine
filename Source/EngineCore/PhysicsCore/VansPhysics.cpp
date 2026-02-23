#include "VansPhysics.h"
#include <iostream>
#include <chrono>
#include <vehicle2/PxVehicleAPI.h>

namespace VansEngine
{
	// ============================================================================
	// VansPhysicsErrorCallback
	// ============================================================================
	void VansPhysicsErrorCallback::reportError(PxErrorCode::Enum code, const char* message, const char* file, int line)
	{
		const char* errorCode = "UNKNOWN";
		switch (code)
		{
		case PxErrorCode::eNO_ERROR: errorCode = "NO_ERROR"; break;
		case PxErrorCode::eDEBUG_INFO: errorCode = "DEBUG_INFO"; break;
		case PxErrorCode::eDEBUG_WARNING: errorCode = "DEBUG_WARNING"; break;
		case PxErrorCode::eINVALID_PARAMETER: errorCode = "INVALID_PARAMETER"; break;
		case PxErrorCode::eINVALID_OPERATION: errorCode = "INVALID_OPERATION"; break;
		case PxErrorCode::eOUT_OF_MEMORY: errorCode = "OUT_OF_MEMORY"; break;
		case PxErrorCode::eINTERNAL_ERROR: errorCode = "INTERNAL_ERROR"; break;
		case PxErrorCode::eABORT: errorCode = "ABORT"; break;
		case PxErrorCode::ePERF_WARNING: errorCode = "PERF_WARNING"; break;
		}
		
		std::cerr << "[PhysX Error] " << errorCode << ": " << message << " (File: " << file << ", Line: " << line << ")" << std::endl;
	}

	// ============================================================================
	// VansPhysicsAllocator
	// ============================================================================
	void* VansPhysicsAllocator::allocate(size_t size, const char* typeName, const char* filename, int line)
	{
		return _aligned_malloc(size, 16);
	}

	void VansPhysicsAllocator::deallocate(void* ptr)
	{
		_aligned_free(ptr);
	}

	// ============================================================================
	// VansPhysicsSystem
	// ============================================================================
	VansPhysicsSystem::VansPhysicsSystem()
	{
	}

	VansPhysicsSystem::~VansPhysicsSystem()
	{
		Shutdown();
	}

	VansPhysicsSystem& VansPhysicsSystem::GetInstance()
	{
		static VansPhysicsSystem instance;
		return instance;
	}

	bool VansPhysicsSystem::Initialize()
	{
		// Create Foundation
		m_Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_Allocator, m_ErrorCallback);
		if (!m_Foundation)
		{
			std::cerr << "[PhysX] Failed to create Foundation" << std::endl;
			return false;
		}

#ifdef _DEBUG
		// Create PVD (PhysX Visual Debugger) - Only in Debug mode
		m_Pvd = PxCreatePvd(*m_Foundation);
		if (m_Pvd)
		{
			// Create PVD transport (network connection to PVD application)
			PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
			if (transport)
			{
				// Connect to PVD with full instrumentation
				bool pvdConnected = m_Pvd->connect(*transport, PxPvdInstrumentationFlag::eALL);
				if (pvdConnected)
				{
					std::cout << "[PhysX PVD] Connected to PhysX Visual Debugger at 127.0.0.1:5425" << std::endl;
					std::cout << "[PhysX PVD] Open PhysX Visual Debugger application to see real-time physics data" << std::endl;
				}
				else
				{
					std::cout << "[PhysX PVD] Failed to connect to PhysX Visual Debugger (is PVD application running?)" << std::endl;
				}
			}
			else
			{
				std::cout << "[PhysX PVD] Failed to create PVD transport" << std::endl;
			}
		}
		else
		{
			std::cout << "[PhysX PVD] Failed to create PVD instance" << std::endl;
		}
#else
		m_Pvd = nullptr;
		std::cout << "[PhysX] PVD disabled in Release mode" << std::endl;
#endif

		// Create Physics
		m_Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_Foundation, PxTolerancesScale(), true, m_Pvd);
		if (!m_Physics)
		{
			std::cerr << "[PhysX] Failed to create Physics" << std::endl;
			return false;
		}

		// Initialize Cooking Parameters (PhysX 5 uses standalone cooking functions)
		m_CookingParams = new PxCookingParams(PxTolerancesScale());
		// Configure cooking parameters for better performance and mesh quality
		m_CookingParams->meshWeldTolerance = 0.001f;
		m_CookingParams->meshPreprocessParams = PxMeshPreprocessingFlags(PxMeshPreprocessingFlag::eWELD_VERTICES);

		// Create CPU Dispatcher
		m_Dispatcher = PxDefaultCpuDispatcherCreate(2); // 2 worker threads

		// Create Scene
		PxSceneDesc sceneDesc(m_Physics->getTolerancesScale());
		sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
		sceneDesc.cpuDispatcher = m_Dispatcher;
		sceneDesc.filterShader = PxDefaultSimulationFilterShader;
		
		m_Scene = m_Physics->createScene(sceneDesc);
		if (!m_Scene)
		{
			std::cerr << "[PhysX] Failed to create Scene" << std::endl;
			return false;
		}

		// Setup PVD client for the scene (if PVD is connected)
		if (m_Pvd && m_Pvd->isConnected())
		{
			PxPvdSceneClient* pvdClient = m_Scene->getScenePvdClient();
			if (pvdClient)
			{
				pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
				pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
				pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
				std::cout << "[PhysX PVD] Scene instrumentation enabled for debugging" << std::endl;
			}
		}

		// Create Default Material
		m_DefaultMaterial = m_Physics->createMaterial(0.5f, 0.5f, 0.6f); // static friction, dynamic friction, restitution

		std::cout << "[PhysX] Initialized successfully" << std::endl;
		return true;
	}

	void VansPhysicsSystem::Shutdown()
	{
		StopSimulation();

		if (m_DefaultMaterial) m_DefaultMaterial->release();
		if (m_Scene) m_Scene->release();
		if (m_Dispatcher) m_Dispatcher->release();
		if (m_Physics) m_Physics->release();
		if (m_Pvd) m_Pvd->release();
		if (m_Foundation) m_Foundation->release();
		if (m_CookingParams) delete m_CookingParams;

		std::cout << "[PhysX] Shutdown complete" << std::endl;
	}

	void VansPhysicsSystem::StartSimulation()
	{
		if (m_IsRunning) return;

		m_IsRunning = true;
		m_ShouldExit = false;
		m_SimulationThread = std::thread(&VansPhysicsSystem::SimulationThread, this);

		std::cout << "[PhysX] Simulation thread started" << std::endl;
	}

	void VansPhysicsSystem::StopSimulation()
	{
		if (!m_IsRunning) return;

		m_ShouldExit = true;
		m_SimulationCV.notify_all();

		if (m_SimulationThread.joinable())
		{
			m_SimulationThread.join();
		}

		m_IsRunning = false;
		std::cout << "[PhysX] Simulation thread stopped" << std::endl;
	}

	void VansPhysicsSystem::SimulationThread()
	{
		using Clock = std::chrono::high_resolution_clock;
		auto lastTime = Clock::now();

		std::cout << "[PhysX] Simulation thread running with fixed timestep: " << m_FixedTimeStep << "s" << std::endl;
		if (IsPvdConnected())
		{
			std::cout << "[PhysX PVD] Real-time physics data streaming active" << std::endl;
		}

		while (!m_ShouldExit)
		{
			auto currentTime = Clock::now();
			float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
			lastTime = currentTime;

			// Fixed timestep accumulator
			m_Accumulator += deltaTime;

			while (m_Accumulator >= m_FixedTimeStep)
			{
				{
					std::lock_guard<std::mutex> lock(m_SimulationMutex);
					
                    // Execute Pre-Simulate Callback (e.g. Vehicle Updates)
                    if (m_PreSimulateCallback)
                    {
                        m_PreSimulateCallback(m_FixedTimeStep);
                    }

					// Step the simulation
					m_Scene->simulate(m_FixedTimeStep);
					m_Scene->fetchResults(true); // Block until simulation is done
					
					// PVD data is automatically streamed when connected
					// The PVD client handles real-time data transmission
				}

				m_Accumulator -= m_FixedTimeStep;
			}

			// Sleep to avoid spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		
		std::cout << "[PhysX] Simulation thread stopped" << std::endl;
	}

	std::mutex& VansPhysicsSystem::GetSimulationMutex()
	{
		return m_SimulationMutex;
	}

	void VansPhysicsSystem::FetchResults()
	{
		// Main thread calls this to synchronize with physics thread
		std::lock_guard<std::mutex> lock(m_SimulationMutex);
		// The simulation thread already calls fetchResults internally
		// This lock ensures we don't read transforms while simulation is running
	}

	void VansPhysicsSystem::SetGravity(const PxVec3& gravity)
	{
		if (m_Scene)
		{
			std::lock_guard<std::mutex> lock(m_SimulationMutex);
			m_Scene->setGravity(gravity);
		}
	}

	PxVec3 VansPhysicsSystem::GetGravity() const
	{
		if (m_Scene)
		{
			return m_Scene->getGravity();
		}
		return PxVec3(0.0f, -9.81f, 0.0f);
	}

	// ============================================================================
	// Mesh Cooking Helper Methods (PhysX 5 API)
	// ============================================================================
	PxConvexMesh* VansPhysicsSystem::CookConvexMesh(const PxConvexMeshDesc& desc)
	{
		if (!m_Physics || !m_CookingParams)
		{
			std::cerr << "[PhysX] Physics or cooking params not initialized for cooking convex mesh" << std::endl;
			return nullptr;
		}

		// In PhysX 5, use standalone cooking functions
		PxInsertionCallback& insertionCallback = m_Physics->getPhysicsInsertionCallback();
		return PxCreateConvexMesh(*m_CookingParams, desc, insertionCallback);
	}

	PxTriangleMesh* VansPhysicsSystem::CookTriangleMesh(const PxTriangleMeshDesc& desc)
	{
		if (!m_Physics || !m_CookingParams)
		{
			std::cerr << "[PhysX] Physics or cooking params not initialized for cooking triangle mesh" << std::endl;
			return nullptr;
		}

		// In PhysX 5, use standalone cooking functions
		PxInsertionCallback& insertionCallback = m_Physics->getPhysicsInsertionCallback();
		return PxCreateTriangleMesh(*m_CookingParams, desc, insertionCallback);
	}

	PxHeightField* VansPhysicsSystem::CookHeightField(const PxHeightFieldDesc& desc)
	{
		if (!m_Physics)
		{
			std::cerr << "[PhysX] Physics not initialized for cooking height field" << std::endl;
			return nullptr;
		}

		// In PhysX 5, use standalone cooking functions
		PxInsertionCallback& insertionCallback = m_Physics->getPhysicsInsertionCallback();
		return PxCreateHeightField(desc, insertionCallback);
	}
}
