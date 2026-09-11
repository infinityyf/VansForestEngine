#pragma once
#include "../../GameplayActionCore/VansActionServices.h"
#include "../../GameplayTargeting/VansSurfaceImpact.h"
#include <functional>

namespace Vans
{
struct VansDecalSceneBackend
{
    std::function<bool(const std::string&, const VansSurfaceImpact&, std::string&)> spawnImpact;
};
const VansActionServiceCapability& VansDecalActionCapability();
// 命令不向 Action ledger 交付资源；弹坑寿命由场景池管理。
class VansDecalActionService final : public IVansActionService
{
public:
    explicit VansDecalActionService(VansDecalSceneBackend backend) : m_Backend(std::move(backend)) {}
    const VansActionServiceCapability& Capability() const override { return VansDecalActionCapability(); }
    VansActionCommandResult Execute(const VansActionCommand& command) override;
    bool Release(VansGenerationHandle, std::string& error) override
    { error = "Impact decals have scene-owned lifetime"; return false; }
private:
    VansDecalSceneBackend m_Backend;
};
}
