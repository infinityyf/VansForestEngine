#pragma once
#include "../../ParticleCore/VansParticleAsset.h"
#include <memory>
#include <vector>

namespace VansGraphics
{
class VansTexture;
struct VansParticleTextureBindings
{
    VansTexture* color = nullptr;
    VansTexture* positiveAxes = nullptr;
    VansTexture* negativeAxes = nullptr;
};
// 主线程完成资产依赖解析；RT 只消费这份只读绑定，不访问源文件或组件。
struct VansParticleRenderAsset
{
    std::shared_ptr<const VansParticleAsset> definition;
    std::vector<VansParticleTextureBindings> textures;
};
}
