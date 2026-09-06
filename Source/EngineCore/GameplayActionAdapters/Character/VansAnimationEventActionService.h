#pragma once

#include "../../GameplayActionCore/VansActionServices.h"
#include "../../RuntimeCore/VansGenerationPool.h"
#include "../../SceneRuntime/VansRuntimeHandle.h"
#include <memory>
#include <unordered_set>

namespace Vans
{
class VansRuntimeWorld;
class VansGameplayRuntime;

const VansActionServiceCapability& VansAnimationEventActionCapability();

// Clip 只描述语义事件。此适配器把最终动画输出中的事件路由到精确的 Action 实例。
class VansAnimationEventActionService final : public IVansActionService
{
public:
    VansAnimationEventActionService(VansRuntimeWorld& world, VansGameplayRuntime& gameplay);
    const VansActionServiceCapability& Capability() const override;
    VansActionCommandResult Execute(const VansActionCommand& command) override;
    bool Release(VansGenerationHandle resource, std::string& error) override;
    // 每个游戏动画帧只调用一次，必须在最终姿态和挂点世界变换结算之后。
    void PublishEvaluatedEvents();

private:
    struct Subscription
    {
        VansEntityHandle owner;
        VansActionHandle action;
        VansComponentHandle animation;
        std::string clip;
        std::string event;
        float minimumWeight = 0.5f;
        bool oncePerAction = false;
        bool forwardOnly = true;
        std::unordered_set<std::string> delivered;
    };
    VansRuntimeWorld& m_World;
    VansGameplayRuntime& m_Gameplay;
    VansGenerationPool<Subscription> m_Subscriptions;
};
}
