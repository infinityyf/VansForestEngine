#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <PxPhysicsAPI.h>
#include <string>
#include <vector>
#include "VansPhysicsNode.h"

using namespace physx;

namespace VansEngine
{
    struct TerrainPhysicsProperties
    {
        bool enabled = false;
        std::string heightmapPath;
        float terrainSize = 1024.0f;
        float maxHeight = 500.0f;
        float heightOffset = -23.0f;
        std::string layerName = "Environment";
        bool flipX = false;
        bool flipZ = false;
        PhysicsMaterialProperties material;
    };

    class VansTerrainPhysicsNode
    {
    public:
        VansTerrainPhysicsNode();
        ~VansTerrainPhysicsNode();

        bool Initialize(const TerrainPhysicsProperties& properties);
        void Shutdown();

        bool IsEnabled() const { return m_Enabled; }
        PxRigidStatic* GetActor() const { return m_Actor; }

    private:
        bool LoadHeightSamples(std::vector<PxHeightFieldSample>& samples, PxU32& rowCount, PxU32& columnCount);
        bool CreateHeightFieldActor(const std::vector<PxHeightFieldSample>& samples, PxU32 rowCount, PxU32 columnCount);
        PxMaterial* CreatePhysicsMaterial();
        void ApplyFilterData();

    private:
        TerrainPhysicsProperties m_Properties;
        bool m_Enabled = false;
        PxRigidStatic* m_Actor = nullptr;
        PxShape* m_Shape = nullptr;
        PxHeightField* m_HeightField = nullptr;
        PxMaterial* m_Material = nullptr;
    };
}
