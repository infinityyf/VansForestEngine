#pragma once

#include "VansUIRuntimeHandles.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace VansRuntime
{
	struct VansUIVariant;

	using VansUIVariantArray = std::vector<VansUIVariant>;
	using VansUIVariantMap = std::unordered_map<std::string, VansUIVariant>;

	struct VansUIVariant
	{
		using Value = std::variant<
			std::monostate,
			bool,
			std::int64_t,
			double,
			std::string,
			VansUIVariantArray,
			VansUIVariantMap,
			VansUIHandleId>;

		Value value;

		VansUIVariant() = default;
		VansUIVariant(bool v) : value(v) {}
		VansUIVariant(std::int64_t v) : value(v) {}
		VansUIVariant(double v) : value(v) {}
		VansUIVariant(std::string v) : value(std::move(v)) {}
		VansUIVariant(const char* v) : value(std::string(v ? v : "")) {}
		VansUIVariant(VansUIVariantArray v) : value(std::move(v)) {}
		VansUIVariant(VansUIVariantMap v) : value(std::move(v)) {}
		VansUIVariant(VansUIHandleId v) : value(v) {}
	};
}
