#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace Vans
{
enum class VansPcgConfigurationFieldPersistence
{
	Always,
	TreeOnly
};

enum class VansPcgConfigurationFieldVisibility
{
	Always,
	DensitySourceOnly
};

template <typename Owner>
using VansPcgConfigurationMember = std::variant<
	float Owner::*,
	std::uint32_t Owner::*,
	bool Owner::*,
	std::array<float, 2> Owner::*,
	std::array<float, 3> Owner::*,
	std::vector<float> Owner::*>;

// One table owns the persisted name, typed member, editor presentation and validation bounds.
// Bounds describe the accepted domain. editorConstrained remains false for the existing free-drag UI contract.
template <typename Owner>
struct VansPcgConfigurationFieldDescriptor
{
	const char* name = "";
	const char* label = "";
	const char* treeLabel = "";
	VansPcgConfigurationMember<Owner> member;
	float editorSpeed = 0.01f;
	float minimum = 0;
	float maximum = 0;
	bool hasMinimum = false;
	bool hasMaximum = false;
	bool editorConstrained = false;
	std::size_t minimumCount = 0;
	std::size_t maximumCount = 0;
	std::uint32_t editorOrder = 0;
	VansPcgConfigurationFieldPersistence persistence = VansPcgConfigurationFieldPersistence::Always;
	VansPcgConfigurationFieldVisibility visibility = VansPcgConfigurationFieldVisibility::Always;
};

template <typename Owner>
bool IsPcgConfigurationFieldPresent(
	const VansPcgConfigurationFieldDescriptor<Owner>& field,
	bool tree)
{
	return field.persistence != VansPcgConfigurationFieldPersistence::TreeOnly || tree;
}
}
