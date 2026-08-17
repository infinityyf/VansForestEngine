#pragma once

#include "VansGameplayAssetStorage.h"

#include "../CameraCore/VansCameraCore.h"
#include "../EventCore/VansPayloadSchemaRegistry.h"
#include "../GameplayActionCore/VansActionDefinition.h"
#include "../GameplayActionCore/VansActionHost.h"
#include "../GameplayActionExecution/VansActionExecutionGraph.h"
#include "../GameplayAttributes/VansGameplayAttributes.h"
#include "../GameplayEffects/VansGameplayEffects.h"
#include "../GameplayTargeting/VansGameplayTargeting.h"

#include <memory>
#include <string>
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
	VansCameraRigDefinition,
	VansCameraShakeDefinition>;

struct VansCompiledGameplayAsset
{
	VansAssetType assetType = VansAssetType::Unknown;
	std::uint32_t schemaVersion = 0;
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

class VansGameplayAssetCompiler
{
public:
	static VansGameplayCompileResult Compile(const VansGameplayCookedAsset& cooked);
};
}
