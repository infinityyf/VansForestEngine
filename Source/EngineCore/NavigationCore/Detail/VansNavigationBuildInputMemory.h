#pragma once
#include "../VansNavigationSourceSnapshot.h"
#include <set>

namespace Vans
{
// 构建与运行期共用容量计量；相同冻结对象及高度采样只计一次。
class VansNavigationBuildInputMemory final
{
public:
    void Add(const VansNavigationProfileBuildInput& input)
    {
        if(!m_Inputs.insert(&input).second) return;
        bytes+=sizeof(input)+64+input.profileId.capacity()+1+
            input.contributions.capacity()*sizeof(VansNavigationContribution)+
            input.footprints.capacity()*sizeof(VansNavigationBuildFootprint)+
            input.modifiers.capacity()*sizeof(VansNavigationModifierRecord)+
            input.heightfields.capacity()*sizeof(VansNavigationHeightfieldInput)+input.carveObstacles.capacity()*sizeof(VansNavigationObstacleGeometry);
        bytes+=input.links.capacity()*sizeof(VansNavigationLinkGeometry);
        for(const auto& link:input.links) bytes+=link.id.capacity()+1;
        for(const auto& shape:input.carveObstacles) bytes+=shape.id.capacity()+1;
        if(input.geometry && m_Geometry.insert(input.geometry.get()).second)
            bytes+=sizeof(VansNavigationGeometry)+64+input.geometry->vertices.capacity()*sizeof(float)+
                input.geometry->indices.capacity()*sizeof(int)+input.geometry->areas.capacity();
        for(const auto& item:input.contributions) bytes+=item.id.capacity()+1;
        for(const auto& item:input.footprints) bytes+=item.id.capacity()+1;
        for(const auto& item:input.modifiers)
        {
            bytes+=item.id.capacity()+1+item.modifier.mapId.capacity()+1+item.modifier.area.capacity()+1+
                item.modifier.profiles.capacity()*sizeof(std::string)+
                item.modifier.convexVerticesMeters.capacity()*sizeof(glm::vec2);
            for(const auto& profile:item.modifier.profiles) bytes+=profile.capacity()+1;
        }
        for(const auto& field:input.heightfields)
        {
            if(field.data && m_Grids.insert(field.data.get()).second)
                bytes+=sizeof(VansHeightfieldData)+64+field.data->samples.capacity()*sizeof(std::int16_t);
            bytes+=field.sourceId.capacity()+1+field.surfaces.capacity()*sizeof(VansNavigationSurfaceRecord);
            for(const auto& record:field.surfaces)
            {
                const auto& surface=record.surface;
                bytes+=record.id.capacity()+1+record.ownerId.capacity()+1+surface.mapId.capacity()+1+
                    surface.navigationMeshGuid.capacity()+1+surface.profiles.capacity()*sizeof(std::string)+
                    surface.collisionLayers.capacity()*sizeof(std::string)+surface.sources.capacity()*sizeof(VansNavigationSourceReference);
                for(const auto& id:surface.profiles) bytes+=id.capacity()+1;
                for(const auto& id:surface.collisionLayers) bytes+=id.capacity()+1;
                for(const auto& ref:surface.sources) bytes+=ref.entityGuid.capacity()+1+ref.componentGuid.capacity()+1;
            }
        }
    }
    std::uint64_t bytes=0;
private:
    std::set<const VansNavigationProfileBuildInput*> m_Inputs;
    std::set<const VansNavigationGeometry*> m_Geometry;
    std::set<const VansHeightfieldData*> m_Grids;
};
}
