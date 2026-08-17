#pragma once

#include "VansGameplayAssetSchema.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansGameplayMigrationRecord
{
	VansAssetType assetType = VansAssetType::Unknown;
	std::uint32_t fromVersion = 0;
	std::uint32_t toVersion = 0;
	std::string name;
};

using VansGameplayMigrationCallback = std::function<bool(VansSerializedValue&, std::string&)>;

class VansGameplayAssetMigrationRegistry
{
public:
	bool Register(
		VansAssetType assetType,
		std::uint32_t fromVersion,
		std::string name,
		VansGameplayMigrationCallback callback,
		std::string& error);
	bool Seal(std::string& error);
	bool Migrate(
		VansAssetType assetType,
		std::uint32_t targetVersion,
		VansSerializedValue& document,
		std::vector<VansGameplayMigrationRecord>& report,
		std::string& error) const;
	bool IsSealed() const { return m_Sealed; }
	static const VansGameplayAssetMigrationRegistry& BuiltIns();

private:
	struct Step
	{
		std::string name;
		VansGameplayMigrationCallback callback;
	};

	bool m_Sealed = false;
	std::unordered_map<VansAssetType, std::unordered_map<std::uint32_t, Step>> m_Steps;
};
}
