#pragma once

#include "VansGameplayRuntime.h"

#include <optional>
#include <vector>

namespace Vans
{
struct VansActionHostRef
{
	VansEntityHandle owner;
	explicit operator bool() const { return owner.IsValid(); }
};

struct VansActionInstanceRef
{
	VansActionHostRef host;
	VansActionHandle action;
	explicit operator bool() const { return host && action; }
};

struct VansActionQuery
{
	VansActionHostRef host;
	VansActionId action;
	std::optional<VansActionInstanceState> state;
	std::optional<VansPredictionKey> prediction;
};

struct VansActionView
{
	VansActionHostRef host;
	VansActionInstanceSnapshot instance;
};

struct VansActivationReport
{
	bool allowed = false;
	VansActionError error = VansActionError::None;
	std::string message;
	VansActionSpecHandle spec;
};

class IVansActionSystem
{
public:
	virtual ~IVansActionSystem() = default;
	virtual VansActionSpecHandle GiveAction(
		VansActionHostRef host, const VansActionGrantDesc& grant, std::string& error) = 0;
	virtual bool RevokeAction(VansActionHostRef host, VansActionSpecHandle spec,
		VansActionRevokePolicy policy, std::string& error) = 0;
	virtual VansActionSetHandle ApplyActionSet(VansActionHostRef host,
		const VansActionSetDefinition& set, std::string& error) = 0;
	virtual VansActivationReport CanActivate(VansActionHostRef host, VansActionSpecHandle spec,
		const VansActionContext& context, bool hasAuthority, bool predicted) const = 0;
	virtual VansActivationReport CanActivate(VansActionHostRef host, VansActionId action,
		const VansActionContext& context, bool hasAuthority, bool predicted) const = 0;
	virtual VansActionResult TryActivate(VansActionHostRef host, VansActionSpecHandle spec,
		VansActionContext context, bool hasAuthority, bool predicted) = 0;
	virtual VansActionResult TryActivate(VansActionHostRef host, VansActionId action,
		VansActionContext context, bool hasAuthority, bool predicted) = 0;
	virtual bool RequestCancel(VansActionInstanceRef action,
		VansActionCancelReason reason, std::string& error) = 0;
	virtual std::size_t InterruptByQuery(const VansActionQuery& query, std::string& error) = 0;
	virtual std::vector<VansActionView> QueryActive(const VansActionQuery& query) const = 0;
	virtual std::optional<VansActionView> Inspect(VansActionInstanceRef action) const = 0;
};

class VansActionSystem final : public IVansActionSystem
{
public:
	explicit VansActionSystem(VansGameplayRuntime& runtime) : m_Runtime(runtime) {}

	VansActionSpecHandle GiveAction(VansActionHostRef host,
		const VansActionGrantDesc& grant, std::string& error) override;
	bool RevokeAction(VansActionHostRef host, VansActionSpecHandle spec,
		VansActionRevokePolicy policy, std::string& error) override;
	VansActionSetHandle ApplyActionSet(VansActionHostRef host,
		const VansActionSetDefinition& set, std::string& error) override;
	VansActionSetHandle ApplyActionSetReference(VansActionHostRef host,
		std::string_view reference, std::string& error);
	VansActivationReport CanActivate(VansActionHostRef host, VansActionSpecHandle spec,
		const VansActionContext& context, bool hasAuthority, bool predicted) const override;
	VansActivationReport CanActivate(VansActionHostRef host, VansActionId action,
		const VansActionContext& context, bool hasAuthority, bool predicted) const override;
	VansActionResult TryActivate(VansActionHostRef host, VansActionSpecHandle spec,
		VansActionContext context, bool hasAuthority, bool predicted) override;
	VansActionResult TryActivate(VansActionHostRef host, VansActionId action,
		VansActionContext context, bool hasAuthority, bool predicted) override;
	bool RequestCancel(VansActionInstanceRef action,
		VansActionCancelReason reason, std::string& error) override;
	std::size_t InterruptByQuery(const VansActionQuery& query, std::string& error) override;
	std::vector<VansActionView> QueryActive(const VansActionQuery& query) const override;
	std::optional<VansActionView> Inspect(VansActionInstanceRef action) const override;

private:
	std::shared_ptr<VansActionHost> Resolve(VansActionHostRef host) const;
	static bool Matches(const VansActionView& view, const VansActionQuery& query);

	VansGameplayRuntime& m_Runtime;
};
}
