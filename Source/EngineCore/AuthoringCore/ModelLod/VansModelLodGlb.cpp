#include "VansModelLodBuildData.h"
#include <nlohmann/json.hpp>
#include <limits>
namespace Vans::ModelLodBuild
{
std::string EncodeGlb(const std::vector<Vertex>& vertices,const std::vector<uint32_t>& indices)
{
    using Json=nlohmann::json;
    // 不输出跨格式约定不同的切线符号；由运行时统一导入流程根据最终 UV 重新计算。
    const auto& encoded=vertices;
    glm::vec3 lo(std::numeric_limits<float>::max()),hi(-std::numeric_limits<float>::max());
    // glTF 的 UV 已采用引擎需要的方向；Assimp 的 glTF 导入与 FlipUVs 会互相抵消。
    for(auto& v:encoded){lo=glm::min(lo,v.position);hi=glm::max(hi,v.position);}
    std::string bin(reinterpret_cast<const char*>(encoded.data()),encoded.size()*sizeof(Vertex));
    const auto indexOffset=bin.size();bin.append(reinterpret_cast<const char*>(indices.data()),indices.size()*4);
    Json root={{"asset",{{"version","2.0"},{"generator","Forest Editor Model LOD"}}},{"scene",0},
        {"scenes",Json::array({{{"nodes",{0}}}})},{"nodes",Json::array({{{"name","ModelLOD"},{"mesh",0}}})},
        {"meshes",Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",0},{"NORMAL",1},{"TEXCOORD_0",2}}},{"indices",3}}})}}})},
        {"buffers",Json::array({{{"byteLength",bin.size()}}})},
        {"bufferViews",Json::array({{{"buffer",0},{"byteLength",indexOffset},{"byteStride",sizeof(Vertex)},{"target",34962}},
            {{"buffer",0},{"byteOffset",indexOffset},{"byteLength",indices.size()*4},{"target",34963}}})}};
    Json access=Json::array();
    for(auto item:std::vector<std::pair<size_t,std::string>>{{offsetof(Vertex,position),"VEC3"},{offsetof(Vertex,normal),"VEC3"},{offsetof(Vertex,uv),"VEC2"}})
        access.push_back({{"bufferView",0},{"byteOffset",item.first},{"componentType",5126},{"count",vertices.size()},{"type",item.second}});
    access[0]["min"]={lo.x,lo.y,lo.z};access[0]["max"]={hi.x,hi.y,hi.z};
    access.push_back({{"bufferView",1},{"componentType",5125},{"count",indices.size()},{"type","SCALAR"}});root["accessors"]=access;
    auto json=root.dump();while(json.size()%4)json+=' ';while(bin.size()%4)bin+='\0';
    std::string out;const auto word=[&](uint32_t n){out.append(reinterpret_cast<const char*>(&n),4);};
    word(0x46546c67);word(2);word(uint32_t(12+8+json.size()+8+bin.size()));word(uint32_t(json.size()));word(0x4e4f534a);out+=json;
    word(uint32_t(bin.size()));word(0x004e4942);out+=bin;return out;
}
}
