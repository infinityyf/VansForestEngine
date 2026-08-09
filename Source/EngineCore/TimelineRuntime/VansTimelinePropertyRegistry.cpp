#include "VansTimelinePropertyRegistry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace Vans
{
namespace
{
template <typename Vector>
Vector BlendVector(const Vector& current, const Vector& sampled, VansTimelineBlendMode mode)
{
	Vector result = sampled;
	for (std::size_t index = 0; index < result.value.size(); ++index)
	{
		if (mode == VansTimelineBlendMode::Additive || mode == VansTimelineBlendMode::Relative)
			result.value[index] = current.value[index] + sampled.value[index];
		else if (mode == VansTimelineBlendMode::Multiply)
			result.value[index] = current.value[index] * sampled.value[index];
	}
	return result;
}

VansTimelineQuaternion MultiplyQuaternion(
	const VansTimelineQuaternion& left,
	const VansTimelineQuaternion& right)
{
	const double lx = left.value[0], ly = left.value[1], lz = left.value[2], lw = left.value[3];
	const double rx = right.value[0], ry = right.value[1], rz = right.value[2], rw = right.value[3];
	VansTimelineQuaternion result{ {
		lw * rx + lx * rw + ly * rz - lz * ry,
		lw * ry - lx * rz + ly * rw + lz * rx,
		lw * rz + lx * ry - ly * rx + lz * rw,
		lw * rw - lx * rx - ly * ry - lz * rz } };
	const double length = std::sqrt(result.value[0] * result.value[0] + result.value[1] * result.value[1] +
		result.value[2] * result.value[2] + result.value[3] * result.value[3]);
	if (length > 0.0000001)
		for (double& component : result.value) component /= length;
	return result;
}

bool BlendValue(
	const VansTimelineKeyValue& current,
	const VansTimelineKeyValue& sampled,
	VansTimelineBlendMode mode,
	VansTimelineKeyValue& result,
	std::string& error)
{
	if (mode == VansTimelineBlendMode::Override)
	{
		result = sampled;
		return true;
	}
	if (current.index() != sampled.index())
	{
		error = "Property blend requires matching registered runtime value types";
		return false;
	}
	return std::visit([&](const auto& currentValue) -> bool
	{
		using T = std::decay_t<decltype(currentValue)>;
		const T* sampledValue = std::get_if<T>(&sampled);
		if (!sampledValue) return false;
		if constexpr (std::is_same_v<T, std::int32_t>)
		{
			const std::int64_t value = mode == VansTimelineBlendMode::Multiply
				? static_cast<std::int64_t>(currentValue) * *sampledValue
				: static_cast<std::int64_t>(currentValue) + *sampledValue;
			result = static_cast<std::int32_t>(std::clamp<std::int64_t>(value,
				std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
			return true;
		}
		else if constexpr (std::is_same_v<T, std::int64_t>)
		{
			result = mode == VansTimelineBlendMode::Multiply ? currentValue * *sampledValue : currentValue + *sampledValue;
			return true;
		}
		else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
		{
			result = mode == VansTimelineBlendMode::Multiply ? currentValue * *sampledValue : currentValue + *sampledValue;
			return true;
		}
		else if constexpr (std::is_same_v<T, VansTimelineVec2> || std::is_same_v<T, VansTimelineVec3> ||
			std::is_same_v<T, VansTimelineVec4> || std::is_same_v<T, VansTimelineColorLinear> ||
			std::is_same_v<T, VansTimelineColorSrgb>)
		{
			result = BlendVector(currentValue, *sampledValue, mode);
			return true;
		}
		else if constexpr (std::is_same_v<T, VansTimelineQuaternion>)
		{
			result = MultiplyQuaternion(currentValue, *sampledValue);
			return true;
		}
		else
		{
			error = "The registered Property value type only supports Override blending";
			return false;
		}
	}, current);
}
}

bool VansTimelinePropertyRegistry::Register(
	VansTimelineRuntimePropertyDescriptor descriptor,
	std::string& error)
{
	error.clear();
	if (descriptor.descriptorId.empty() || descriptor.componentTypeId == 0 ||
		!descriptor.read || !descriptor.write)
	{
		error = "Runtime Property descriptor requires stable ID, component type, reader and writer";
		return false;
	}
	if (!m_Descriptors.emplace(descriptor.descriptorId, std::move(descriptor)).second)
	{
		error = "Duplicate runtime Property descriptor ID";
		return false;
	}
	return true;
}

const VansTimelineRuntimePropertyDescriptor* VansTimelinePropertyRegistry::Find(
	const std::string& descriptorId) const
{
	const auto found = m_Descriptors.find(descriptorId);
	return found == m_Descriptors.end() ? nullptr : &found->second;
}

bool VansTimelinePropertyRegistry::Apply(
	VansTimelineBlendMode blendMode,
	const VansResolvedTimelineTarget& target,
	const VansTimelinePropertyOutput& output,
	VansTimelineRestoreCallback& restore,
	std::string& error) const
{
	const VansTimelineRuntimePropertyDescriptor* descriptor = Find(output.descriptorId);
	if (!descriptor)
	{
		error = "Property descriptor has no registered runtime setter: " + output.descriptorId;
		return false;
	}
	if (descriptor->componentTypeId != output.componentTypeId)
	{
		error = "Property descriptor component type does not match the authored track";
		return false;
	}
	if (descriptor->valueType != output.valueType)
	{
		error = "Property descriptor value type does not match the authored track";
		return false;
	}
	VansTimelineKeyValue previous;
	if (!descriptor->read(target, previous, error)) return false;
	VansTimelineKeyValue blended;
	if (!BlendValue(previous, output.value, blendMode, blended, error)) return false;
	if (!descriptor->write(target, blended, error)) return false;
	restore = [descriptor, target, previous]
	{
		std::string ignored;
		descriptor->write(target, previous, ignored);
	};
	return true;
}
}
