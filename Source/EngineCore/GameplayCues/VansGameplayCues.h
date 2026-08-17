#pragma once

#include "../GameplayActionCore/VansActionServices.h"
#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"
#include "../RuntimeCore/VansGenerationPool.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vans
{
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
	VansPredictionKey prediction;
	VansCueId cue;
	std::uint32_t sequence = 0;

	bool IsValid() const { return cue.IsValid() && sequence != 0; }
	friend bool operator==(const VansGameplayCueKey& left, const VansGameplayCueKey& right)
	{
		return left.prediction == right.prediction && left.cue == right.cue &&
			left.sequence == right.sequence;
	}
};

struct VansGameplayCueKeyHash
{
	std::size_t operator()(const VansGameplayCueKey& key) const noexcept
	{
		std::size_t value = std::hash<std::uint64_t>{}(key.cue.value);
		value ^= std::hash<std::uint32_t>{}(key.prediction.connection) + 0x9e3779b9u + (value << 6) + (value >> 2);
		value ^= std::hash<std::uint32_t>{}(key.prediction.sequence) + 0x9e3779b9u + (value << 6) + (value >> 2);
		value ^= std::hash<std::uint32_t>{}(key.sequence) + 0x9e3779b9u + (value << 6) + (value >> 2);
		return value;
	}
};

struct VansGameplayCueParameters
{
	VansActionContext context;
	VansEntityHandle target;
	std::array<double, 3> position{};
	std::array<double, 3> direction{};
	double intensity = 1.0;
	VansGameplayTagId surface;
	VansSerializedValue payload = VansSerializedValue::Object({});
};

struct VansGameplayCueAdapterMapping
{
	std::string serviceName;
	VansActionServiceId service;
	std::string commandName;
	VansActionFieldId command;
	std::string updateCommandName;
	VansActionFieldId updateCommand;
	std::string removeCommandName;
	VansActionFieldId removeCommand;
	std::string asset;
	VansSerializedValue parameters = VansSerializedValue::Object({});
};

class IVansGameplayCueAdapter
{
public:
	virtual ~IVansGameplayCueAdapter() = default;
	virtual VansCueId CueId() const = 0;
	virtual std::string_view StableName() const = 0;
	virtual VansGameplayCueScope DefaultScope() const { return VansGameplayCueScope::Target; }
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
		std::vector<VansGameplayCueAdapterMapping> mappings,
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
	struct BoundResource
	{
		std::size_t mapping = 0;
		VansGenerationHandle external;
		bool active = true;
	};
	struct ActiveCue
	{
		VansGameplayCueKey key;
		VansGameplayCueScope scope = VansGameplayCueScope::Target;
		VansGameplayCueParameters parameters;
		std::vector<BoundResource> resources;
	};

	VansActionCommandResult Run(
		const VansGameplayCueAdapterMapping& mapping,
		std::string_view commandName,
		VansActionFieldId command,
		const VansGameplayCueParameters& parameters,
		VansGenerationHandle resource) const;
	bool ReleaseBound(BoundResource& resource,
		const VansGameplayCueParameters* parameters, std::string& error) const;

	VansCueId m_Cue;
	std::string m_StableName;
	VansGameplayCueScope m_Scope = VansGameplayCueScope::Target;
	std::vector<VansGameplayCueAdapterMapping> m_Mappings;
	const VansActionServiceRegistry* m_Services = nullptr;
	VansGenerationPool<ActiveCue> m_Active;
};

class VansGameplayCueRegistry
{
public:
	bool Register(std::shared_ptr<IVansGameplayCueAdapter> adapter, std::string& error);
	bool Seal(std::string& error);
	std::shared_ptr<IVansGameplayCueAdapter> Resolve(VansCueId cue) const;
	VansGameplayCueScope DefaultScope(VansCueId cue) const;
	bool IsSealed() const { return m_Sealed; }

private:
	bool m_Sealed = false;
	std::unordered_map<VansCueId, std::shared_ptr<IVansGameplayCueAdapter>> m_Adapters;
};

class VansGameplayCueService
{
public:
	explicit VansGameplayCueService(const VansGameplayCueRegistry* registry = nullptr)
		: m_Registry(registry) {}

	void SetRegistry(const VansGameplayCueRegistry* registry) { m_Registry = registry; }
	bool Execute(
		const VansGameplayCueKey& key,
		VansGameplayCueScope scope,
		const VansGameplayCueParameters& parameters,
		std::string& error);
	VansCueHandle Add(
		const VansGameplayCueKey& key,
		VansGameplayCueScope scope,
		const VansGameplayCueParameters& parameters,
		std::uint64_t source,
		std::string& error);
	bool Update(VansCueHandle handle, const VansGameplayCueParameters& parameters, std::string& error);
	bool Remove(VansCueHandle handle, std::string& error);
	VansGameplayCueScope DefaultScope(VansCueId cue) const;
	std::size_t RemoveSource(std::uint64_t source);
	void Clear();
	std::size_t ActiveCount() const { return m_Active.ActiveCount(); }

private:
	struct ActiveCue
	{
		std::shared_ptr<IVansGameplayCueAdapter> adapter;
		VansGenerationHandle resource;
		VansGameplayCueKey key;
		std::uint64_t source = 0;
	};

	const VansGameplayCueRegistry* m_Registry = nullptr;
	VansGenerationPool<ActiveCue> m_Active;
	std::unordered_set<VansGameplayCueKey, VansGameplayCueKeyHash> m_Executed;
};
}
