#include "VansTerrainPhysicsNode.h"
#include "VansCollisionLayerManager.h"
#include "../Util/VansLog.h"

#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>

namespace VansEngine
{
    VansTerrainPhysicsNode::VansTerrainPhysicsNode()
    {
    }

    VansTerrainPhysicsNode::~VansTerrainPhysicsNode()
    {
        Shutdown();
    }

    bool VansTerrainPhysicsNode::Initialize(const TerrainPhysicsProperties& properties)
    {
        Shutdown();
        m_Properties = properties;

        if (!m_Properties.enabled)
        {
            return false;
        }

        if (m_Properties.heightmapPath.empty())
        {
            VANS_LOG_ERROR("[TerrainPhysics] heightmapPath is empty, terrain collision skipped.");
            return false;
        }

        std::vector<PxHeightFieldSample> samples;
        PxU32 rowCount = 0;
        PxU32 columnCount = 0;
        if (!LoadHeightSamples(samples, rowCount, columnCount))
        {
            return false;
        }

        if (!CreateHeightFieldActor(samples, rowCount, columnCount))
        {
            Shutdown();
            return false;
        }

        ApplyFilterData();
        m_Enabled = true;

        VANS_LOG("[TerrainPhysics] Heightfield collision created. rows=" << rowCount
                 << " columns=" << columnCount
                 << " terrainSize=" << m_Properties.terrainSize
                 << " maxHeight=" << m_Properties.maxHeight
                 << " heightOffset=" << m_Properties.heightOffset);
        return true;
    }

    void VansTerrainPhysicsNode::Shutdown()
    {
        if (m_Actor)
        {
            PxScene* scene = VansPhysicsSystem::GetInstance().GetScene();
            if (scene)
            {
                PxSceneWriteLock scopedWriteLock(*scene);
                scene->removeActor(*m_Actor);
            }

            m_Actor->release();
            m_Actor = nullptr;
        }
        else if (m_Shape)
        {
            m_Shape->release();
        }

        if (m_HeightField)
        {
            m_HeightField->release();
            m_HeightField = nullptr;
        }

        if (m_Material)
        {
            m_Material->release();
            m_Material = nullptr;
        }

        m_Shape = nullptr;
        m_Enabled = false;
    }

    bool VansTerrainPhysicsNode::LoadHeightSamples(std::vector<PxHeightFieldSample>& samples, PxU32& rowCount, PxU32& columnCount)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_us* pixels = stbi_load_16(m_Properties.heightmapPath.c_str(), &width, &height, &channels, 1);
        if (!pixels)
        {
            VANS_LOG_ERROR("[TerrainPhysics] Failed to load heightmap: " << m_Properties.heightmapPath);
            return false;
        }

        if (width < 2 || height < 2)
        {
            VANS_LOG_ERROR("[TerrainPhysics] Invalid heightmap size: " << width << "x" << height);
            stbi_image_free(pixels);
            return false;
        }

        rowCount = static_cast<PxU32>(width);
        columnCount = static_cast<PxU32>(height);
        samples.resize(static_cast<size_t>(rowCount) * static_cast<size_t>(columnCount));

        for (int row = 0; row < width; ++row)
        {
            const int sourceX = m_Properties.flipX ? (width - 1 - row) : row;
            for (int column = 0; column < height; ++column)
            {
                const int sourceY = m_Properties.flipZ ? (height - 1 - column) : column;
                const uint16_t pixel = pixels[sourceY * width + sourceX];
                const float normalizedHeight = static_cast<float>(pixel) / 65535.0f;
                const int sampleHeight = static_cast<int>(std::lround(normalizedHeight * 32767.0f));

                PxHeightFieldSample& sample = samples[static_cast<size_t>(row) * columnCount + column];
                sample.height = static_cast<PxI16>(std::clamp(sampleHeight, 0, 32767));
                sample.materialIndex0 = 0;
                sample.materialIndex1 = 0;
                sample.clearTessFlag();
            }
        }

        stbi_image_free(pixels);

        VANS_LOG("[TerrainPhysics] Heightmap loaded: " << m_Properties.heightmapPath
                 << " size=" << width << "x" << height
                 << " channels=" << channels
                 << " flipX=" << m_Properties.flipX
                 << " flipZ=" << m_Properties.flipZ);
        return true;
    }

    bool VansTerrainPhysicsNode::CreateHeightFieldActor(const std::vector<PxHeightFieldSample>& samples, PxU32 rowCount, PxU32 columnCount)
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = physicsSystem.GetPhysics();
        PxScene* scene = physicsSystem.GetScene();

        if (!physics || !scene)
        {
            VANS_LOG_ERROR("[TerrainPhysics] Physics system is not initialized.");
            return false;
        }

        if (m_Properties.terrainSize <= 0.0f || m_Properties.maxHeight <= 0.0f)
        {
            VANS_LOG_ERROR("[TerrainPhysics] Invalid terrain scale: terrainSize=" << m_Properties.terrainSize
                           << " maxHeight=" << m_Properties.maxHeight);
            return false;
        }

        PxHeightFieldDesc heightFieldDesc;
        heightFieldDesc.nbRows = rowCount;
        heightFieldDesc.nbColumns = columnCount;
        heightFieldDesc.samples.data = samples.data();
        heightFieldDesc.samples.stride = sizeof(PxHeightFieldSample);

        if (!heightFieldDesc.isValid())
        {
            VANS_LOG_ERROR("[TerrainPhysics] Invalid PxHeightFieldDesc.");
            return false;
        }

        m_HeightField = physicsSystem.CookHeightField(heightFieldDesc);
        if (!m_HeightField)
        {
            VANS_LOG_ERROR("[TerrainPhysics] Failed to cook PxHeightField.");
            return false;
        }

        m_Material = CreatePhysicsMaterial();
        if (!m_Material)
        {
            VANS_LOG_ERROR("[TerrainPhysics] Failed to create physics material.");
            return false;
        }

        const float heightScale = m_Properties.maxHeight / 32767.0f;
        const float rowScale = m_Properties.terrainSize / static_cast<float>(rowCount - 1);
        const float columnScale = m_Properties.terrainSize / static_cast<float>(columnCount - 1);

        PxHeightFieldGeometry geometry(m_HeightField, PxMeshGeometryFlags(), heightScale, rowScale, columnScale);
        if (!geometry.isValid())
        {
            VANS_LOG_ERROR("[TerrainPhysics] Invalid PxHeightFieldGeometry.");
            return false;
        }

        m_Shape = physics->createShape(geometry, *m_Material);
        if (!m_Shape)
        {
            VANS_LOG_ERROR("[TerrainPhysics] Failed to create heightfield shape.");
            return false;
        }

        m_Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
        m_Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);

        const float halfSize = m_Properties.terrainSize * 0.5f;
        PxTransform terrainTransform(PxVec3(-halfSize, m_Properties.heightOffset, -halfSize), PxQuat(PxIdentity));
        m_Actor = physics->createRigidStatic(terrainTransform);
        if (!m_Actor)
        {
            VANS_LOG_ERROR("[TerrainPhysics] Failed to create terrain rigid static actor.");
            return false;
        }

        m_Actor->setName("TerrainHeightField");
        m_Actor->userData = this;
        m_Actor->attachShape(*m_Shape);
        m_Shape->release();

        {
            PxSceneWriteLock scopedWriteLock(*scene);
            scene->addActor(*m_Actor);
        }

        VANS_LOG("[TerrainPhysics] Geometry scale: heightScale=" << heightScale
                 << " rowScale=" << rowScale
                 << " columnScale=" << columnScale);
        return true;
    }

    PxMaterial* VansTerrainPhysicsNode::CreatePhysicsMaterial()
    {
        PxPhysics* physics = VansPhysicsSystem::GetInstance().GetPhysics();
        if (!physics)
        {
            return nullptr;
        }

        return physics->createMaterial(
            m_Properties.material.staticFriction,
            m_Properties.material.dynamicFriction,
            m_Properties.material.restitution);
    }

    void VansTerrainPhysicsNode::ApplyFilterData()
    {
        if (!m_Shape)
        {
            return;
        }

        auto& layerMgr = VansCollisionLayerManager::Get();
        int layerIdx = layerMgr.GetLayerIndex(m_Properties.layerName);
        if (layerIdx == 0 && m_Properties.layerName != layerMgr.GetLayerName(0))
        {
            VANS_LOG_WARN("[TerrainPhysics] Layer '" << m_Properties.layerName
                          << "' not found, terrain collision falls back to '"
                          << layerMgr.GetLayerName(0) << "'.");
        }

        PxFilterData filterData;
        filterData.word0 = static_cast<PxU32>(layerIdx);
        filterData.word1 = layerMgr.GetCollisionMask(layerIdx);
        filterData.word2 = 0u;
        filterData.word3 = 0u;

        m_Shape->setSimulationFilterData(filterData);
        m_Shape->setQueryFilterData(filterData);

        VANS_LOG("[TerrainPhysics] ApplyFilterData: layer='" << m_Properties.layerName
                 << "' layerIdx=" << layerIdx
                 << " mask=0x" << std::hex << filterData.word1 << std::dec);
    }
}
