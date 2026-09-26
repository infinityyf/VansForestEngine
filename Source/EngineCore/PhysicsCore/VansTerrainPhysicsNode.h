#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <PxPhysicsAPI.h>
#include <string>
#include <vector>
#include "VansPhysicsNode.h"
#include "../TerrainCore/VansTerrainAsset.h"
#include <memory>

namespace VansEngine
{
    using namespace physx;

    struct TerrainPhysicsProperties
    {
        bool enabled = false;
        std::shared_ptr<const Vans::VansTerrainAsset> surface;
        std::string layerName;
        PhysicsMaterialProperties material;
    };

    class VansTerrainPhysicsNode
    {
    public:
        VansTerrainPhysicsNode();
        ~VansTerrainPhysicsNode();

        bool Initialize(const TerrainPhysicsProperties& properties);
        bool UpdateSurface(std::shared_ptr<const Vans::VansTerrainAsset> surface);
        void Shutdown();

        bool IsEnabled() const { return m_Enabled; }
        PxRigidStatic* GetActor() const { return m_Actor; }

    private:
        static bool BuildHeightSamples(
            const Vans::VansTerrainAsset& surface,
            std::vector<PxHeightFieldSample>& samples,
            PxU32& rowCount,
            PxU32& columnCount);
        static PxHeightFieldGeometry BuildHeightFieldGeometry(
            PxHeightField* heightField,
            PxU32 rowCount,
            PxU32 columnCount,
            const Vans::VansTerrainAssetSettings& settings);
        bool CreateHeightFieldActor(const std::vector<PxHeightFieldSample>& samples, PxU32 rowCount, PxU32 columnCount);
        PxMaterial* CreatePhysicsMaterial();
        bool ApplyFilterData();

    private:
        TerrainPhysicsProperties m_Properties;
        bool m_Enabled = false;
        PxRigidStatic* m_Actor = nullptr;
        PxShape* m_Shape = nullptr;
        PxHeightField* m_HeightField = nullptr;
        PxMaterial* m_Material = nullptr;
    };
}
