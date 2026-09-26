#pragma once

#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"
#include "../RuntimeCore/VansGenerationPool.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Vans
{
struct VansTargetLocation { std::array<double, 3> value{}; };
struct VansTargetRay
{
	std::array<double, 3> origin{};
	std::array<double, 3> direction{};
	double length = 0.0;
};
struct VansTargetHitResult
{
	VansEntityHandle entity;
	std::array<double, 3> position{};
	std::array<double, 3> normal{};
	double distance = 0.0;
	VansGameplayTagId surface;
	VansEntityHandle hitEntity;
	std::string componentGuid;
	std::string region;
};
using VansTargetDataValue = std::variant<
	VansEntityHandle,
	VansTargetLocation,
	VansTargetRay,
	VansTargetHitResult>;

struct VansTargetData
{
	std::vector<VansTargetDataValue> values;
};

struct VansTargetDataValidationPolicy
{
	std::size_t maximumTargets = 64;
	double maximumCoordinateMagnitude = 10000000.0;
	double maximumDistance = 1000000.0;
	std::function<bool(VansEntityHandle)> entityAllowed;
};

VansSerializedValue VansEncodeTargetData(const VansTargetData& data);
bool VansDecodeTargetData(const VansSerializedValue& value,
	VansTargetData& data,
	std::string& error,
	const VansTargetDataValidationPolicy& policy = {});

class VansTargetDataStore
{
public:
	VansTargetDataHandle Store(VansTargetData data) { return { m_Data.Emplace(std::move(data)) }; }
	VansTargetData* Resolve(VansTargetDataHandle handle) { return m_Data.Resolve(handle.value); }
	const VansTargetData* Resolve(VansTargetDataHandle handle) const { return m_Data.Resolve(handle.value); }
	bool Release(VansTargetDataHandle handle) { return m_Data.Release(handle.value); }
	void Clear() { m_Data.Clear(); }

private:
	VansGenerationPool<VansTargetData> m_Data;
};

struct VansTargetingStep
{
	VansActionGraphNodeTypeId handler;
	VansSerializedValue inputs = VansSerializedValue::Object({});
};

struct VansTargetingPolicy
{
	VansTargetingPolicyId id;
	std::string name;
	std::vector<VansTargetingStep> steps;
};

class VansTargetingPolicyRegistry
{
public:
	bool Register(VansTargetingPolicy policy, std::string& error);
	bool Seal(std::string& error);
	const VansTargetingPolicy* Resolve(VansTargetingPolicyId id) const;
	bool IsSealed() const { return m_Sealed; }

private:
	bool m_Sealed = false;
	std::unordered_map<VansTargetingPolicyId, VansTargetingPolicy> m_Policies;
};

struct VansTargetingTraceEntry
{
	std::string step;
	std::size_t inputCount = 0;
	std::size_t outputCount = 0;
	bool succeeded = false;
	std::string message;
};

struct VansTargetingResult
{
	VansActionError error = VansActionError::None;
	VansTargetData data;
	std::vector<VansTargetingTraceEntry> trace;
	std::string message;

	explicit operator bool() const { return error == VansActionError::None; }
};

struct VansTargetingInputField
{
	std::string name;
	std::string valueType;
	bool required = false;
	VansSerializedValue defaultValue;
};

class IVansTargetingStepHandler
{
public:
	virtual ~IVansTargetingStepHandler() = default;
	virtual VansActionGraphNodeTypeId TypeId() const = 0;
	virtual std::string_view StableName() const = 0;
	virtual bool BeginsPipeline() const { return false; }
	virtual std::vector<VansTargetingInputField> InputFields() const { return {}; }
	virtual bool Execute(
		const VansTargetingStep& step,
		const VansActionContext& context,
		std::vector<VansTargetDataValue>& values,
		std::string& message) const = 0;
};

struct VansTargetingStepDescriptor
{
	VansActionGraphNodeTypeId type;
	std::string stableName;
	bool beginsPipeline = false;
	std::vector<VansTargetingInputField> inputFields;
};

class VansTargetingHandlerRegistry
{
public:
	bool Register(std::shared_ptr<const IVansTargetingStepHandler> handler, std::string& error);
	bool Seal(std::string& error);
	std::shared_ptr<const IVansTargetingStepHandler> Resolve(VansActionGraphNodeTypeId type) const;
	std::shared_ptr<const IVansTargetingStepHandler> Find(std::string_view stableName) const;
	std::vector<VansTargetingStepDescriptor> Snapshot() const;
	bool IsSealed() const { return m_Sealed; }

private:
	bool m_Sealed = false;
	std::unordered_map<VansActionGraphNodeTypeId, std::shared_ptr<const IVansTargetingStepHandler>> m_Handlers;
	std::unordered_map<std::string, VansActionGraphNodeTypeId> m_ByName;
};

class VansTargetingPipeline
{
public:
	static VansTargetingResult Execute(
		const VansTargetingPolicy& policy,
		const VansActionContext& context,
		const VansTargetingHandlerRegistry& handlers,
		VansTargetData initial = {});
};

bool VansRegisterBuiltInTargetingHandlers(
	VansTargetingHandlerRegistry& registry,
	std::string& error);
bool VansBuildBuiltInTargetingHandlerRegistry(
	VansTargetingHandlerRegistry& registry,
	std::string& error);
}
