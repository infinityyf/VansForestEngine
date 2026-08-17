#pragma once

#include "VansTimelineEvaluation.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
class VansTimelineBindingResolver;

using VansTimelineApplierSlot = std::uint32_t;
inline constexpr VansTimelineApplierSlot VansInvalidTimelineApplierSlot = UINT32_MAX;

struct VansTimelineResourceId
{
	std::uint64_t type = 0;
	std::uint64_t instance = 0;
	explicit operator bool() const { return type != 0 && instance != 0; }
	friend bool operator==(const VansTimelineResourceId& left, const VansTimelineResourceId& right)
	{ return left.type == right.type && left.instance == right.instance; }
};

struct VansTimelineResourceIdHash
{
	std::size_t operator()(const VansTimelineResourceId& resource) const noexcept
	{ return std::hash<std::uint64_t>{}(resource.type) ^ (std::hash<std::uint64_t>{}(resource.instance) << 1); }
};

struct VansTimelineRestoreToken
{
	VansTimelineRestoreHandle handle;
	VansTimelineApplierSlot applier = VansInvalidTimelineApplierSlot;
	VansTimelineWriterHandle writer;
	VansTimelineResourceId resource;
};

struct VansTimelineApplyContext
{
	const VansCompiledTimeline& timeline;
	const VansCompiledTimelineTrack& track;
	const VansCompiledTimelineSection* section = nullptr;
	VansTimelineSessionHandle session;
	VansTimelineSessionHandle root;
	VansTimelineSessionKind sessionKind = VansTimelineSessionKind::External;
	VansTimelineWriterHandle writer;
	VansTimelineStableOrder order;
	VansTimelineBlendMode blendMode = VansTimelineBlendMode::Override;
	VansTimelineCompletionMode completion = VansTimelineCompletionMode::RestoreState;
	VansTimelineBindingResolver* bindings = nullptr;
	VansTimelineDiagnostics* diagnostics = nullptr;
};

enum class VansTimelineApplyStatus : std::uint8_t { Applied, Ignored, Failed };

struct VansTimelineApplyResult
{
	VansTimelineApplyStatus status = VansTimelineApplyStatus::Applied;
	VansTimelineRestoreToken restore;
	std::string error;
};

class IVansTimelineOutputApplier
{
public:
	virtual ~IVansTimelineOutputApplier() = default;
	virtual VansTimelineOutputTypeId OutputType() const = 0;
	virtual std::string_view StableName() const = 0;
	virtual std::uint32_t PayloadSize() const = 0;
	virtual std::uint32_t PayloadAlignment() const = 0;
	virtual VansTimelineApplyResult Apply(
		const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target,
		VansTimelineOutputPayloadView payload) = 0;
	virtual bool Restore(VansTimelineRestoreToken token) = 0;
	// Called as soon as a restoring writer becomes inactive, even when its restore
	// token must wait below a higher-priority writer on the same resource.
	virtual void DeactivateWriter(VansTimelineWriterHandle writer) { (void)writer; }
	virtual void ReleaseWriter(VansTimelineWriterHandle writer) = 0;
	virtual void ReleaseAll() = 0;
};

class VansTimelineApplierRegistry
{
public:
	bool Register(std::shared_ptr<IVansTimelineOutputApplier> applier, std::string& error);
	bool Seal(std::string& error);
	bool IsSealed() const { return m_Sealed; }
	VansTimelineApplierSlot SlotOf(VansTimelineOutputTypeId type) const;
	IVansTimelineOutputApplier* At(VansTimelineApplierSlot slot) const;
	void Apply(
		const VansCompiledTimeline& timeline,
		std::vector<VansTimelineEvaluationOutput>& outputs,
		VansTimelineBindingResolver& bindings,
		class VansTimelineWriterRegistry& writers,
		class VansTimelinePreAnimatedState& preAnimated,
		VansTimelineDiagnostics& diagnostics) const;
	void ReleaseWriter(VansTimelineWriterHandle writer) const;
	void ReleaseAll() const;

private:
	bool m_Sealed = false;
	std::vector<std::shared_ptr<IVansTimelineOutputApplier>> m_Appliers;
	std::unordered_map<VansTimelineOutputTypeId, VansTimelineApplierSlot> m_ByType;
};
}
