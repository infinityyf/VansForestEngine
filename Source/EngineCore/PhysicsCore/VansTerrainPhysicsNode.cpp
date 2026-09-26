#include "VansTerrainPhysicsNode.h"
#include "VansPhysics.h"
#include "VansPhysicsNativeAccess.h"
#include "VansCollisionFilter.h"
#include "../TerrainCore/VansTerrainHeightEncoding.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

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

        if (!m_Properties.surface || !m_Properties.surface->HasPixelData())
        {
            VANS_LOG_ERROR("[TerrainPhysics] Effective terrain pixels are unavailable.");
            return false;
        }

        std::vector<PxHeightFieldSample> samples;
        PxU32 rowCount = 0;
        PxU32 columnCount = 0;
        if (!BuildHeightSamples(*m_Properties.surface, samples, rowCount, columnCount))
        {
            return false;
        }

        if (!CreateHeightFieldActor(samples, rowCount, columnCount))
        {
            Shutdown();
            return false;
        }

        if (!ApplyFilterData())
        {
            VANS_LOG_ERROR("[TerrainPhysics] Unknown collision layer '"
                << m_Properties.layerName << "'.");
            Shutdown();
            return false;
        }
        m_Enabled = true;

        return true;
    }

    void VansTerrainPhysicsNode::Shutdown()
    {
        if (m_Actor)
        {
            auto& physicsSystem = VansPhysicsSystem::GetInstance();
            PxScene* scene = VansPhysicsNativeAccess::Scene(physicsSystem);
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

    bool VansTerrainPhysicsNode::BuildHeightSamples(
        const Vans::VansTerrainAsset& surface,
        std::vector<PxHeightFieldSample>& samples,
        PxU32& rowCount,
        PxU32& columnCount)
    {
        if (!surface.HasPixelData() || surface.width < 2u || surface.height < 2u)
        {
            return false;
        }

        rowCount = surface.width;
        columnCount = surface.height;
        samples.resize(static_cast<size_t>(rowCount) * columnCount);
        for (PxU32 row = 0; row < rowCount; ++row)
        {
            for (PxU32 column = 0; column < columnCount; ++column)
            {
                const auto pixel = surface.heights[
                    static_cast<size_t>(column) * rowCount + row];
                auto& sample = samples[
                    static_cast<size_t>(row) * columnCount + column];
                sample.height = Vans::VansTerrainHeightEncoding::EncodePhysics(pixel);
                sample.materialIndex0 = 0;
                sample.materialIndex1 = 0;
                sample.clearTessFlag();
            }
        }
        return true;
    }

    PxHeightFieldGeometry VansTerrainPhysicsNode::BuildHeightFieldGeometry(
        PxHeightField* heightField,
        PxU32 rowCount,
        PxU32 columnCount,
        const Vans::VansTerrainAssetSettings& settings)
    {
        return PxHeightFieldGeometry(
            heightField,
            PxMeshGeometryFlags(),
            Vans::VansTerrainHeightEncoding::PhysicsHeightScale(settings.maxHeight),
            settings.terrainSize / static_cast<float>(rowCount - 1u),
            settings.terrainSize / static_cast<float>(columnCount - 1u));
    }

    bool VansTerrainPhysicsNode::UpdateSurface(std::shared_ptr<const Vans::VansTerrainAsset> surface)
    {
        if (!surface || !surface->HasPixelData())
        {
            return false;
        }

        auto properties = m_Properties;
        properties.surface = surface;
        if (!m_HeightField || !m_Shape || !m_Actor ||
            m_HeightField->getNbRows() != surface->width ||
            m_HeightField->getNbColumns() != surface->height ||
            !m_Properties.surface ||
            m_Properties.surface->settings.terrainSize != surface->settings.terrainSize ||
            m_Properties.surface->settings.maxHeight != surface->settings.maxHeight ||
            m_Properties.surface->settings.heightOffset != surface->settings.heightOffset)
        {
            VansTerrainPhysicsNode replacement;
            if (!replacement.Initialize(properties))
            {
                return false;
            }
            std::swap(m_Properties, replacement.m_Properties);
            std::swap(m_HeightField, replacement.m_HeightField);
            std::swap(m_Shape, replacement.m_Shape);
            std::swap(m_Actor, replacement.m_Actor);
            std::swap(m_Material, replacement.m_Material);
            std::swap(m_Enabled, replacement.m_Enabled);
            return true;
        }

        std::vector<PxHeightFieldSample> samples;
        PxU32 rows = 0;
        PxU32 columns = 0;
        if (!BuildHeightSamples(*surface, samples, rows, columns))
        {
            return false;
        }

        PxHeightFieldDesc desc;
        desc.nbRows = rows;
        desc.nbColumns = columns;
        desc.samples.data = samples.data();
        desc.samples.stride = sizeof(PxHeightFieldSample);
        auto& physicsSystem = VansPhysicsSystem::GetInstance();
        auto* scene = VansPhysicsNativeAccess::Scene(physicsSystem);
        if (!scene)
        {
            return false;
        }
        PxSceneWriteLock lock(*scene);
        if (!m_HeightField->modifySamples(0, 0, desc, true))
        {
            return false;
        }
        const PxHeightFieldGeometry geometry = BuildHeightFieldGeometry(
            m_HeightField, rows, columns, surface->settings);
        // PhysX 要求更新所有引用形状，刷新查询加速结构与边界。
        m_Shape->setGeometry(geometry);
        m_Properties = properties;
        return true;
    }

    bool VansTerrainPhysicsNode::CreateHeightFieldActor(const std::vector<PxHeightFieldSample>& samples, PxU32 rowCount, PxU32 columnCount)
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = VansPhysicsNativeAccess::Physics(physicsSystem);
        PxScene* scene = VansPhysicsNativeAccess::Scene(physicsSystem);

        if (!physics || !scene)
        {
            VANS_LOG_ERROR("[TerrainPhysics] Physics system is not initialized.");
            return false;
        }

        const Vans::VansTerrainAssetSettings& settings = m_Properties.surface->settings;
        if (settings.terrainSize <= 0.0f || settings.maxHeight <= 0.0f)
        {
            VANS_LOG_ERROR("[TerrainPhysics] Invalid terrain scale: terrainSize=" << settings.terrainSize
                           << " maxHeight=" << settings.maxHeight);
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

        m_HeightField = VansPhysicsNativeAccess::CookHeightField(physicsSystem, heightFieldDesc);
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

        const PxHeightFieldGeometry geometry = BuildHeightFieldGeometry(
            m_HeightField, rowCount, columnCount, m_Properties.surface->settings);
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

        const float halfSize = settings.terrainSize * 0.5f;
        PxTransform terrainTransform(PxVec3(-halfSize, settings.heightOffset, -halfSize), PxQuat(PxIdentity));
        m_Actor = physics->createRigidStatic(terrainTransform);
        if (!m_Actor)
        {
            VANS_LOG_ERROR("[TerrainPhysics] Failed to create terrain rigid static actor.");
            return false;
        }

        m_Actor->setName("TerrainHeightField");
        // 地形 actor 不继承 VansPhysicsNode，不参与脚本物理查询（raycast / overlapSphere）。
        // 置 nullptr 防止 ScriptBridge 将其错误地 static_cast 为 VansPhysicsNode* 后触发 UB 崩溃。
        m_Actor->userData = nullptr;
        m_Actor->attachShape(*m_Shape);
        m_Shape->release();

        {
            PxSceneWriteLock scopedWriteLock(*scene);
            scene->addActor(*m_Actor);
        }

        return true;
    }

    PxMaterial* VansTerrainPhysicsNode::CreatePhysicsMaterial()
    {
        auto& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = VansPhysicsNativeAccess::Physics(physicsSystem);
        if (!physics)
        {
            return nullptr;
        }

        return physics->createMaterial(
            m_Properties.material.staticFriction,
            m_Properties.material.dynamicFriction,
            m_Properties.material.restitution);
    }

    bool VansTerrainPhysicsNode::ApplyFilterData()
    {
        if (!m_Shape)
        {
            return false;
        }

        PxFilterData filterData;
        if (!VansCollisionFilter::Build(
            m_Properties.layerName,
            VansCollisionFilter::None,
            0u,
            filterData))
            return false;

        m_Shape->setSimulationFilterData(filterData);
        m_Shape->setQueryFilterData(filterData);
        return true;
    }
}
