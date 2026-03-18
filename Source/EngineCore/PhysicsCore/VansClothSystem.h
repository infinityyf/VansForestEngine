#pragma once

// ─── NvCloth core (CPU factory, no CUDA/DX) ────────────────────────────────
#include <NvCloth/Factory.h>
#include <NvCloth/Solver.h>
#include <NvCloth/Callbacks.h>

namespace VansEngine
{
    // =========================================================================
    // VansClothSystem
    // Singleton that owns the NvCloth Factory + Solver.
    // Lifecycle:
    //   VansClothSystem::GetInstance().Initialize()  — call after VansPhysicsSystem::Initialize()
    //   VansClothSystem::GetInstance().SimulateStep(dt) — call each frame before reading particles
    //   VansClothSystem::GetInstance().Shutdown()    — call before VansPhysicsSystem::Shutdown()
    // =========================================================================
    class VansClothSystem
    {
    public:
        static VansClothSystem& GetInstance();

        // Call after VansPhysicsSystem::Initialize().
        // Sets up NvCloth logging, creates CPU factory and solver.
        bool Initialize();

        // Destroy solver and factory. Safe to call multiple times.
        void Shutdown();

        // Step the cloth solver for this frame.
        // Call AFTER SyncPinnedParticlesToRenderNode on all cloth nodes and
        // BEFORE WriteSimResultsToStagingBuffer.
        void SimulateStep(float dt);

        nv::cloth::Factory* GetFactory() const { return m_Factory; }
        nv::cloth::Solver*  GetSolver()  const { return m_Solver;  }

        bool IsInitialized() const { return m_Factory != nullptr; }

    private:
        VansClothSystem()  = default;
        ~VansClothSystem() = default;
        VansClothSystem(const VansClothSystem&)            = delete;
        VansClothSystem& operator=(const VansClothSystem&) = delete;

        nv::cloth::Factory* m_Factory = nullptr;
        nv::cloth::Solver*  m_Solver  = nullptr;
    };
}
