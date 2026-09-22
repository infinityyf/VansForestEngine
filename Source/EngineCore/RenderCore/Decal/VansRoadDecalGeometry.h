#pragma once
#include "../../PcgCore/VansPcgSplineField.h"
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
// 每个原始道路三角形向世界 -Y 挤出。一个棱柱的全部顶点携带相同
// 投影坐标基，fragment 使用接收地表位置求 UV，绝不插值代理表面的 UV。
struct VansRoadDecalVertex
{
    glm::vec3 position{};
    glm::vec4 originDepth{};
    glm::vec3 edge1{};
    glm::vec3 edge2{};
    glm::vec4 uv01{};
    glm::vec2 uv2{};
};

inline void BuildRoadDecalGeometry(const Vans::VansPcgRoadMesh& road,
    std::vector<VansRoadDecalVertex>& vertices, std::vector<std::uint32_t>& indices)
{
    vertices.clear();indices.clear();
    vertices.reserve(road.indices.size()*2);
    indices.reserve(road.indices.size()*8);
    const float depth=std::max(road.projectedDepth,.05f);
    for (std::size_t i=0;i+2<road.indices.size();i+=3)
    {
        const auto& a=road.vertices.at(road.indices[i]);
        const auto& b=road.vertices.at(road.indices[i+1]);
        const auto& c=road.vertices.at(road.indices[i+2]);
        const glm::vec3 e1=b.position-a.position,e2=c.position-a.position;
        const float area=e1.x*e2.z-e1.z*e2.x;
        if (std::abs(area)<1e-8f) continue;
        const auto base=static_cast<std::uint32_t>(vertices.size());
        for (float offset:{0.f,-depth})
            for (const auto* source:{&a,&b,&c})
                vertices.push_back({source->position+glm::vec3(0,offset,0),
                    glm::vec4(a.position,depth),e1,e2,glm::vec4(a.uv,b.uv),c.uv});
        const auto add=[&](std::uint32_t x,std::uint32_t y,std::uint32_t z) {
            // 保持所有棱柱向外绕序，包括反转点序/局部折返后的道路。
            if (area>0) std::swap(y,z);
            indices.insert(indices.end(),{base+x,base+y,base+z});
        };
        add(0,1,2);add(3,5,4);
        for (std::uint32_t j=0;j<3;++j)
        {
            const auto k=(j+1)%3;
            add(j,j+3,k+3);add(j,k+3,k);
        }
    }
}
}
