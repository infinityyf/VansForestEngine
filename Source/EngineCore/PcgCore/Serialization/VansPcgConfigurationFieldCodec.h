#pragma once

#include "VansPcgValueCodec.h"
#include "../VansPcgConfigurationField.h"

#include <type_traits>

namespace Vans
{
template <typename Owner, std::size_t Count>
void ReadPcgConfigurationFields(
	PcgValue::Reader& reader,
	Owner& owner,
	const std::array<VansPcgConfigurationFieldDescriptor<Owner>, Count>& descriptors,
	bool tree)
{
	for (const auto& descriptor : descriptors)
	{
		if (!IsPcgConfigurationFieldPresent(descriptor, tree)) continue;
		std::visit([&](auto member) {
			using Member = decltype(member);
			if constexpr (std::is_same_v<Member, float Owner::*>) reader.Float(descriptor.name, owner.*member);
			else if constexpr (std::is_same_v<Member, std::uint32_t Owner::*>) reader.IntegerField(descriptor.name, owner.*member);
			else if constexpr (std::is_same_v<Member, bool Owner::*>) reader.Bool(descriptor.name, owner.*member);
			else if constexpr (std::is_same_v<Member, std::vector<float> Owner::*>)
				reader.FloatVector(descriptor.name, owner.*member, descriptor.minimumCount, descriptor.maximumCount);
			else reader.Vector(descriptor.name, owner.*member);
		}, descriptor.member);
	}
}

template <typename Owner, std::size_t Count>
void WritePcgConfigurationFields(
	std::vector<std::pair<std::string, VansSerializedValue>>& fields,
	const Owner& owner,
	const std::array<VansPcgConfigurationFieldDescriptor<Owner>, Count>& descriptors,
	bool tree)
{
	for (const auto& descriptor : descriptors)
	{
		if (!IsPcgConfigurationFieldPresent(descriptor, tree)) continue;
		std::visit([&](auto member) {
			using Member = decltype(member);
			if constexpr (std::is_same_v<Member, float Owner::*>)
				fields.emplace_back(descriptor.name, VansSerializedValue::Float(owner.*member));
			else if constexpr (std::is_same_v<Member, std::uint32_t Owner::*>)
				fields.emplace_back(descriptor.name, VansSerializedValue::Int(owner.*member));
			else if constexpr (std::is_same_v<Member, bool Owner::*>)
				fields.emplace_back(descriptor.name, VansSerializedValue::Bool(owner.*member));
			else if constexpr (std::is_same_v<Member, std::vector<float> Owner::*>)
				fields.emplace_back(descriptor.name, PcgValue::FloatVector(owner.*member));
			else fields.emplace_back(descriptor.name, PcgValue::Vector(owner.*member));
		}, descriptor.member);
	}
}
}
