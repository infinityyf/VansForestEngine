// Include NvCloth AFTER PhysX headers so the foundation types (PxVec3, etc.)
// are resolved from External/PhysX/include which comes first in the include path.
// NvCloth's PxShared headers serve as fallback only.
#include "VansPhysics.h"            // brings in PxPhysicsAPI + our allocator/error types
#include "VansClothSystem.h"
#include "../Util/VansLog.h"

// ── NvCloth callbacks ── //
// We provide lightweight wrappers that forward to the same error/alloc behaviour
// as the rest of the physics system, without duplicating the PhysX Foundation objects.

namespace
{
    // Allocator wraps _aligned_malloc / _aligned_free (16-byte aligned, required by NvCloth).
    class ClothAllocator : public physx::PxAllocatorCallback
    {
    public:
        void* allocate(size_t size, const char*, const char*, int) override
        {
            return _aligned_malloc(size, 16);
        }
        void deallocate(void* ptr) override
        {
            _aligned_free(ptr);
        }
    };

    // Error callback forwards NvCloth messages to the engine log.
    class ClothErrorCallback : public physx::PxErrorCallback
    {
    public:
        void reportError(physx::PxErrorCode::Enum code, const char* message,
                         const char* file, int line) override
        {
            if (code == physx::PxErrorCode::eNO_ERROR ||
                code == physx::PxErrorCode::eDEBUG_INFO)
            {
                VANS_LOG("[NvCloth] " << message);
            }
            else if (code == physx::PxErrorCode::eDEBUG_WARNING ||
                     code == physx::PxErrorCode::ePERF_WARNING)
            {
                VANS_LOG_WARN("[NvCloth] " << message << " (" << file << ":" << line << ")");
            }
            else
            {
                VANS_LOG_ERROR("[NvCloth] " << message << " (" << file << ":" << line << ")");
            }
        }
    };

    static ClothAllocator    s_ClothAllocator;
    static ClothErrorCallback s_ClothErrorCallback;
}

namespace VansEngine
{
    VansClothSystem& VansClothSystem::GetInstance()
    {
        static VansClothSystem instance;
        return instance;
    }

    bool VansClothSystem::Initialize()
    {
        if (m_Factory)
        {
            VANS_LOG_WARN("[NvCloth] Already initialized, skipping.");
            return true;
        }

        // Register NvCloth's internal memory/error/assert callbacks.
        // Passing nullptr for assertHandler and profilerCallback is safe in release builds.
        nv::cloth::InitializeNvCloth(
            &s_ClothAllocator,
            &s_ClothErrorCallback,
            nullptr,   // assert handler — not needed for release/debug non-assert builds
            nullptr    // profiler callback — optional performance tracking
        );

        // Create CPU software cloth factory (no CUDA / DX11 dependency).
        m_Factory = NvClothCreateFactoryCPU();
        if (!m_Factory)
        {
            VANS_LOG_ERROR("[NvCloth] NvClothCreateFactoryCPU() failed.");
            return false;
        }

        // Create a single solver that manages all registered cloth instances.
        m_Solver = m_Factory->createSolver();
        if (!m_Solver)
        {
            VANS_LOG_ERROR("[NvCloth] createSolver() failed.");
            NvClothDestroyFactory(m_Factory);
            m_Factory = nullptr;
            return false;
        }

        VANS_LOG("[NvCloth] Initialized (CPU solver).");
        return true;
    }

    void VansClothSystem::Shutdown()
    {
        if (m_Solver)
        {
            delete m_Solver;
            m_Solver = nullptr;
        }
        if (m_Factory)
        {
            NvClothDestroyFactory(m_Factory);
            m_Factory = nullptr;
        }
        VANS_LOG("[NvCloth] Shutdown complete.");
    }

    void VansClothSystem::SimulateStep(float dt)
    {
        if (!m_Solver || m_Solver->getNumCloths() == 0)
            return;

        // beginSimulation returns false when there is nothing to simulate.
        if (m_Solver->beginSimulation(dt))
        {
            // simulateChunk(0) executes the single available chunk on the calling thread.
            // For a multi-threaded setup, call getSimulationChunkCount() and dispatch chunks
            // across worker threads, then call endSimulation() on any thread.
            m_Solver->simulateChunk(0);
            m_Solver->endSimulation();
        }
    }
}
