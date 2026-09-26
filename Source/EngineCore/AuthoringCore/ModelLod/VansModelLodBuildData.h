#pragma once
#include "../../AssetCore/VansModelLod.h"
#include <glm/glm.hpp>
#include <vector>
namespace Vans::ModelLodBuild
{
struct Vertex { glm::vec3 position, normal; glm::vec2 uv; };
struct Part { std::vector<Vertex> vertices; std::vector<uint32_t> indices; };
std::string EncodeGlb(const std::vector<Vertex>& vertices,const std::vector<uint32_t>& indices);
}
