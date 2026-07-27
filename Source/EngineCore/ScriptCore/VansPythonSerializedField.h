#pragma once

#include "../AssetCore/Serialization/VansSerializedObjectReference.h"

#include <cstdint>
#include <string>

enum class VansPythonSerializedFieldType : uint8_t
{
	Null,
	Bool,
	Int,
	Float,
	String,
	ObjectReference
};

using VansPythonSerializedObjectReference = Vans::SerializedObjectReferenceValue;

struct VansPythonSerializedFieldValue
{
	VansPythonSerializedFieldType type = VansPythonSerializedFieldType::Null;
	bool boolValue = false;
	std::int64_t intValue = 0;
	double floatValue = 0.0;
	std::string stringValue;
	VansPythonSerializedObjectReference objectReference;
};
