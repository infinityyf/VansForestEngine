#pragma once
#include "../Util/VansWorldTextureWindow.h"
#include <string>

namespace Vans
{
// 纯值输入，不持有 GPU、项目 Pass 或游戏业务。纹理 key 来自通用运行时资源目录。
// Albedo: 线性 RGB 预乘 A 覆盖；Normal: 编码的世界 XYZ 法线 RGB 预乘 A。
// Height: dot(texel, heightWeights)，结果单位为米；零表示无位移。
struct VansTerrainInteraction
{
    bool enabled = false;
    VansWorldTextureWindow window;
    std::string albedo, normal, height;
    glm::vec4 heightWeights{1.0f, 0.0f, 0.0f, 0.0f};
    float albedoStrength = 1.0f;
    float normalStrength = 1.0f;
    float maxDisplacementMeters = 0.15f;
    float edgeFadeMeters = 0.25f;
};
bool ValidateTerrainInteraction(const VansTerrainInteraction& input, std::string& error);
}
