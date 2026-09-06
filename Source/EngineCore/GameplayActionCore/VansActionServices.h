#pragma once

#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansActionCommandValueKind : std::uint8_t
{
	Bool,
	Int,
	Float,
	String,
	Object,
	Array
};

enum class VansActionCommandResourcePolicy : std::uint8_t
{
	None,
	Create,
	Update,
	Release
};

struct VansActionCommandFieldSchema
{
	std::string name;
	VansActionCommandValueKind kind = VansActionCommandValueKind::String;
	bool required = false;
	VansSerializedValue defaultValue;
	bool hasMinimum = false;
	bool hasMaximum = false;
	double minimum = 0.0;
	double maximum = 0.0;
};

struct VansActionCommandSchema
{
	VansActionFieldId command;
	std::string stableName;
	VansActionCommandResourcePolicy resourcePolicy = VansActionCommandResourcePolicy::None;
	bool allowUnknownFields = false;
	std::vector<VansActionCommandFieldSchema> fields;
};

struct VansActionServiceCapability
{
	VansActionServiceId service;
	std::string stableName;
	std::vector<VansActionCommandSchema> commandSchemas;
};

struct VansActionCommand
{
	VansActionServiceId service;
	VansActionFieldId command;
	std::string stableName;
	VansActionHandle action;
	VansActionContext context;
	VansSerializedValue payload = VansSerializedValue::Object({});
};

struct VansActionCommandResult
{
	VansActionError error = VansActionError::None;
	VansGenerationHandle resource;
	VansSerializedValue payload = VansSerializedValue::Object({});
	std::string message;
	std::string reasonCode;

	explicit operator bool() const { return error == VansActionError::None; }
	std::string_view StableReasonCode() const
	{
		return reasonCode.empty() ? std::string_view(VansActionDefaultReasonCode(error))
			: std::string_view(reasonCode);
	}
};

class IVansActionService
{
public:
	virtual ~IVansActionService() = default;
	virtual const VansActionServiceCapability& Capability() const = 0;
	virtual VansActionCommandResult Execute(const VansActionCommand& command) = 0;
	virtual bool Release(VansGenerationHandle resource, std::string& error) = 0;
	virtual void Tick(double) {}
};

class VansActionServiceRegistry
{
public:
	bool Register(std::shared_ptr<IVansActionService> service, std::string& error);
	bool Seal(std::string& error);
	std::shared_ptr<IVansActionService> Resolve(VansActionServiceId service) const;
	bool ValidateRequired(const std::vector<VansActionServiceId>& required, std::string& error) const;
	const VansActionCommandSchema* ResolveCommandSchema(
		VansActionServiceId service, VansActionFieldId command) const;
	VansActionCommandResult Execute(const VansActionCommand& command) const;
	void Tick(double deltaSeconds) const;
	bool IsSealed() const { return m_Sealed; }

private:
	bool m_Sealed = false;
	std::unordered_map<VansActionServiceId, std::shared_ptr<IVansActionService>> m_Services;
	std::vector<std::shared_ptr<IVansActionService>> m_TickOrder;
};
}
