#pragma once

#include "../GameplayActionExecution/VansActionExecution.h"
#include "../GameplayAttributes/VansGameplayAttributes.h"
#include "../GameplayTargeting/VansGameplayTargeting.h"

#include <string>

namespace Vans
{
// 领域服务只能经此边界访问 Host 状态和 World 资源所有权；具体 Runtime
// 继续作为组合根与 Scheduler owner，不向 Adapter 暴露具体 Host。
class IVansGameplayServiceRuntime
{
public:
	virtual ~IVansGameplayServiceRuntime() = default;

	virtual bool HasHost(VansEntityHandle owner) const = 0;
	virtual bool IsActionActive(VansEntityHandle owner, VansActionHandle action) const = 0;
	virtual bool HasTag(VansEntityHandle owner, VansGameplayTagId tag) const = 0;
	virtual bool EnqueueActionEvent(
		VansEntityHandle owner,
		VansActionHandle action,
		VansActionEvent event,
		std::string& error) = 0;
	virtual bool PublishGameplayEvent(VansEntityHandle owner, VansActionEvent event) = 0;
	virtual bool ReadAttribute(
		VansEntityHandle owner,
		VansAttributeId attribute,
		double& value) const = 0;
	virtual bool ApplyBaseAttribute(
		VansEntityHandle owner,
		VansAttributeId attribute,
		VansAttributeBaseOperation operation,
		double magnitude,
		double& value) = 0;
	virtual VansActionResult ActivateAction(
		VansEntityHandle owner,
		VansActionId action,
		VansActionContext context,
		VansTargetData targetData) = 0;
	virtual bool ForgetCompletedWorldResource(
		VansActionServiceId service,
		VansGenerationHandle resource) = 0;
};
}
