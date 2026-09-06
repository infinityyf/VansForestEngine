#pragma once

#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansCompiledActionGraph;
enum class VansActionConcurrencyPolicy : std::uint8_t
{
	Allow,
	RejectNew,
	CancelExisting,
	QueueNew
};

struct VansActionVariableDefinition
{
	VansActionFieldId id;
	std::string name;
	VansSerializedValue defaultValue;
};

struct VansCompiledActionRecord
{
	std::string type;
	VansSerializedValue inputs = VansSerializedValue::Object({});
};

struct VansCompiledActionPhase
{
	std::vector<VansCompiledActionRecord> guards;
	std::vector<VansCompiledActionRecord> operations;
	std::vector<VansCompiledActionRecord> drivers;
};

struct VansCompiledActionMetadata
{
	std::string displayName;
	std::string category;
	std::vector<std::string> labels;
	std::int32_t priority = 0;
};

struct VansCompiledActionProgram
{
	VansCompiledActionMetadata metadata;
	VansSerializedValue contextSchema = VansSerializedValue::Array({});
	VansSerializedValue contextDefaults = VansSerializedValue::Object({});
	std::vector<VansCompiledActionRecord> policies;
	VansCompiledActionPhase activate;
	VansCompiledActionPhase commit;
	VansCompiledActionPhase execute;
	VansCompiledActionPhase finish;
	VansCompiledActionPhase cancel;
	std::vector<VansCompiledActionRecord> transitions;
	std::vector<std::string> capabilities;
	std::vector<std::string> modules;
	std::vector<VansCompiledActionRecord> extensions;
};

struct VansCompiledActionDefinition
{
	VansActionId id;
	std::string name;
	VansCompiledActionProgram program;
	std::uint64_t contentHash = 0;

	VansActionConcurrencyGroupId concurrencyGroup;
	VansActionConcurrencyPolicy concurrencyPolicy = VansActionConcurrencyPolicy::Allow;
	std::uint32_t concurrencyLimit = 1;
	double concurrencyQueueTimeoutSeconds = 0.0;
	VansActionExecutorId executor;
	std::string executionGraphAsset;
	std::shared_ptr<const VansCompiledActionGraph> executionGraph;
	std::vector<VansActionVariableDefinition> variables;

	bool cancellable = true;
	bool interruptible = true;
	std::vector<VansActionId> blockedActions;
	std::vector<VansActionId> cancelActions;
	std::vector<std::string> blockedActionReferences;
	std::vector<std::string> cancelActionReferences;

};

class VansActionDefinitionRegistry
{
public:
	bool Register(std::shared_ptr<const VansCompiledActionDefinition> definition, std::string& error);
	bool Replace(std::shared_ptr<const VansCompiledActionDefinition> definition, std::string& error);
	std::shared_ptr<const VansCompiledActionDefinition> Resolve(VansActionId id) const;
	std::size_t ActionCount() const { return m_Definitions.size(); }
	std::vector<std::shared_ptr<const VansCompiledActionDefinition>> Definitions() const;
	static VansGameplayDiagnostics Validate(const VansCompiledActionDefinition& definition);

private:
	std::unordered_map<VansActionId, std::shared_ptr<const VansCompiledActionDefinition>> m_Definitions;
};
}
