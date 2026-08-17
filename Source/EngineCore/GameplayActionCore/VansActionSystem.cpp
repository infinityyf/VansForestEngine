#include "VansActionSystem.h"

#include <algorithm>

namespace Vans
{
std::shared_ptr<VansActionHost> VansActionSystem::Resolve(VansActionHostRef host) const
{
	return host ? m_Runtime.FindHost(host.owner) : nullptr;
}

VansActionSpecHandle VansActionSystem::GiveAction(
	VansActionHostRef host,
	const VansActionGrantDesc& grant,
	std::string& error)
{
	auto resolved = Resolve(host);
	if (!resolved) { error = "Action Host was not found"; return {}; }
	return resolved->Grant(grant, error);
}

bool VansActionSystem::RevokeAction(
	VansActionHostRef host,
	VansActionSpecHandle spec,
	VansActionRevokePolicy policy,
	std::string& error)
{
	auto resolved = Resolve(host);
	if (!resolved) { error = "Action Host was not found"; return false; }
	return resolved->Revoke(spec, policy, error);
}

VansActionSetHandle VansActionSystem::ApplyActionSet(
	VansActionHostRef host,
	const VansActionSetDefinition& set,
	std::string& error)
{
	auto resolved = Resolve(host);
	if (!resolved) { error = "Action Host was not found"; return {}; }
	return resolved->ApplyActionSet(set, error);
}

VansActionSetHandle VansActionSystem::ApplyActionSetReference(
	VansActionHostRef host,
	std::string_view reference,
	std::string& error)
{
	const VansActionSetDefinition* set = m_Runtime.Assets().ResolveActionSet(reference);
	if (!set) { error = "Action Set reference could not be resolved"; return {}; }
	return ApplyActionSet(host, *set, error);
}

namespace
{
VansActivationReport Report(VansActionSpecHandle spec, const VansActionResult& result)
{
	return { static_cast<bool>(result), result.error, result.message, spec };
}
}

VansActivationReport VansActionSystem::CanActivate(
	VansActionHostRef host,
	VansActionSpecHandle spec,
	const VansActionContext& context,
	bool hasAuthority,
	bool predicted) const
{
	auto resolved = Resolve(host);
	if (!resolved) return { false, VansActionError::InvalidHandle, "Action Host was not found", spec };
	return Report(spec, resolved->CanActivate(spec, context, hasAuthority, predicted));
}

VansActivationReport VansActionSystem::CanActivate(
	VansActionHostRef host,
	VansActionId action,
	const VansActionContext& context,
	bool hasAuthority,
	bool predicted) const
{
	auto resolved = Resolve(host);
	if (!resolved) return { false, VansActionError::InvalidHandle, "Action Host was not found", {} };
	const auto grants = resolved->GrantedActions();
	const auto spec = std::find_if(grants.begin(), grants.end(),
		[action](const auto& value) { return value.action == action; });
	const VansActionSpecHandle handle = spec == grants.end()
		? VansActionSpecHandle{} : spec->handle;
	return Report(handle, resolved->CanActivateAction(action, context, hasAuthority, predicted));
}

VansActionResult VansActionSystem::TryActivate(
	VansActionHostRef host,
	VansActionSpecHandle spec,
	VansActionContext context,
	bool hasAuthority,
	bool predicted)
{
	auto resolved = Resolve(host);
	if (!resolved) return { VansActionError::InvalidHandle, {}, "Action Host was not found" };
	VansActionActivationRequest request;
	request.spec = spec;
	request.context = std::move(context);
	request.hasAuthority = hasAuthority;
	request.predicted = predicted;
	return resolved->Activate(request);
}

VansActionResult VansActionSystem::TryActivate(
	VansActionHostRef host,
	VansActionId action,
	VansActionContext context,
	bool hasAuthority,
	bool predicted)
{
	auto resolved = Resolve(host);
	if (!resolved) return { VansActionError::InvalidHandle, {}, "Action Host was not found" };
	const auto grants = resolved->GrantedActions();
	const auto spec = std::find_if(grants.begin(), grants.end(),
		[action](const auto& value) { return value.action == action; });
	if (spec == grants.end()) return { VansActionError::NotGranted, {}, "Action is not granted" };
	return TryActivate(host, spec->handle, std::move(context), hasAuthority, predicted);
}

bool VansActionSystem::RequestCancel(
	VansActionInstanceRef action,
	VansActionCancelReason reason,
	std::string& error)
{
	auto host = Resolve(action.host);
	if (!host) { error = "Action Host was not found"; return false; }
	return host->Cancel(action.action, reason, error);
}

bool VansActionSystem::Matches(const VansActionView& view, const VansActionQuery& query)
{
	if (query.host && view.host.owner != query.host.owner) return false;
	if (query.action && view.instance.action != query.action) return false;
	if (query.state && view.instance.state != *query.state) return false;
	if (query.prediction && !(view.instance.prediction == *query.prediction)) return false;
	return true;
}

std::vector<VansActionView> VansActionSystem::QueryActive(const VansActionQuery& query) const
{
	std::vector<VansActionView> result;
	const auto append = [&](const std::shared_ptr<VansActionHost>& host)
	{
		if (!host) return;
		for (VansActionInstanceSnapshot instance : host->ActiveActions())
		{
			VansActionView view{ { host->Owner() }, std::move(instance) };
			if (Matches(view, query)) result.push_back(std::move(view));
		}
	};
	if (query.host) append(Resolve(query.host));
	else for (const auto& host : m_Runtime.Hosts()) append(host);
	return result;
}

std::size_t VansActionSystem::InterruptByQuery(
	const VansActionQuery& query,
	std::string& error)
{
	std::size_t count = 0;
	for (const VansActionView& view : QueryActive(query))
	{
		auto host = Resolve(view.host);
		if (!host || !host->Interrupt(view.instance.handle, error)) return count;
		++count;
	}
	return count;
}

std::optional<VansActionView> VansActionSystem::Inspect(VansActionInstanceRef action) const
{
	auto host = Resolve(action.host);
	if (!host) return std::nullopt;
	auto snapshot = host->Query(action.action);
	if (!snapshot) return std::nullopt;
	return VansActionView{ action.host, std::move(*snapshot) };
}
}
