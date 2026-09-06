#pragma once

#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"
#include "../RuntimeCore/VansGenerationPool.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vans
{
struct VansAttributeDefinition
{
	VansAttributeId id;
	std::uint64_t fieldId = 0;
	std::string name;
	double defaultValue = 0.0;
	double minimum = 0.0;
	double maximum = 0.0;
	bool hasMinimum = false;
	bool hasMaximum = false;
};

class VansAttributeRegistry
{
public:
	bool Register(VansAttributeDefinition definition, std::string& error);
	bool Seal(std::string& error);
	const VansAttributeDefinition* Resolve(VansAttributeId id) const;
	const std::vector<VansAttributeDefinition>& Definitions() const { return m_Definitions; }
	bool IsSealed() const { return m_Sealed; }

private:
	bool m_Sealed = false;
	std::vector<VansAttributeDefinition> m_Definitions;
	std::unordered_map<VansAttributeId, std::size_t> m_ById;
	std::unordered_map<std::uint64_t, std::size_t> m_ByFieldId;
};

enum class VansAttributeModifierOperation : std::uint8_t
{
	Additive,
	Multiplicative,
	Override
};

struct VansAttributeModifierDesc
{
	VansAttributeId attribute;
	VansAttributeModifierOperation operation = VansAttributeModifierOperation::Additive;
	double magnitude = 0.0;
	std::int32_t priority = 0;
	std::uint64_t sourceOrder = 0;
	std::uint64_t source = 0;
};

struct VansAttributeSnapshot
{
	VansAttributeId attribute;
	double baseValue = 0.0;
	double currentValue = 0.0;
};

class VansAttributeService
{
public:
	using ChangedCallback = std::function<void(VansAttributeId, double, double)>;

	explicit VansAttributeService(const VansAttributeRegistry* registry = nullptr)
		: m_Registry(registry) {}

	void SetRegistry(const VansAttributeRegistry* registry);
	bool InitializeDefaults(std::string& error);
	bool SetBase(VansAttributeId attribute, double value);
	bool AddBase(VansAttributeId attribute, double delta);
	double Base(VansAttributeId attribute) const;
	double Current(VansAttributeId attribute) const;
	VansAttributeModifierHandle AddModifier(const VansAttributeModifierDesc& desc);
	bool UpdateModifier(VansAttributeModifierHandle handle, const VansAttributeModifierDesc& desc);
	bool RemoveModifier(VansAttributeModifierHandle handle);
	std::size_t RemoveModifiersFromSource(std::uint64_t source);
	std::vector<VansAttributeSnapshot> Capture() const;
	void Restore(const std::vector<VansAttributeSnapshot>& snapshot);
	void BeginBatch();
	void EndBatch();
	void SetChangedCallback(ChangedCallback callback) { m_Changed = std::move(callback); }

private:
	struct AttributeState
	{
		double baseValue = 0.0;
		double currentValue = 0.0;
	};
	struct ModifierState
	{
		VansAttributeModifierDesc desc;
	};

	bool HasAttribute(VansAttributeId attribute) const;
	double Evaluate(VansAttributeId attribute) const;
	void MarkDirty(VansAttributeId attribute);
	void FlushDirty();

	const VansAttributeRegistry* m_Registry = nullptr;
	std::unordered_map<VansAttributeId, AttributeState> m_States;
	VansGenerationPool<ModifierState> m_Modifiers;
	std::unordered_set<VansAttributeId> m_Dirty;
	ChangedCallback m_Changed;
	std::uint32_t m_BatchDepth = 0;
};
}
