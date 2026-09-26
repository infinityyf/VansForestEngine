#pragma once

#include "../GameplayAttributes/VansGameplayAttributes.h"
#include "../GameplayActionSchema/VansGAFPerformanceBudget.h"
#include "../GameplayCues/VansGameplayCues.h"
#include "../GameplayTags/VansGameplayTags.h"
#include "../GameplayTargeting/VansGameplayTargeting.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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
	RayLength
};

enum class VansEffectModifierApplication : std::uint8_t
{
	Base,
	Persistent
};

enum class VansEffectModifierOperation : std::uint8_t
{
	Add,
	Multiply,
	Set
};

struct VansEffectModifier
{
	VansAttributeId attribute;
	VansEffectModifierApplication application = VansEffectModifierApplication::Base;
	VansEffectModifierOperation operation = VansEffectModifierOperation::Add;
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
	VansEffectDurationPolicy durationPolicy = VansEffectDurationPolicy::Instant;
	double durationSeconds = 0.0;
	double periodSeconds = 0.0;
	bool executePeriodicOnApply = false;
	VansEffectStackingPolicy stackingPolicy = VansEffectStackingPolicy::None;
	VansEffectOverflowPolicy overflowPolicy = VansEffectOverflowPolicy::Reject;
	std::uint32_t maximumStacks = 1;
	bool refreshDurationOnStack = false;
	bool resetPeriodOnStack = false;
	VansGameplayTagQuery requirements;
	VansGameplayTagQuery immunity;
	std::vector<VansGameplayTagId> grantedTags;
	std::vector<VansEffectModifier> modifiers;
	std::vector<VansCueId> executeCues;
	std::vector<VansCueId> persistentCues;
	std::vector<VansCueId> periodicCues;
	std::vector<VansCueId> removeCues;
};

enum class VansEffectPolicyField : std::uint8_t
{
	DurationSeconds,
	PeriodSeconds,
	ExecutePeriodicOnApply,
	StackingPolicy,
	OverflowPolicy,
	MaximumStacks,
	RefreshDuration,
	ResetPeriod,
	GrantedTags,
	PersistentCues,
	PeriodicCues,
	RemoveCues,
	Count
};

struct VansEffectPolicyIssue
{
	VansEffectPolicyField field = VansEffectPolicyField::DurationSeconds;
	std::string_view message;
};

struct VansEffectPolicyValidation
{
	std::array<VansEffectPolicyIssue,
		static_cast<std::size_t>(VansEffectPolicyField::Count)> issues{};
	std::size_t count = 0;

	bool IsValid() const { return count == 0; }
	const VansEffectPolicyIssue* begin() const { return issues.data(); }
	const VansEffectPolicyIssue* end() const { return issues.data() + count; }
};

VansEffectPolicyValidation VansValidateEffectPolicy(
	const VansEffectDefinition& definition);
std::string_view VansEffectPolicyFieldPath(VansEffectPolicyField field);

struct VansEffectSpec
{
	std::shared_ptr<const VansEffectDefinition> definition;
	VansActionContext context;
	VansTargetDataHandle targetData;
	std::uint64_t source = 0;
	double level = 1.0;
	std::unordered_map<VansActionFieldId, double> setByCaller;
	std::vector<double> snapshotModifierValues;
};

bool VansDecodeEffectSetByCaller(
	const VansSerializedValue* value,
	std::unordered_map<VansActionFieldId, double>& result,
	std::string& error);

class VansEffectRegistry
{
public:
	bool Register(std::shared_ptr<const VansEffectDefinition> definition, std::string& error);
	bool Seal(std::string& error);
	bool ValidatePerformanceBudget(
		const VansGAFPerformanceBudget& performance,
		std::string& error) const;
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
	std::uint64_t correlationId = 0;
};

struct VansPersistentEffectContextSlot
{
	std::string name;
	VansActionValueKind kind = VansActionValueKind::Serialized;
	VansSerializedValue serialized = VansSerializedValue::Object({});
	VansEntityHandle entity;
	VansTargetData targetData;
};

struct VansPersistentEffectContext
{
	std::uint64_t correlationId = 0;
	std::uint64_t randomSeed = 0;
	std::vector<VansPersistentEffectContextSlot> slots;
};

struct VansPersistentEffectSetByCallerState
{
	VansActionFieldId field;
	double value = 0.0;
};

struct VansPersistentEffectSnapshotState
{
	std::uint32_t modifierIndex = 0;
	double value = 0.0;
};

struct VansPersistentEffectStackState
{
	VansPersistentEffectContext context;
	VansTargetData targetData;
	bool hasTargetData = false;
	std::uint64_t applicationSequence = 0;
	std::uint64_t source = 0;
	double level = 1.0;
	std::vector<VansPersistentEffectSetByCallerState> setByCaller;
	std::vector<VansPersistentEffectSnapshotState> snapshots;
};

struct VansPersistentEffectState
{
	VansEffectId effect;
	std::uint64_t modifierOrder = 0;
	double remainingSeconds = 0.0;
	double periodRemainingSeconds = 0.0;
	std::vector<VansPersistentEffectStackState> stacks;
};

struct VansPersistentEffectServiceState
{
	std::uint64_t nextApplicationSequence = 1;
	std::vector<VansPersistentEffectState> activeEffects;
};

struct VansEffectApplicationResult
{
	VansActionError error = VansActionError::None;
	VansActiveEffectHandle active;
	bool stacked = false;
	std::string message;

	explicit operator bool() const { return error == VansActionError::None; }
};

struct VansEffectValidationResult
{
	VansActionError error = VansActionError::None;
	std::string message;

	explicit operator bool() const { return error == VansActionError::None; }
};

struct VansEffectTickResult
{
	VansActionError error = VansActionError::None;
	VansActiveEffectHandle effect;
	std::uint32_t executedPulses = 0;
	std::uint32_t removedEffects = 0;
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
		VansTargetDataStore* targetData = nullptr,
		VansGAFPerformanceBudget performance = {})
		: m_Attributes(attributes), m_Tags(tags), m_Cues(cues),
		  m_TargetData(targetData), m_Performance(performance) {}

	VansEffectValidationResult ValidateApplication(const VansEffectSpec& spec) const;
	VansEffectValidationResult ValidateApplications(
		const std::vector<VansEffectSpec>& specs) const;
	VansEffectApplicationResult Apply(const VansEffectSpec& spec);
	VansEffectTickResult Tick(double deltaSeconds);
	bool Remove(VansActiveEffectHandle handle, std::string& error);
	std::vector<VansActiveEffectSnapshot> Snapshot() const;
	bool CapturePersistentState(
		VansPersistentEffectServiceState& state,
		std::string& error) const;
	bool ValidatePersistentState(
		const VansPersistentEffectServiceState& state,
		const VansEffectRegistry& registry,
		std::string& error) const;
	bool RestorePersistentState(
		const VansPersistentEffectServiceState& state,
		const VansEffectRegistry& registry,
		std::string& error);
	void Clear();
	std::size_t ActiveCount() const { return m_Active.ActiveCount(); }

private:
	struct VansActiveEffectStack
	{
		VansEffectSpec spec;
		std::uint64_t applicationSequence = 0;
		std::vector<VansTargetDataHandle> ownedTargetData;
	};

	struct VansActiveEffect
	{
		std::vector<VansActiveEffectStack> stackStates;
		double remainingSeconds = 0.0;
		double periodRemainingSeconds = 0.0;
		std::uint64_t modifierOrder = 0;
		std::uint64_t tagSource = 0;
		std::vector<VansAttributeModifierHandle> modifiers;
		std::vector<VansCueHandle> cues;

		const VansEffectSpec& CurrentSpec() const { return stackStates.back().spec; }
		std::uint32_t StackCount() const
		{
			return static_cast<std::uint32_t>(stackStates.size());
		}
	};

	VansActiveEffectHandle FindStack(const VansEffectSpec& spec) const;
	VansEffectValidationResult ValidateSpec(
		const VansEffectSpec& spec,
		VansEffectSpec& prepared,
		std::uint64_t applicationSequence,
		const std::vector<VansGameplayTagId>* addedTags) const;
	VansEffectValidationResult ValidatePrepared(const VansEffectSpec& spec) const;
	bool PrepareSpec(const VansEffectSpec& source, VansEffectSpec& prepared,
		std::uint64_t applicationSequence, std::string& error) const;
	bool ResolveMagnitude(const VansEffectModifier& modifier, std::size_t modifierIndex,
		const VansEffectSpec& spec, double& magnitude, std::string& error) const;
	bool AggregateMagnitude(const VansEffectModifier& modifier, std::size_t modifierIndex,
		const std::vector<VansActiveEffectStack>& stackStates,
		double& magnitude, std::string& error) const;
	bool ApplyBaseModifiers(const VansEffectSpec& spec, std::string& error);
	bool ApplyBaseModifiers(const VansActiveEffect& effect, std::string& error);
	bool ApplyPersistentResources(VansActiveEffectHandle handle, VansActiveEffect& effect,
		bool emitExecuteCues, std::string& error);
	bool RebuildStackResources(VansActiveEffectHandle handle, VansActiveEffect& effect, std::string& error);
	bool RefreshDynamicModifiers(VansActiveEffectHandle handle, VansActiveEffect& effect, std::string& error);
	void ReleaseResources(VansActiveEffect& effect);
	bool EmitExecuteCues(
		const std::vector<VansCueId>& cues,
		const VansActiveEffect& effect,
		std::string& error);
	VansGameplayCueParameters BuildCueParameters(const VansActiveEffect& effect) const;
	void ClearForRestore();
	void ReleaseTargetData(const std::vector<VansTargetDataHandle>& handles);
	std::uint64_t ModifierOrderFor(VansActiveEffectHandle handle) const;
	static std::uint64_t SourceForHandle(VansActiveEffectHandle handle);

	VansAttributeService* m_Attributes = nullptr;
	VansGameplayTagContainer* m_Tags = nullptr;
	VansGameplayCueService* m_Cues = nullptr;
	VansTargetDataStore* m_TargetData = nullptr;
	VansGAFPerformanceBudget m_Performance;
	VansGenerationPool<VansActiveEffect> m_Active;
	std::uint64_t m_NextApplicationSequence = 1;
	std::uint32_t m_CueSequence = 1;
};
}
