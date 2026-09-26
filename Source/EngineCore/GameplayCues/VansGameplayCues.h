#pragma once

#include "../GameplayActionCore/VansActionServices.h"
#include "../GameplayActionSchema/VansGAFPerformanceBudget.h"
#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"
#include "../RuntimeCore/VansGenerationPool.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vans
{
struct VansTargetData;

enum class VansGameplayCueScope : std::uint8_t
{
	Owner,
	Target,
	Observers,
	World,
	LocalOnly
};

struct VansGameplayCueKey
{
	std::uint64_t correlationId = 0;
	VansCueId cue;
	std::uint64_t producerId = 0;
	std::uint32_t sequence = 0;

	bool IsValid() const { return cue.IsValid() && producerId != 0 && sequence != 0; }
	friend bool operator==(const VansGameplayCueKey& left, const VansGameplayCueKey& right)
	{
		return left.correlationId == right.correlationId && left.cue == right.cue &&
			left.producerId == right.producerId && left.sequence == right.sequence;
	}
};

struct VansGameplayCueKeyHash
{
	std::size_t operator()(const VansGameplayCueKey& key) const noexcept
	{
		std::size_t value = std::hash<std::uint64_t>{}(key.cue.value);
		value ^= std::hash<std::uint64_t>{}(key.correlationId) + 0x9e3779b9u +
			(value << 6) + (value >> 2);
		value ^= std::hash<std::uint64_t>{}(key.producerId) + 0x9e3779b9u +
			(value << 6) + (value >> 2);
		value ^= std::hash<std::uint32_t>{}(key.sequence) + 0x9e3779b9u + (value << 6) + (value >> 2);
		return value;
	}
};

enum class VansGameplayCueExecuteStatus : std::uint8_t
{
	Failed,
	Executed,
	Suppressed
};

struct VansGameplayCueParameters
{
	VansActionContext context;
	VansEntityHandle target;
	std::optional<std::array<double, 3>> position;
	std::optional<std::array<double, 3>> origin;
	std::optional<std::array<double, 3>> direction;
	std::optional<std::array<double, 3>> normal;
	double intensity = 1.0;
	VansGameplayTagId surface;
	VansSerializedValue payload = VansSerializedValue::Object({});
};

enum class VansGameplayCueSource : std::uint8_t
{
	Asset,
	Target,
	Position,
	Origin,
	Direction,
	Normal,
	Surface,
	Intensity,
	Payload
};

bool VansReadGameplayCueSource(std::string_view name, VansGameplayCueSource& source);
std::string_view VansGameplayCueSourceName(VansGameplayCueSource source);
void VansApplyGameplayCueTargetData(
	VansGameplayCueParameters& parameters,
	const VansTargetData& targetData);

struct VansGameplayCueFieldBinding
{
	std::string field;
	VansGameplayCueSource source = VansGameplayCueSource::Payload;
};

struct VansGameplayCueCommandBinding
{
	VansActionFieldId command;
	VansSerializedValue values = VansSerializedValue::Object({});
	std::vector<VansGameplayCueFieldBinding> fields;
};

struct VansGameplayCueBinding
{
	VansActionServiceId service;
	std::string asset;
	VansGameplayCueCommandBinding invoke;
	VansGameplayCueCommandBinding update;
	VansGameplayCueCommandBinding release;
};

class IVansGameplayCueAdapter
{
public:
	virtual ~IVansGameplayCueAdapter() = default;
	virtual VansCueId CueId() const = 0;
	virtual std::string_view StableName() const = 0;
	virtual VansGameplayCueScope DefaultScope() const = 0;
	virtual bool Execute(
		const VansGameplayCueKey& key,
		VansGameplayCueScope scope,
		const VansGameplayCueParameters& parameters,
		std::string& error) = 0;
	virtual VansGenerationHandle Add(
		const VansGameplayCueKey& key,
		VansGameplayCueScope scope,
		const VansGameplayCueParameters& parameters,
		std::string& error) = 0;
	virtual bool Update(
		VansGenerationHandle resource,
		const VansGameplayCueParameters& parameters,
		std::string& error) = 0;
	virtual bool Remove(VansGenerationHandle resource, std::string& error) = 0;
};

class VansActionServiceGameplayCueAdapter final : public IVansGameplayCueAdapter
{
public:
	VansActionServiceGameplayCueAdapter(
		VansCueId cue,
		std::string stableName,
		VansGameplayCueScope scope,
		VansGameplayCueBinding binding,
		const VansActionServiceRegistry* services);

	bool Validate(std::string& error) const;
	VansCueId CueId() const override { return m_Cue; }
	std::string_view StableName() const override { return m_StableName; }
	VansGameplayCueScope DefaultScope() const override { return m_Scope; }
	bool Execute(const VansGameplayCueKey& key, VansGameplayCueScope scope,
		const VansGameplayCueParameters& parameters, std::string& error) override;
	VansGenerationHandle Add(const VansGameplayCueKey& key, VansGameplayCueScope scope,
		const VansGameplayCueParameters& parameters, std::string& error) override;
	bool Update(VansGenerationHandle resource,
		const VansGameplayCueParameters& parameters, std::string& error) override;
	bool Remove(VansGenerationHandle resource, std::string& error) override;

private:
	struct ActiveCue
	{
		VansGameplayCueParameters parameters;
		VansGenerationHandle resource;
	};

	VansActionCommandResult Run(
		const VansGameplayCueCommandBinding& command,
		const VansGameplayCueParameters& parameters,
		VansGenerationHandle resource) const;
	bool ReleaseBound(VansGenerationHandle resource,
		const VansGameplayCueParameters& parameters, std::string& error) const;

	VansCueId m_Cue;
	std::string m_StableName;
	VansGameplayCueScope m_Scope;
	VansGameplayCueBinding m_Binding;
	const VansActionServiceRegistry* m_Services = nullptr;
	VansGenerationPool<ActiveCue> m_Active;
};

class VansGameplayCueRegistry
{
public:
	bool Register(std::shared_ptr<IVansGameplayCueAdapter> adapter, std::string& error);
	bool Seal(bool allowEmpty, std::string& error);
	std::shared_ptr<IVansGameplayCueAdapter> Resolve(VansCueId cue) const;
	bool IsSealed() const { return m_Sealed; }

private:
	bool m_Sealed = false;
	std::unordered_map<VansCueId, std::shared_ptr<IVansGameplayCueAdapter>> m_Adapters;
};

class VansGameplayCueService
{
public:
	explicit VansGameplayCueService(
		const VansGameplayCueRegistry* registry = nullptr,
		std::size_t maximumExecutionHistory =
			VansGAFPerformanceBudget::DefaultMaximumCueHistoryPerHost)
		: m_Registry(registry), m_MaximumExecutionHistory(maximumExecutionHistory) {}

	void SetRegistry(const VansGameplayCueRegistry* registry) { m_Registry = registry; }
	bool Contains(VansCueId cue) const { return m_Registry && m_Registry->Resolve(cue); }
	VansGameplayCueExecuteStatus Execute(
		const VansGameplayCueKey& key,
		std::optional<VansGameplayCueScope> scopeOverride,
		const VansGameplayCueParameters& parameters,
		std::string& error);
	VansCueHandle Add(
		const VansGameplayCueKey& key,
		std::optional<VansGameplayCueScope> scopeOverride,
		const VansGameplayCueParameters& parameters,
		std::string& error);
	bool Update(VansCueHandle handle, const VansGameplayCueParameters& parameters, std::string& error);
	bool Remove(VansCueHandle handle, std::string& error);
	void Clear();
	std::size_t ActiveCount() const { return m_Active.ActiveCount(); }

private:
	struct ActiveCue
	{
		std::shared_ptr<IVansGameplayCueAdapter> adapter;
		VansGenerationHandle resource;
	};

	const VansGameplayCueRegistry* m_Registry = nullptr;
	VansGenerationPool<ActiveCue> m_Active;
	std::unordered_set<VansGameplayCueKey, VansGameplayCueKeyHash> m_Executed;
	std::deque<VansGameplayCueKey> m_ExecutionOrder;
	std::size_t m_MaximumExecutionHistory;
};
}
