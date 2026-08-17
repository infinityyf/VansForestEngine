#pragma once

#include "../GameplayAttributes/VansGameplayAttributes.h"
#include "../GameplayCues/VansGameplayCues.h"
#include "../GameplayTags/VansGameplayTags.h"
#include "../GameplayTargeting/VansGameplayTargeting.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansEffectDurationPolicy : std::uint8_t
{
	Instant,
	Duration,
	Infinite
};

enum class VansEffectStackingPolicy : std::uint8_t
{
	None,
	AggregateBySource,
	AggregateByTarget
};

enum class VansEffectOverflowPolicy : std::uint8_t
{
	Reject,
	RefreshOnly,
	ReplaceOldest
};

enum class VansEffectMagnitudeSource : std::uint8_t
{
	Fixed,
	SetByCaller,
	CapturedAttribute,
	ContextPayload,
	TargetData,
	RandomRange
};

enum class VansEffectCapturePolicy : std::uint8_t
{
	Snapshot,
	Dynamic
};

enum class VansEffectTargetDataMetric : std::uint8_t
{
	Count,
	HitDistance,
	AreaRadius,
	RayLength
};

struct VansEffectModifier
{
	VansAttributeId attribute;
	VansAttributeModifierOperation operation = VansAttributeModifierOperation::Additive;
	double magnitude = 0.0;
	std::int32_t priority = 0;
	VansEffectMagnitudeSource magnitudeSource = VansEffectMagnitudeSource::Fixed;
	VansActionFieldId setByCallerField;
	VansAttributeId capturedAttribute;
	VansEffectCapturePolicy capturePolicy = VansEffectCapturePolicy::Snapshot;
	std::string contextPayloadPath;
	VansEffectTargetDataMetric targetDataMetric = VansEffectTargetDataMetric::Count;
	double randomMinimum = 0.0;
	double randomMaximum = 1.0;
	double coefficient = 1.0;
	double preAdd = 0.0;
	double postAdd = 0.0;
};

struct VansEffectDefinition
{
	VansEffectId id;
	std::string name;
	std::uint32_t definitionVersion = 1;
	VansEffectDurationPolicy durationPolicy = VansEffectDurationPolicy::Instant;
	double durationSeconds = 0.0;
	double periodSeconds = 0.0;
	bool executePeriodicOnApply = false;
	VansEffectStackingPolicy stackingPolicy = VansEffectStackingPolicy::None;
	VansEffectOverflowPolicy overflowPolicy = VansEffectOverflowPolicy::Reject;
	std::uint32_t maximumStacks = 1;
	bool refreshDurationOnStack = true;
	bool resetPeriodOnStack = false;
	VansGameplayTagQuery requirements;
	VansGameplayTagQuery immunity;
	std::vector<VansGameplayTagId> effectTags;
	std::vector<VansGameplayTagId> grantedTags;
	std::vector<VansEffectModifier> modifiers;
	std::vector<VansCueId> executeCues;
	std::vector<VansCueId> persistentCues;
	std::vector<VansCueId> periodicCues;
	std::vector<VansCueId> removeCues;
	std::vector<std::string> executeCueReferences;
	std::vector<std::string> persistentCueReferences;
	std::vector<std::string> periodicCueReferences;
	std::vector<std::string> removeCueReferences;
};

struct VansEffectSpec
{
	std::shared_ptr<const VansEffectDefinition> definition;
	VansActionContext context;
	VansTargetDataHandle targetData;
	std::uint64_t source = 0;
	double level = 1.0;
	std::unordered_map<VansActionFieldId, double> setByCaller;
	std::vector<double> capturedModifierValues;
};

class VansEffectRegistry
{
public:
	bool Register(std::shared_ptr<const VansEffectDefinition> definition, std::string& error);
	bool Seal(std::string& error);
	std::shared_ptr<const VansEffectDefinition> Resolve(VansEffectId id) const;
	bool IsSealed() const { return m_Sealed; }

private:
	bool m_Sealed = false;
	std::unordered_map<VansEffectId, std::shared_ptr<const VansEffectDefinition>> m_Definitions;
};

struct VansActiveEffectSnapshot
{
	VansActiveEffectHandle handle;
	VansEffectId effect;
	std::uint64_t source = 0;
	double remainingSeconds = 0.0;
	double periodRemainingSeconds = 0.0;
	std::uint32_t stacks = 1;
	VansPredictionKey prediction;
};

struct VansEffectApplicationResult
{
	VansActionError error = VansActionError::None;
	VansActiveEffectHandle active;
	bool stacked = false;
	std::string message;

	explicit operator bool() const { return error == VansActionError::None; }
};

class VansGameplayEffectService
{
public:
	VansGameplayEffectService(
		VansAttributeService* attributes,
		VansGameplayTagContainer* tags,
		VansGameplayCueService* cues,
		std::size_t maximumActiveEffects = 256,
		const VansTargetDataStore* targetData = nullptr)
		: m_Attributes(attributes), m_Tags(tags), m_Cues(cues),
		  m_TargetData(targetData), m_MaximumActiveEffects(maximumActiveEffects) {}

	VansEffectApplicationResult Apply(const VansEffectSpec& spec);
	void Tick(double deltaSeconds);
	bool Remove(VansActiveEffectHandle handle, std::string& error);
	std::size_t RemoveByEffect(VansEffectId effect);
	std::size_t RemoveBySource(std::uint64_t source);
	std::size_t RemoveMatchingTags(const VansGameplayTagQuery& query);
	std::vector<VansActiveEffectSnapshot> Snapshot() const;
	void Clear();
	std::size_t ActiveCount() const { return m_Active.ActiveCount(); }

private:
	struct ActiveEffect
	{
		VansEffectSpec spec;
		std::vector<VansEffectSpec> stackSpecs;
		double remainingSeconds = 0.0;
		double periodRemainingSeconds = 0.0;
		std::uint32_t stacks = 1;
		std::uint64_t tagSource = 0;
		std::vector<VansAttributeModifierHandle> modifiers;
		std::vector<VansCueHandle> cues;
	};

	VansActiveEffectHandle FindStack(const VansEffectSpec& spec) const;
	bool PrepareSpec(const VansEffectSpec& source, VansEffectSpec& prepared, std::string& error) const;
	bool ResolveMagnitude(const VansEffectModifier& modifier, std::size_t modifierIndex,
		const VansEffectSpec& spec, double& magnitude, std::string& error) const;
	bool AggregateMagnitude(const VansEffectModifier& modifier, std::size_t modifierIndex,
		const std::vector<VansEffectSpec>& stackSpecs, double& magnitude, std::string& error) const;
	bool ApplyInstantModifiers(const VansEffectSpec& spec, std::uint32_t stacks, std::string& error);
	bool ApplyStackModifiers(const ActiveEffect& effect, std::string& error);
	bool ApplyPersistentResources(VansActiveEffectHandle handle, ActiveEffect& effect, std::string& error);
	bool RebuildStackResources(VansActiveEffectHandle handle, ActiveEffect& effect, std::string& error);
	bool RefreshDynamicModifiers(VansActiveEffectHandle handle, ActiveEffect& effect, std::string& error);
	void ReleaseResources(ActiveEffect& effect);
	bool EmitExecuteCues(
		const std::vector<VansCueId>& cues,
		const ActiveEffect& effect,
		std::string& error);
	VansGameplayCueParameters BuildCueParameters(const ActiveEffect& effect) const;
	static std::uint64_t SourceForHandle(VansActiveEffectHandle handle);

	VansAttributeService* m_Attributes = nullptr;
	VansGameplayTagContainer* m_Tags = nullptr;
	VansGameplayCueService* m_Cues = nullptr;
	const VansTargetDataStore* m_TargetData = nullptr;
	VansGenerationPool<ActiveEffect> m_Active;
	std::size_t m_MaximumActiveEffects = 256;
	std::uint32_t m_CueSequence = 1;
};
}
