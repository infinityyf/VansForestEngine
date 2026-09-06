#pragma once

#include "VansGameplayAssetStorage.h"

#include "../EventCore/VansPayloadSchemaRegistry.h"
#include "../GameplayActionCore/VansActionDefinition.h"
#include "../GameplayActionCore/VansActionHost.h"
#include "../GameplayActionExecution/VansActionExecutionGraph.h"
#include "../GameplayAttributes/VansGameplayAttributes.h"
#include "../GameplayEffects/VansGameplayEffects.h"
#include "../GameplayTargeting/VansGameplayTargeting.h"

#include <memory>
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Vans
{
struct VansCompiledGameplayCueDefinition
{
	VansCueId id;
	std::string name;
	VansGameplayCueScope scope = VansGameplayCueScope::Target;
	std::string payloadSchemaAsset;
	std::vector<VansGameplayCueAdapterMapping> adapterMappings;
};

struct VansCompiledAttributeSetDefinition
{
	VansStableId<struct VansAttributeSetIdTag> id;
	std::string name;
	std::vector<VansAttributeDefinition> attributes;
};

struct VansCompiledGameplayTagTreeDefinition
{
	VansStableId<struct VansGameplayTagTreeIdTag> id;
	std::string name;
	std::vector<VansGameplayTagDefinition> tags;
};

struct VansCompiledGameplayExtensionAsset
{
	std::string typeId;
	std::string stableName;
	std::shared_ptr<const void> data;
};

template <typename T>
const T* VansResolveCompiledGameplayExtension(
	const VansCompiledGameplayExtensionAsset* extension,
	std::string_view typeId)
{
	return extension && extension->typeId == typeId && extension->data
		? static_cast<const T*>(extension->data.get()) : nullptr;
}

using VansCompiledGameplayAssetData = std::variant<
	std::monostate,
	std::shared_ptr<const VansCompiledActionDefinition>,
	VansActionSetDefinition,
	std::shared_ptr<const VansEffectDefinition>,
	VansCompiledGameplayCueDefinition,
	VansCompiledAttributeSetDefinition,
	VansTargetingPolicy,
	VansCompiledGameplayTagTreeDefinition,
	VansPayloadSchema,
	std::shared_ptr<const VansCompiledActionGraph>,
	VansCompiledGameplayExtensionAsset>;

struct VansCompiledGameplayAsset
{
	VansAssetType assetType = VansAssetType::Unknown;
	std::uint64_t contentHash = 0;
	std::vector<std::string> dependencies;
	VansCompiledGameplayAssetData data;
};

struct VansGameplayCompileResult
{
	VansCompiledGameplayAsset asset;
	VansGameplayDiagnostics diagnostics;
	std::string error;

	explicit operator bool() const { return error.empty(); }
};

class VansGameplayAssetCompilerRegistry
{
public:
	using Compiler = std::function<bool(
		const VansGameplayCookedAsset&,
		VansCompiledGameplayAssetData&,
		VansGameplayDiagnostics&)>;

	bool Register(
		VansAssetType assetType,
		std::string stableName,
		Compiler compiler,
		std::string& error);
	bool Seal(std::string& error);
	const Compiler* Resolve(VansAssetType assetType) const;
	bool Contains(VansAssetType assetType) const { return Resolve(assetType) != nullptr; }
	bool IsSealed() const { return m_Sealed; }

private:
	struct Entry
	{
		std::string stableName;
		Compiler compiler;
	};
	std::unordered_map<VansAssetType, Entry> m_Compilers;
	bool m_Sealed = false;
};

bool VansRegisterCoreGameplayAssetCompilers(
	VansGameplayAssetCompilerRegistry& registry,
	std::string& error);
bool VansRegisterGameplayPrimitiveAssetCompilers(
	VansGameplayAssetCompilerRegistry& registry,
	std::string& error);
bool VansRegisterDefaultGameplayAssetCompilers(
	VansGameplayAssetCompilerRegistry& registry,
	std::string& error);

class VansGameplayAssetCompiler
{
public:
	static VansGameplayCompileResult Compile(const VansGameplayCookedAsset& cooked);
	static VansGameplayCompileResult Compile(
		const VansGameplayCookedAsset& cooked,
		const VansGameplayAssetCompilerRegistry& compilers);
};
}
