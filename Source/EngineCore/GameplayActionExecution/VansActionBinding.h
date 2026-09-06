#pragma once

#include "VansActionExecution.h"

#include <string>
#include <string_view>

namespace Vans
{
enum class VansActionBindingValueType : std::uint8_t
{
	Any,
	Bool,
	Int,
	Float,
	String,
	Object,
	Array,
	Entity,
	TargetData,
	Resource
};

enum class VansActionBindingSource : std::uint8_t
{
	Literal,
	Context,
	Variable
};

enum class VansActionBindingTarget : std::uint8_t
{
	Variable
};

struct VansActionInputBinding
{
	VansActionBindingSource source = VansActionBindingSource::Literal;
	VansActionBindingValueType type = VansActionBindingValueType::Any;
	std::string name;
	std::vector<std::string> path;
	VansSerializedValue literal;
};

struct VansActionOutputBinding
{
	VansActionBindingTarget target = VansActionBindingTarget::Variable;
	VansActionBindingValueType type = VansActionBindingValueType::Any;
	std::string name;
};

bool VansDecodeActionInputBinding(
	const VansSerializedValue& source,
	VansActionInputBinding& binding,
	std::string& error);
bool VansDecodeActionOutputBinding(
	const VansSerializedValue& source,
	VansActionOutputBinding& binding,
	std::string& error);
bool VansResolveActionInputBinding(
	const VansActionInputBinding& binding,
	const VansActionExecutionContext& context,
	VansSerializedValue& value,
	std::string& error);
bool VansWriteActionOutputBinding(
	const VansActionOutputBinding& binding,
	VansSerializedValue value,
	VansActionExecutionContext& context,
	std::string& error);
bool VansActionBindingValueMatches(
	VansActionBindingValueType type,
	const VansSerializedValue& value);
}
