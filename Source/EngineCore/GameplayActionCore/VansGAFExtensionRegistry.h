#pragma once

#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansGAFExtensionKind : std::uint8_t
{
	ValueType,
	Policy,
	Guard,
	Operation,
	Driver,
	Transition,
	Extension,
	Signal,
	Task,
	Resource,
	ContextSlot
};

struct VansGAFTypeDescriptor
{
	std::string typeId;
	std::string displayName;
	VansGAFExtensionKind kind = VansGAFExtensionKind::ValueType;
};

using VansGAFValueValidator =
	std::function<bool(const VansSerializedValue&, std::string&)>;

struct VansGAFValueTypeDescriptor
{
	std::string typeId;
	std::string displayName;
	VansGAFValueValidator validate;
};

class VansGAFTypeRegistry
{
public:
	bool RegisterType(VansGAFTypeDescriptor descriptor, std::string& error);
	bool RegisterValueType(VansGAFValueTypeDescriptor descriptor, std::string& error);
	bool Seal(std::string& error);
	const VansGAFTypeDescriptor* ResolveType(std::string_view typeId) const;
	const VansGAFValueTypeDescriptor* ResolveValueType(std::string_view typeId) const;
	bool IsSealed() const { return m_Sealed; }
	std::uint64_t Fingerprint() const;

private:
	bool m_Sealed = false;
	std::unordered_map<std::string, VansGAFTypeDescriptor> m_Types;
	std::unordered_map<std::string, VansGAFValueTypeDescriptor> m_ValueTypes;
};

struct VansGAFInputFieldDescriptor
{
	std::string name;
	std::string valueType;
	bool required = false;
	VansSerializedValue defaultValue;
};

struct VansGAFInputSchemaDescriptor
{
	std::string typeId;
	std::vector<VansGAFInputFieldDescriptor> fields;
};

class VansGAFSchemaRegistry
{
public:
	explicit VansGAFSchemaRegistry(const VansGAFTypeRegistry* types = nullptr)
		: m_Types(types) {}

	void BindTypes(const VansGAFTypeRegistry& types) { m_Types = &types; }
	bool Register(VansGAFInputSchemaDescriptor descriptor, std::string& error);
	bool Seal(std::string& error);
	const VansGAFInputSchemaDescriptor* Resolve(std::string_view typeId) const;
	const VansGAFValueTypeDescriptor* ResolveValueType(std::string_view typeId) const
		{ return m_Types ? m_Types->ResolveValueType(typeId) : nullptr; }
	VansGameplayDiagnostics Validate(
		VansGAFExtensionKind expectedKind,
		std::string_view typeId,
		const VansSerializedValue& inputs,
		std::string_view fieldPath) const;
	bool IsSealed() const { return m_Sealed; }
	std::uint64_t Fingerprint() const;

private:
	const VansGAFTypeRegistry* m_Types = nullptr;
	bool m_Sealed = false;
	std::unordered_map<std::string, VansGAFInputSchemaDescriptor> m_Schemas;
};

bool VansRegisterCoreGAFTypes(VansGAFTypeRegistry& registry, std::string& error);
bool VansRegisterCoreGAFSchemas(VansGAFSchemaRegistry& registry, std::string& error);
bool VansRegisterGameplayPrimitiveGAFTypes(VansGAFTypeRegistry& registry, std::string& error);
bool VansRegisterGameplayPrimitiveGAFSchemas(VansGAFSchemaRegistry& registry, std::string& error);
bool VansRegisterDefaultEngineGAFTypes(VansGAFTypeRegistry& registry, std::string& error);
bool VansRegisterDefaultEngineGAFSchemas(VansGAFSchemaRegistry& registry, std::string& error);
}
