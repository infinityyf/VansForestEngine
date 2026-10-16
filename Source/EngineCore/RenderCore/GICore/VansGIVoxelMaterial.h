#pragma once
#include <glm/glm.hpp>
#include <map>

namespace VansGraphics
{
    class VansPBRMaterial;
    class VansTexture;
    // 光照使用的反照率/金属度/粗糙度随帧发布；不存储 radiance 或 irradiance。
    // 材质指针只作身份键，RT 不通过它读取 Main 参数。
    struct VansGIVoxelMaterial
    {
        glm::vec3 albedo{1};
        float metallic=0,roughness=1;
        bool alphaTest=false;
        float alphaCutoff=.5f;
        VansTexture* baseColorTexture=nullptr;
        float leafAlphaCutoff=.5f;
        VansGIVoxelMaterial ForSourcePart(bool leaf) const
        {
            auto result=*this;
            if(leaf){result.alphaTest=true;result.alphaCutoff=leafAlphaCutoff;}
            return result;
        }
        bool CoverageEquals(const VansGIVoxelMaterial& other) const
        {return alphaTest==other.alphaTest && (!alphaTest || alphaCutoff==other.alphaCutoff) && baseColorTexture==other.baseColorTexture;}
        bool operator==(const VansGIVoxelMaterial& other) const
        {return albedo==other.albedo && metallic==other.metallic && roughness==other.roughness &&
            leafAlphaCutoff==other.leafAlphaCutoff && CoverageEquals(other);}
        glm::vec4 Resolve(glm::vec3 baseColor) const
        {return glm::vec4(baseColor*albedo*(1-metallic),roughness);}
    };
    using VansGIVoxelMaterialInputs=std::map<const VansPBRMaterial*,VansGIVoxelMaterial>;
}
