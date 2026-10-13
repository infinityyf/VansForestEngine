#include "VansPcgCollisionPlan.h"
#include "VansPcgRuntimePolicy.h"
#include <algorithm>
#include <map>
#include <set>
#include <limits>
#include <new>

namespace Vans
{
bool BuildPcgCollisionPlan(const VansPcgUpdatePlan& update,VansPcgCollisionPlan& output,std::string& error)
try
{
    VansPcgCollisionPlan candidate;
    std::map<std::string,std::uint64_t> excluded;
    std::set<std::string> identities;
    const auto field=[](const std::string& value){return std::to_string(value.size())+":"+value+"/";};
    for(const auto& layer:update.updates)
    {
    if(layer.coverage) { error="PCG collision snapshot requires a complete committed plan"; return false; }
    for(const auto& [key,batch]:layer.batches)
    {
        if(!batch || !batch->plant || !(batch->key==key) || key.region.empty() || key.layer.empty() ||
            key.region.size()+key.layer.size()+key.variant.size()>448)
        { error="PCG collision plan contains an invalid committed batch"; return false; }
        const auto found=std::find_if(batch->plant->variants.begin(),batch->plant->variants.end(),
            [&](const auto& variant){return variant.id==key.variant;});
        if(found==batch->plant->variants.end())
        { error="PCG collision batch has no matching Plant variant"; return false; }
        const auto scope="pcg/"+field(key.region)+field(key.layer);
        if(!found->collision)
        {
            if(batch->points.empty()) continue;
            auto& count=excluded[scope+field(key.variant)];
            if(batch->points.size()>(std::numeric_limits<std::uint64_t>::max)()-count)
            { error="PCG excluded instance count overflows"; return false; }
            count+=batch->points.size(); continue;
        }
        if(!found->collision->mesh.IsValid() || found->collision->layer.empty() || found->collision->layer.size()>128)
        { error="Committed PCG collision requires an explicit CPU Model and layer"; return false; }
        if(batch->points.size()>MaximumPcgCollisionInstances-candidate.instances.size())
        { error="Committed PCG collision instance capacity exceeded"; return false; }
        for(const auto& point:batch->points)
        {
            VansPcgCollisionInstance instance;
            const auto identity=scope+VansPcgPointGenerator::PointIdText(point.id);
            instance.ownerGuid=VansAssetGuid::FromStableName("PcgInstance",identity).ToString();
            if(!identities.insert(instance.ownerGuid).second)
            { error="Committed PCG collision has duplicate stable instance identities"; return false; }
            instance.componentGuid=VansAssetGuid::FromStableName("PcgCollision",instance.ownerGuid).ToString();
            instance.point=point; instance.prototype=*found->collision;
            candidate.instances.push_back(std::move(instance));
        }
    }
    }
    std::sort(candidate.instances.begin(),candidate.instances.end(),[](const auto& a,const auto& b){return a.ownerGuid<b.ownerGuid;});
    for(auto& [id,count]:excluded) candidate.noCollisionPrototype.push_back({std::move(id),count});
    output=std::move(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="PCG collision plan allocation failed"; return false; }
}
