#include "VansTerrainPhysicsNode.h"
#include "VansCollisionLayerManager.h"
#include "../Util/VansLog.h"

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

        if (!m_Properties.surface || !m_Properties.surface->HasPixelData())
        {
            VANS_LOG_ERROR("[TerrainPhysics] Effective terrain pixels are unavailable.");
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
        const auto& surface=m_Properties.surface;
        if (!surface || !surface->HasPixelData()) return false;
        rowCount=surface->width;columnCount=surface->height;
        samples.resize(static_cast<size_t>(rowCount)*columnCount);
        // 保留已有高度场网格布局和量化约定，数据改为共享的内存有效地形。
        for (PxU32 row=0;row<rowCount;++row) for (PxU32 column=0;column<columnCount;++column)
        {
            const auto pixel=surface->heights[static_cast<size_t>(column)*rowCount+row];
            auto& sample=samples[static_cast<size_t>(row)*columnCount+column];
            sample.height=static_cast<PxI16>(std::lround(float(pixel)*32767.0f/65535.0f));
            sample.materialIndex0=0;sample.materialIndex1=0;sample.clearTessFlag();
        }
        return true;
    }

    bool VansTerrainPhysicsNode::UpdateSurface(std::shared_ptr<const Vans::VansTerrainAsset> surface)
    {
        if (!surface || !surface->HasPixelData()) return false;
        auto properties=m_Properties;
        properties.surface=surface;
        properties.terrainSize=surface->settings.terrainSize;
        properties.maxHeight=surface->settings.maxHeight;
        properties.heightOffset=surface->settings.heightOffset;
        if (!m_HeightField || !m_Shape || !m_Actor ||
            m_HeightField->getNbRows()!=surface->width || m_HeightField->getNbColumns()!=surface->height ||
            m_Properties.terrainSize!=properties.terrainSize || m_Properties.maxHeight!=properties.maxHeight ||
            m_Properties.heightOffset!=properties.heightOffset)
        {
            VansTerrainPhysicsNode replacement;
            if (!replacement.Initialize(properties)) return false;
            std::swap(m_Properties,replacement.m_Properties);
            std::swap(m_HeightField,replacement.m_HeightField);
            std::swap(m_Shape,replacement.m_Shape);
            std::swap(m_Actor,replacement.m_Actor);
            std::swap(m_Material,replacement.m_Material);
            std::swap(m_Enabled,replacement.m_Enabled);
            return true;
        }
        const auto old=m_Properties.surface;
        m_Properties.surface=surface;
        std::vector<PxHeightFieldSample> samples;PxU32 rows=0,columns=0;
        if (!LoadHeightSamples(samples,rows,columns)) {m_Properties.surface=old;return false;}
        PxHeightFieldDesc desc;desc.nbRows=rows;desc.nbColumns=columns;
        desc.samples.data=samples.data();desc.samples.stride=sizeof(PxHeightFieldSample);
        auto* scene=VansPhysicsSystem::GetInstance().GetScene();
        if (!scene) {m_Properties.surface=old;return false;}
        PxSceneWriteLock lock(*scene);
        if (!m_HeightField->modifySamples(0,0,desc,true)) {m_Properties.surface=old;return false;}
        const PxHeightFieldGeometry geometry(m_HeightField,PxMeshGeometryFlags(),
            properties.maxHeight/32767.0f,properties.terrainSize/float(rows-1),properties.terrainSize/float(columns-1));
        // PhysX 要求更新所有引用形状，刷新查询加速结构与边界。
        m_Shape->setGeometry(geometry);
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
        // 地形 actor 不继承 VansPhysicsNode，不参与脚本物理查询（raycast / overlapSphere）。
        // 置 nullptr 防止 ScriptBridge 将其错误地 static_cast 为 VansPhysicsNode* 后触发 UB 崩溃。
        m_Actor->userData = nullptr;
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
