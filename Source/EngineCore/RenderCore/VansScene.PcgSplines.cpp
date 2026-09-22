#include "VansScene.h"
#include "Decal/VansRoadDecalGeometry.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../PhysicsCore/VansTerrainPhysicsNode.h"
#include "TerrainCore/VansTerrain.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansMesh.h"
#include "WaterCore/VansWaterSystem.h"
#include "WaterCore/VansWaterGeometryClipmap.h"
#include "../RuntimeCore/VansThreadContract.h"
#include <cstring>
#include <mutex>
#include <cmath>
#include <algorithm>

namespace VansGraphics
{
namespace
{
constexpr std::uint32_t PcgRoadTerrainReceiverGroup = 1024;

class SplineRoadMutationBarrier final : public IVansRenderThreadTransaction
{
public:
    bool Execute(VansGraphicsDevice& backend) override { return backend.WaitForIdle(); }
};

}

bool VansScene::UpdateSplineRoadRenderNodes(const Vans::VansPcgSplineFieldSnapshot& field, std::string& error)
{
    VANS_ASSERT_MAIN_THREAD();
    bool changed = field.roads.size() != m_SplineRoads.size();
    for (const auto& [id, road] : field.roads)
    {
        if (!GetMaterialAsset(road->material.ToString()))
        { error = "Road material is not loaded: " + road->material.ToString(); return false; }
        if (road->renderMode == Vans::VansPcgRoadRenderMode::ProjectedDecal)
        {
            const auto* material=static_cast<VansMaterial*>(GetMaterialAsset(road->roadDecalMaterial.ToString()));
            if (!material || material->m_MaterialType!=VAN_PBR)
            { error = "Road decal requires a loaded PBR surface material: " + road->roadDecalMaterial.ToString(); return false; }
        }
        const auto old = m_SplineRoads.find(id);
        const bool isDecal = road->renderMode == Vans::VansPcgRoadRenderMode::ProjectedDecal;
        changed |= old == m_SplineRoads.end() || old->second.fingerprint != road->fingerprint ||
            (old != m_SplineRoads.end() &&
                (old->second.node->GetNodeType() == DECAL_NODE) != isDecal);
    }
    if (!changed) return true;
    // 与普通运行时节点的结构修改共用线程事务边界，旧帧退出后才能替换其网格。
    if (IsSceneReady() && m_RenderThreadTransactionExecutor &&
        !ExecuteRenderThreadTransaction(std::make_unique<SplineRoadMutationBarrier>()))
    { error = "Road render mutation barrier failed."; return false; }
    auto* device = static_cast<VansVKDevice*>(m_GraphicsDevice);
    auto native = device->GetLogicDevice();
    std::map<std::string, std::shared_ptr<VansMesh>> prepared;
    std::map<std::string, std::unique_ptr<VansRenderNode>> added;
    const auto rollback = [&]() {
        for (const auto& [id, node] : added)
            if (node->m_TransfromIndex >= 0) m_TransformSlotAllocator.FreeSlot(node->m_TransfromIndex);
    };
    try
    {
        for (const auto& [id, road] : field.roads)
        {
            const auto old = m_SplineRoads.find(id);
            const bool isDecal = road->renderMode == Vans::VansPcgRoadRenderMode::ProjectedDecal;
            if (old != m_SplineRoads.end() && old->second.fingerprint == road->fingerprint &&
                (old->second.node->GetNodeType() == DECAL_NODE) == isDecal)
                continue;
            using Vertex = Vans::VansPcgRoadVertex;
            std::vector<VansRoadDecalVertex> decalVertices;
            std::vector<std::uint32_t> decalIndices;
            const void* vertexData=road->vertices.data();
            std::uint32_t vertexCount=static_cast<std::uint32_t>(road->vertices.size());
            std::size_t vertexStride=sizeof(Vertex);
            const std::uint32_t* indexData=road->indices.data();
            std::uint32_t indexCount=static_cast<std::uint32_t>(road->indices.size());
            std::vector<VkVertexInputAttributeDescription> attributes;
            if (isDecal)
            {
                BuildRoadDecalGeometry(*road,decalVertices,decalIndices);
                vertexData = decalVertices.data();
                vertexCount = static_cast<std::uint32_t>(decalVertices.size());
                vertexStride = sizeof(VansRoadDecalVertex);
                indexData = decalIndices.data();
                indexCount = static_cast<std::uint32_t>(decalIndices.size());
                attributes = {{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(VansRoadDecalVertex,position)},
                    {1,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(VansRoadDecalVertex,originDepth)},
                    {2,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(VansRoadDecalVertex,edge1)},
                    {3,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(VansRoadDecalVertex,edge2)},
                    {4,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(VansRoadDecalVertex,uv01)},
                    {5,0,VK_FORMAT_R32G32_SFLOAT,offsetof(VansRoadDecalVertex,uv2)}};
            }
            else
                attributes = {{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) },
                    { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) },
                    { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) },
                    { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent) }};
            auto mesh = std::make_shared<VansMesh>(true, false);
            std::vector<float> positions;
            positions.reserve(std::size_t(vertexCount) * 3);
            if (isDecal)
                for (const auto& vertex : decalVertices)
                    positions.insert(positions.end(), { vertex.position.x, vertex.position.y, vertex.position.z });
            else
                for (const auto& vertex : road->vertices)
                    positions.insert(positions.end(), { vertex.position.x, vertex.position.y, vertex.position.z });
            mesh->InitFromRawData(native, vertexData, vertexCount, static_cast<uint32_t>(vertexStride),
                indexData, indexCount,
                {{ 0, static_cast<uint32_t>(vertexStride), VK_VERTEX_INPUT_RATE_VERTEX }}, attributes, positions);
            prepared.emplace(id,std::move(mesh));
            if (old == m_SplineRoads.end() ||
                (old->second.node->GetNodeType() == DECAL_NODE) != isDecal)
            {
                std::unique_ptr<VansRenderNode> node;
                if (isDecal) node=std::make_unique<VansDecalRenderNode>(native);
                else node=std::make_unique<VansCommonRenderNode>(native,OPAQUE_NODE);
                node->SetName("PCG Road " + id);
                node->m_RayTracingEnabled = false;
                node->m_Material = static_cast<VansMaterial*>(GetMaterialAsset(
                    isDecal ? road->roadDecalMaterial.ToString() : road->material.ToString()));
                node->m_Mesh = node->m_SourceMesh = prepared.at(id).get();
                if (isDecal)
                {
                    node->m_UsesRoadDecalPass = true;
                    node->m_DecalReceiverId = PcgRoadTerrainReceiverGroup;
                    node->m_DecalMinimumNormalDot=-1.0f;
                }
                else static_cast<VansCommonRenderNode*>(node.get())->m_SupportShadow=true;
                node->SetTransformData();
                if (IsSceneReady())
                {
                    const auto slot = AllocateTransformSlot();
                    if (slot == UINT32_MAX)
                    {
                        rollback();
                        error = "Road transform capacity exhausted.";
                        return false;
                    }
                    node->m_TransfromIndex = static_cast<int>(slot);
                    added.emplace(id,std::move(node));
                    added.at(id)->CreateDescriptorSets(m_Camera,m_LightManager,m_MaterialManager);
                }
                else added.emplace(id,std::move(node));
            }
        }
    }
    catch (const std::exception& exception) { rollback(); error=exception.what(); return false; }
    for (auto it=m_SplineRoads.begin();it!=m_SplineRoads.end();)
    {
        if (field.roads.count(it->first)) { ++it; continue; }
        auto* node=it->second.node;
        ReleaseMainRenderProxyBinding(node,m_PendingRenderMutations);
        if (node->m_TransfromIndex >= 0) m_TransformSlotAllocator.FreeSlot(node->m_TransfromIndex);
        RemoveRenderNodeFromVector(node);delete node;
        it=m_SplineRoads.erase(it);
    }
    for (auto& [id, mesh] : prepared)
    {
        auto& runtime=m_SplineRoads[id];
        const bool isDecal = field.roads.at(id)->renderMode == Vans::VansPcgRoadRenderMode::ProjectedDecal;
        if (runtime.node && (runtime.node->GetNodeType() == DECAL_NODE) != isDecal)
        {
            ReleaseMainRenderProxyBinding(runtime.node,m_PendingRenderMutations);
            if (runtime.node->m_TransfromIndex >= 0)
                m_TransformSlotAllocator.FreeSlot(runtime.node->m_TransfromIndex);
            RemoveRenderNodeFromVector(runtime.node);
            delete runtime.node;
            runtime.node = nullptr;
        }
        if (!runtime.node)
        {
            runtime.node=added.at(id).release();
            RegistRenderNode(runtime.node, isDecal ? DECAL_NODE : OPAQUE_NODE);
        }
        else ReleaseMainRenderProxyBinding(runtime.node,m_PendingRenderMutations);
        runtime.node->m_Mesh=runtime.node->m_SourceMesh=mesh.get();
        runtime.node->m_Material = static_cast<VansMaterial*>(GetMaterialAsset(
            isDecal ? field.roads.at(id)->roadDecalMaterial.ToString() :
                field.roads.at(id)->material.ToString()));
        if (isDecal)
        {
            runtime.node->m_UsesRoadDecalPass = true;
            runtime.node->m_DecalReceiverId = PcgRoadTerrainReceiverGroup;
            runtime.node->m_DecalMinimumNormalDot = -1.0f;
        }
        runtime.node->MarkDescriptorSetsDirty();
        runtime.mesh = std::move(mesh);
        runtime.fingerprint = field.roads.at(id)->fingerprint;
    }
    return true;
}

std::shared_ptr<const Vans::VansTerrainAsset> VansScene::ResolveEffectiveTerrain(Vans::VansAssetGuid guid) const
{
    if (m_SplineField && m_SplineField->terrainGuid==guid) return m_SplineField->effectiveTerrain;
    return Vans::VansProjectManager::Get().GetAssetObjectRepository().ResolveLatest<Vans::VansTerrainAsset>(guid);
}

bool VansScene::PublishSplineField(std::shared_ptr<const Vans::VansPcgSplineFieldSnapshot> field, std::string& error)
{
    if (!field || !field->effectiveTerrain) {error="Incomplete spline field cannot be published.";return false;}
    if (field==m_SplineField) return true;
    if (m_WaterSystem && !VansWaterGeometryClipmap::ValidateRiverFieldBudget(*field,GetWaterConfig().m_Geometry,error)) return false;
    if (!UpdateSplineRoadRenderNodes(*field,error)) return false;
    const bool terrainChanged=!m_SplineField || m_SplineField->effectiveTerrain!=field->effectiveTerrain;
    if (terrainChanged && m_TerrainPhysicsNode)
    {
        auto& physics=VansEngine::VansPhysicsSystem::GetInstance();
        bool collisionUpdated=false;
        {
            std::lock_guard<std::mutex> guard(physics.GetSimulationMutex());
            collisionUpdated=m_TerrainPhysicsNode->UpdateSurface(field->effectiveTerrain);
        }
        // 回退道路可能等待渲染线程，不能持有物理模拟锁跨线程等待。
        if (!collisionUpdated)
        {
            std::string rollbackError;
            const Vans::VansPcgSplineFieldSnapshot empty;
            UpdateSplineRoadRenderNodes(m_SplineField?*m_SplineField:empty,rollbackError);
            error="Effective terrain collision update failed.";
            if (!rollbackError.empty()) error+=" Road rollback: "+rollbackError;
            return false;
        }
    }
    if (terrainChanged && m_TerrainRenderNode)
    {
        const auto& terrain=*field->effectiveTerrain;
        const bool entire=!m_SplineField || m_SplineField->terrainFingerprint!=field->terrainFingerprint ||
            m_SplineField->resolution!=field->resolution;
        const auto upload=[&](std::uint32_t x,std::uint32_t y,std::uint32_t width,std::uint32_t height) {
            if (!width || !height) return;
            VansRenderTerrainRegionUpload change;
            change.assetGuid=field->terrainGuid.ToString();change.x=x;change.y=y;change.width=width;change.height=height;
            change.bytes.resize(std::size_t(width)*height*sizeof(std::uint16_t));
            for (std::uint32_t row=0;row<height;++row)
                std::memcpy(change.bytes.data()+std::size_t(row)*width*2,
                    terrain.heights.data()+std::size_t(y+row)*terrain.width+x,std::size_t(width)*2);
            QueueTerrainRegionUpload(std::move(change));
        };
        if (entire) upload(0,0,terrain.width,terrain.height);
        else for (const auto key:field->changedTiles)
        {
            const std::uint32_t tx=static_cast<std::uint32_t>(key),tz=static_cast<std::uint32_t>(key>>32);
            const auto edge=[&](std::uint32_t tile,std::uint32_t size) {
                return static_cast<std::uint32_t>(std::clamp(std::ceil(double(tile)*Vans::VANS_SPLINE_TILE_SIZE/field->resolution*size-.5),0.0,double(size)));
            };
            const auto x=edge(tx,terrain.width),y=edge(tz,terrain.height);
            upload(x,y,edge(tx+1,terrain.width)-x,edge(tz+1,terrain.height)-y);
        }
    }
    m_SplineField=field;m_PendingSplineField=std::move(field);
    return true;
}
}
