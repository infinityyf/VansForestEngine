#include "VansPhysics.h"
#include "VansPhysicsNativeAccess.h"
#include "VansPhysicsEventCallback.h"
#include "VansClothSystem.h"
#include "VansRagdollTypes.h"
#include "../Util/VansLog.h"
#include "../Util/VansProfiler.h"
#include "../RuntimeCore/VansThreadContract.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <chrono>
#include <vehicle2/PxVehicleAPI.h>

namespace VansEngine
{
	using namespace physx;

	class VansPhysicsErrorCallback final : public PxErrorCallback
	{
	public:
		void reportError(
			PxErrorCode::Enum code,
			const char* message,
			const char* file,
			int line) override;
	};

	class VansPhysicsAllocator final : public PxAllocatorCallback
	{
	public:
		void* allocate(
			size_t size,
			const char* typeName,
			const char* filename,
			int line) override;
		void deallocate(void* ptr) override;
	};

	struct VansPhysicsSystem::NativeState
	{
		PxFoundation* foundation = nullptr;
		PxPhysics* physics = nullptr;
		PxDefaultCpuDispatcher* dispatcher = nullptr;
		PxScene* scene = nullptr;
		PxMaterial* defaultMaterial = nullptr;
		PxControllerManager* controllerManager = nullptr;
		PxCookingParams* cookingParams = nullptr;
		PxPvd* pvd = nullptr;
		PxPvdTransport* pvdTransport = nullptr;
		VansPhysicsEventCallback* eventCallback = nullptr;
		VansPhysicsErrorCallback errorCallback;
		VansPhysicsAllocator allocator;
	};

	// ============================================================================
	// VansCollisionFilterShader replaces PxDefaultSimulationFilterShader.
	// ============================================================================
	PxFilterFlags VansCollisionFilterShader(
		PxFilterObjectAttributes attributes0, PxFilterData filterData0,
		PxFilterObjectAttributes attributes1, PxFilterData filterData1,
		PxPairFlags& pairFlags, const void* constantBlock, PxU32 constantBlockSize)
	{
		// Handle the PhysX built-in trigger flag first.
		if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
		{
			pairFlags = PxPairFlag::eTRIGGER_DEFAULT
			          | PxPairFlag::eNOTIFY_TOUCH_FOUND
			          | PxPairFlag::eNOTIFY_TOUCH_LOST;
			return PxFilterFlag::eDEFAULT;
		}

		// Check the layer collision matrix.
		uint32_t layerA = filterData0.word0;
		uint32_t layerB = filterData1.word0;
		uint32_t maskA  = filterData0.word1;
		uint32_t maskB  = filterData1.word1;

		// 开启自身碰撞的布娃娃不排除任何身体对，包括相邻关节连接的刚体。
		if (RagdollPairSuppressed(filterData0, filterData1))
		{
			return PxFilterFlag::eSUPPRESS;
		}

		if (!((maskA & (1u << layerB)) && (maskB & (1u << layerA))))
		{
			return PxFilterFlag::eSUPPRESS;
		}

		// Check the custom trigger flag stored in word2 bit 0.
		bool isTriggerA = (filterData0.word2 & 0x1) != 0;
		bool isTriggerB = (filterData1.word2 & 0x1) != 0;

		if (isTriggerA || isTriggerB)
		{
			pairFlags = PxPairFlag::eTRIGGER_DEFAULT
			          | PxPairFlag::eNOTIFY_TOUCH_FOUND
			          | PxPairFlag::eNOTIFY_TOUCH_LOST;
			return PxFilterFlag::eDEFAULT;
		}

		// Normal collision pair.
		pairFlags = PxPairFlag::eCONTACT_DEFAULT
		          | PxPairFlag::eNOTIFY_TOUCH_FOUND
		          | PxPairFlag::eNOTIFY_TOUCH_LOST
		          | PxPairFlag::eNOTIFY_CONTACT_POINTS;

		return PxFilterFlag::eDEFAULT;
	}

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
		
		VANS_LOG_ERROR("[PhysX Error] " << errorCode << ": " << message << " (File: " << file << ", Line: " << line << ")");
	}

	// ============================================================================
	// VansPhysicsAllocator
	// ============================================================================
	void* VansPhysicsAllocator::allocate(size_t size, const char* typeName, const char* filename, int line)
	{
		// Some PhysX paths request a zero-byte temporary buffer. Allocate at least
		// one aligned block because PhysX treats a null allocator result as eABORT.
		size_t allocSize = (std::max)(size, static_cast<size_t>(16));
		return _aligned_malloc(allocSize, 16);
	}

	void VansPhysicsAllocator::deallocate(void* ptr)
	{
		_aligned_free(ptr);
	}

	// ============================================================================
	// VansPhysicsSystem
	// ============================================================================
	VansPhysicsSystem::VansPhysicsSystem()
		: m_Native(std::make_unique<NativeState>())
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
		VansClothSystem& clothSystem = VansClothSystem::GetInstance();
		if (m_Native->foundation && m_Native->physics && m_Native->dispatcher && m_Native->scene &&
			m_Native->defaultMaterial && m_Native->controllerManager && clothSystem.IsInitialized())
		{
			return true;
		}

		const bool hasPartialState =
			(m_Native->controllerManager != nullptr) ||
			(m_Native->defaultMaterial != nullptr) ||
			(m_Native->scene != nullptr) ||
			(m_Native->dispatcher != nullptr) ||
			(m_Native->physics != nullptr) ||
			(m_Native->pvd != nullptr) ||
			(m_Native->pvdTransport != nullptr) ||
			(m_Native->foundation != nullptr) ||
			(m_Native->cookingParams != nullptr) ||
			(m_Native->eventCallback != nullptr) ||
			clothSystem.IsInitialized();
		if (hasPartialState)
			Shutdown();

		const auto failInitialization = [this]()
		{
			Shutdown();
			return false;
		};

		// Create Foundation
		m_Native->foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_Native->allocator, m_Native->errorCallback);
		if (!m_Native->foundation)
		{
			VANS_LOG_ERROR("[PhysX] Failed to create Foundation");
			return failInitialization();
		}

		// Create PVD (PhysX Visual Debugger)
		m_Native->pvd = PxCreatePvd(*m_Native->foundation);
		if (m_Native->pvd)
		{
			// Create PVD transport (network connection to PVD application)
			m_Native->pvdTransport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
			if (m_Native->pvdTransport)
			{
				// Connect to PVD with full instrumentation
				bool pvdConnected = m_Native->pvd->connect(*m_Native->pvdTransport, PxPvdInstrumentationFlag::eALL);
				if (pvdConnected)
				{
					VANS_LOG("[PhysX PVD] Connected to PhysX Visual Debugger at 127.0.0.1:5425");
					VANS_LOG("[PhysX PVD] Open PhysX Visual Debugger application to see real-time physics data");
				}
				else
				{
					VANS_LOG_WARN("[PhysX PVD] Failed to connect to PhysX Visual Debugger (is PVD application running?)");
				}
			}
			else
			{
				VANS_LOG_ERROR("[PhysX PVD] Failed to create PVD transport");
			}
		}
		else
		{
				VANS_LOG_ERROR("[PhysX PVD] Failed to create PVD instance");
		}

		// Create Physics
		m_Native->physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_Native->foundation, PxTolerancesScale(), true, m_Native->pvd);
		if (!m_Native->physics)
		{
			VANS_LOG_ERROR("[PhysX] Failed to create Physics");
			return failInitialization();
		}

		// Initialize Cooking Parameters (PhysX 5 uses standalone cooking functions)
		m_Native->cookingParams = new PxCookingParams(PxTolerancesScale());
		// Configure cooking parameters for better performance and mesh quality
		m_Native->cookingParams->meshWeldTolerance = 0.001f;
		m_Native->cookingParams->meshPreprocessParams = PxMeshPreprocessingFlags(PxMeshPreprocessingFlag::eWELD_VERTICES);

		// Create CPU Dispatcher
		m_Native->dispatcher = PxDefaultCpuDispatcherCreate(2); // 2 worker threads
		if (!m_Native->dispatcher)
		{
			VANS_LOG_ERROR("[PhysX] Failed to create CPU dispatcher");
			return failInitialization();
		}

		// Create Scene
		PxSceneDesc sceneDesc(m_Native->physics->getTolerancesScale());
		sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
		sceneDesc.cpuDispatcher = m_Native->dispatcher;
		sceneDesc.filterShader = VansCollisionFilterShader;
		sceneDesc.kineKineFilteringMode = PxPairFilteringMode::eKEEP;
		sceneDesc.staticKineFilteringMode = PxPairFilteringMode::eKEEP;

		VANS_LOG("[PhysX] Scene pair filtering enabled: kineKine=eKEEP, staticKine=eKEEP");

		// Register collision and trigger event callbacks.
		m_Native->eventCallback = new VansPhysicsEventCallback();
		sceneDesc.simulationEventCallback = m_Native->eventCallback;
		
		m_Native->scene = m_Native->physics->createScene(sceneDesc);
		if (!m_Native->scene)
		{
			VANS_LOG_ERROR("[PhysX] Failed to create Scene");
			return failInitialization();
		}

		// Setup PVD client for the scene (if PVD is connected)
		if (m_Native->pvd && m_Native->pvd->isConnected())
		{
			PxPvdSceneClient* pvdClient = m_Native->scene->getScenePvdClient();
			if (pvdClient)
			{
				pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
				pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
				pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
				VANS_LOG("[PhysX PVD] Scene instrumentation enabled for debugging");
			}
		}

		// Create Default Material
		m_Native->defaultMaterial = m_Native->physics->createMaterial(0.5f, 0.5f, 0.6f); // static friction, dynamic friction, restitution
		if (!m_Native->defaultMaterial)
		{
			VANS_LOG_ERROR("[PhysX] Failed to create default material");
			return failInitialization();
		}

		// Initialize NvCloth CPU simulation
		if (!clothSystem.Initialize())
		{
			VANS_LOG_ERROR("[PhysX] Failed to initialize NvCloth");
			return failInitialization();
		}

		// Create the single CCT manager owned by this PxScene.
		m_Native->controllerManager = PxCreateControllerManager(*m_Native->scene);
		if (!m_Native->controllerManager)
		{
			VANS_LOG_ERROR("[VansPhysics] PxCreateControllerManager failed");
			return failInitialization();
		}

		VANS_LOG("[PhysX] Initialized successfully");
		return true;
	}

	void VansPhysicsSystem::Shutdown()
	{
		StopSimulation();

		VansClothSystem& clothSystem = VansClothSystem::GetInstance();
		const bool hadResources =
			(m_Native->controllerManager != nullptr) ||
			(m_Native->defaultMaterial != nullptr) ||
			(m_Native->scene != nullptr) ||
			(m_Native->dispatcher != nullptr) ||
			(m_Native->physics != nullptr) ||
			(m_Native->pvd != nullptr) ||
			(m_Native->pvdTransport != nullptr) ||
			(m_Native->foundation != nullptr) ||
			(m_Native->cookingParams != nullptr) ||
			(m_Native->eventCallback != nullptr) ||
			clothSystem.IsInitialized();

		// The singleton destructor can call Shutdown() after explicit engine shutdown.
		// Other singleton dependencies may already be destroyed on that second call.
		if (!hadResources)
			return;

		clothSystem.Shutdown();

		// Release the controller manager before the PxScene.
		if (m_Native->controllerManager)
		{
			m_Native->controllerManager->release();
			m_Native->controllerManager = nullptr;
		}

		if (m_Native->defaultMaterial) { m_Native->defaultMaterial->release(); m_Native->defaultMaterial = nullptr; }
		if (m_Native->scene) { m_Native->scene->release(); m_Native->scene = nullptr; }
		if (m_Native->dispatcher) { m_Native->dispatcher->release(); m_Native->dispatcher = nullptr; }
		if (m_Native->eventCallback) { delete m_Native->eventCallback; m_Native->eventCallback = nullptr; }
		if (m_Native->cookingParams) { delete m_Native->cookingParams; m_Native->cookingParams = nullptr; }
		if (m_Native->physics) { m_Native->physics->release(); m_Native->physics = nullptr; }
		if (m_Native->pvd)
		{
			if (m_Native->pvd->isConnected())
				m_Native->pvd->disconnect();
			m_Native->pvd->release();
			m_Native->pvd = nullptr;
		}
		if (m_Native->pvdTransport) { m_Native->pvdTransport->release(); m_Native->pvdTransport = nullptr; }
		if (m_Native->foundation) { m_Native->foundation->release(); m_Native->foundation = nullptr; }
		m_PreSimulateCallback = nullptr;
		const VansPhysicsTiming defaultTiming;
		m_FixedTimeStep.store(defaultTiming.fixedTimeStep);
		m_MaximumSubsteps.store(defaultTiming.maximumSubsteps);
		m_ClothFrameTime.store(defaultTiming.clothFrameTime);
		m_ClothSubsteps.store(defaultTiming.clothSubsteps);
		m_Accumulator = 0.0;
		m_ShouldExit = false;
		m_IsPaused = false;

		VANS_LOG("[PhysX] Shutdown complete");
	}

	void VansPhysicsSystem::StartSimulation()
	{
		VANS_ASSERT_MAIN_THREAD();

		if (m_IsRunning) return;

		m_IsRunning = true;
		m_ShouldExit = false;
		m_SimulationThread = std::thread(&VansPhysicsSystem::SimulationThread, this);

		VANS_LOG("[PhysX] Simulation thread started");
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
		m_IsPaused  = false;
		VANS_LOG("[PhysX] Simulation thread stopped");
	}

	void VansPhysicsSystem::PauseSimulation()
	{
		m_IsPaused = true;
		VANS_LOG("[PhysX] Simulation paused");
	}

	void VansPhysicsSystem::ResumeSimulation()
	{
		m_IsPaused = false;
		VANS_LOG("[PhysX] Simulation resumed");
	}

	bool VansPhysicsSystem::SetTiming(const VansPhysicsTiming& timing)
	{
		VANS_ASSERT_MAIN_THREAD();
		if (!timing.IsValid())
		{
			VANS_LOG_WARN("[PhysX] Ignore invalid timing: fixedTimeStep=" << timing.fixedTimeStep
				<< " maximumSubsteps=" << timing.maximumSubsteps
				<< " clothFrameTime=" << timing.clothFrameTime
				<< " clothSubsteps=" << timing.clothSubsteps);
			return false;
		}

		m_MaximumSubsteps.store(timing.maximumSubsteps);
		m_FixedTimeStep.store(timing.fixedTimeStep);
		m_ClothSubsteps.store(timing.clothSubsteps);
		m_ClothFrameTime.store(timing.clothFrameTime);
		VANS_LOG("[PhysX] Timing set: fixedTimeStep=" << timing.fixedTimeStep
			<< "s maximumSubsteps=" << timing.maximumSubsteps
			<< " clothFrameTime=" << timing.clothFrameTime
			<< "s clothSubsteps=" << timing.clothSubsteps);
		return true;
	}

	VansPhysicsTiming VansPhysicsSystem::GetTiming() const
	{
		VansPhysicsTiming timing;
		timing.fixedTimeStep = m_FixedTimeStep.load();
		timing.maximumSubsteps = m_MaximumSubsteps.load();
		timing.clothFrameTime = m_ClothFrameTime.load();
		timing.clothSubsteps = m_ClothSubsteps.load();
		return timing;
	}

	void VansPhysicsSystem::SetPreSimulateCallback(PhysicsStepCallback callback)
	{
		VANS_ASSERT_MAIN_THREAD();
		std::lock_guard<std::mutex> lock(m_SimulationMutex);
		m_PreSimulateCallback = std::move(callback);
	}

	void VansPhysicsSystem::SimulationThread()
	{
		VANS_INIT_PHYSICS_THREAD();
		VANS_ASSERT_PHYSICS_THREAD();
		VANS_PROFILE_THREAD("Physics Thread");

		using Clock = std::chrono::steady_clock;
		auto lastTime = Clock::now();

		const VansPhysicsTiming initialTiming = GetTiming();
		VANS_LOG("[PhysX] Simulation thread running with fixedTimeStep="
			<< initialTiming.fixedTimeStep << "s maximumSubsteps=" << initialTiming.maximumSubsteps);
		if (IsPvdConnected())
		{
			VANS_LOG("[PhysX PVD] Real-time physics data streaming active");
		}

		while (!m_ShouldExit)
		{
			VANS_PROFILE_SCOPE("PhysicsThread::Loop", Vans::ProfileCategory::Physics);

			// Do not advance simulation while the physics system is paused.
			if (m_IsPaused.load())
			{
				VANS_PROFILE_SCOPE("PhysicsThread::SleepPaused", Vans::ProfileCategory::Wait);
				lastTime = Clock::now(); // Prevent a large accumulated step after resume.
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				continue;
			}

			auto currentTime = Clock::now();
			const VansPhysicsTiming timing = GetTiming();
			const double fixedTimeStep = static_cast<double>(timing.fixedTimeStep);
			const double maximumAccumulatedTime =
				fixedTimeStep * static_cast<double>(timing.maximumSubsteps);
			double deltaTime = std::chrono::duration<double>(currentTime - lastTime).count();
			lastTime = currentTime;

			// 丢弃异常长停顿的超额时间，避免补步风暴长期占用 SimulationMutex。
			deltaTime = (std::clamp)(deltaTime, 0.0, maximumAccumulatedTime);
			m_Accumulator = (std::min)(m_Accumulator + deltaTime, maximumAccumulatedTime);

			std::uint32_t substepCount = 0;
			while (m_Accumulator >= fixedTimeStep && substepCount < timing.maximumSubsteps)
			{
				VANS_PROFILE_SCOPE("PhysicsThread::FixedStep", Vans::ProfileCategory::Physics);
				{
					std::lock_guard<std::mutex> lock(m_SimulationMutex);
					
                    // Execute Pre-Simulate Callback (e.g. Vehicle Updates)
                    if (m_PreSimulateCallback)
                    {
						VANS_PROFILE_SCOPE("Physics::PreSimulateCallback", Vans::ProfileCategory::Physics);
						m_PreSimulateCallback(timing.fixedTimeStep);
                    }

					// Step the simulation
					{
						VANS_PROFILE_SCOPE("PhysX::simulate", Vans::ProfileCategory::Physics);
						m_Native->scene->simulate(timing.fixedTimeStep);
					}
					{
						VANS_PROFILE_SCOPE("PhysX::fetchResults", Vans::ProfileCategory::Physics);
						m_Native->scene->fetchResults(true); // Block until simulation is done
					}
					
					// PVD data is automatically streamed when connected
					// The PVD client handles real-time data transmission
				}

				m_Accumulator -= fixedTimeStep;
				++substepCount;
			}

			// Sleep to avoid spinning
			{
				VANS_PROFILE_SCOPE("PhysicsThread::IdleSleep", Vans::ProfileCategory::Wait);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
		
		VANS_ASSERT_PHYSICS_THREAD();
		VANS_CLEAR_THREAD_ROLE();
		VANS_LOG("[PhysX] Simulation thread stopped");
	}

	std::mutex& VansPhysicsSystem::GetSimulationMutex()
	{
		return m_SimulationMutex;
	}

	bool VansPhysicsSystem::IsPvdConnected() const
	{
		return m_Native->pvd && m_Native->pvd->isConnected();
	}

	void VansPhysicsSystem::SetGravity(const glm::vec3& gravity)
	{
		if (m_Native->scene)
		{
			std::lock_guard<std::mutex> lock(m_SimulationMutex);
			m_Native->scene->setGravity(PxVec3(gravity.x, gravity.y, gravity.z));
		}
	}

	glm::vec3 VansPhysicsSystem::GetGravity() const
	{
		if (m_Native->scene)
		{
			const PxVec3 gravity = m_Native->scene->getGravity();
			return glm::vec3(gravity.x, gravity.y, gravity.z);
		}
		return glm::vec3(0.0f, -9.81f, 0.0f);
	}

	PxScene* VansPhysicsNativeAccess::Scene(VansPhysicsSystem& system)
	{
		return system.m_Native->scene;
	}

	const PxScene* VansPhysicsNativeAccess::Scene(const VansPhysicsSystem& system)
	{
		return system.m_Native->scene;
	}

	PxPhysics* VansPhysicsNativeAccess::Physics(VansPhysicsSystem& system)
	{
		return system.m_Native->physics;
	}

	PxControllerManager* VansPhysicsNativeAccess::ControllerManager(VansPhysicsSystem& system)
	{
		return system.m_Native->controllerManager;
	}

	PxMaterial* VansPhysicsNativeAccess::DefaultMaterial(VansPhysicsSystem& system)
	{
		return system.m_Native->defaultMaterial;
	}

	const PxCookingParams* VansPhysicsNativeAccess::CookingParams(
		const VansPhysicsSystem& system)
	{
		return system.m_Native->cookingParams;
	}

	PxConvexMesh* VansPhysicsNativeAccess::CookConvexMesh(
		VansPhysicsSystem& system,
		const PxConvexMeshDesc& desc)
	{
		if (!system.m_Native->physics || !system.m_Native->cookingParams)
		{
			VANS_LOG_ERROR("[PhysX] Physics or cooking params not initialized for cooking convex mesh");
			return nullptr;
		}

		PxInsertionCallback& insertionCallback =
			system.m_Native->physics->getPhysicsInsertionCallback();
		return PxCreateConvexMesh(
			*system.m_Native->cookingParams,
			desc,
			insertionCallback);
	}

	PxTriangleMesh* VansPhysicsNativeAccess::CookTriangleMesh(
		VansPhysicsSystem& system,
		const PxTriangleMeshDesc& desc)
	{
		if (!system.m_Native->physics || !system.m_Native->cookingParams)
		{
			VANS_LOG_ERROR("[PhysX] Physics or cooking params not initialized for cooking triangle mesh");
			return nullptr;
		}

		PxInsertionCallback& insertionCallback =
			system.m_Native->physics->getPhysicsInsertionCallback();
		return PxCreateTriangleMesh(
			*system.m_Native->cookingParams,
			desc,
			insertionCallback);
	}

	PxHeightField* VansPhysicsNativeAccess::CookHeightField(
		VansPhysicsSystem& system,
		const PxHeightFieldDesc& desc)
	{
		if (!system.m_Native->physics)
		{
			VANS_LOG_ERROR("[PhysX] Physics not initialized for cooking height field");
			return nullptr;
		}

		PxInsertionCallback& insertionCallback =
			system.m_Native->physics->getPhysicsInsertionCallback();
		return PxCreateHeightField(desc, insertionCallback);
	}
}
