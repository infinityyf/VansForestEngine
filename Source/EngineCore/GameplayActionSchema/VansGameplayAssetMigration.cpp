#include "VansGameplayAssetMigration.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

namespace Vans
{
bool VansGameplayAssetMigrationRegistry::Register(
	VansAssetType assetType,
	std::uint32_t fromVersion,
	std::string name,
	VansGameplayMigrationCallback callback,
	std::string& error)
{
	error.clear();
	if (m_Sealed)
	{
		error = "Gameplay Asset Migration registry is sealed";
		return false;
	}
	if (!VansGameplayAssetSchemaRegistry::IsGameplayAssetType(assetType) || fromVersion == 0 ||
		name.empty() || !callback)
	{
		error = "Gameplay Asset Migration registration is incomplete";
		return false;
	}
	if (!m_Steps[assetType].emplace(fromVersion, Step{ std::move(name), std::move(callback) }).second)
	{
		error = "Gameplay Asset Migration step is already registered";
		return false;
	}
	return true;
}

bool VansGameplayAssetMigrationRegistry::Seal(std::string& error)
{
	error.clear();
	if (m_Sealed) return true;
	for (const auto& assetSteps : m_Steps)
	{
		for (const auto& versionStep : assetSteps.second)
		{
			if (versionStep.first == UINT32_MAX)
			{
				error = "Gameplay Asset Migration version cannot overflow";
				return false;
			}
		}
	}
	m_Sealed = true;
	return true;
}

bool VansGameplayAssetMigrationRegistry::Migrate(
	VansAssetType assetType,
	std::uint32_t targetVersion,
	VansSerializedValue& document,
	std::vector<VansGameplayMigrationRecord>& report,
	std::string& error) const
{
	error.clear();
	report.clear();
	if (!m_Sealed || targetVersion == 0 || document.kind != VansSerializedValue::Kind::Object)
	{
		error = "Gameplay Asset Migration request is invalid";
		return false;
	}
	const VansSerializedValue* versionValue = FindObjectField(document, "schemaVersion");
	if (!versionValue || versionValue->kind != VansSerializedValue::Kind::Int || versionValue->intValue <= 0)
	{
		error = "Gameplay asset schemaVersion is missing or invalid";
		return false;
	}
	std::uint32_t version = static_cast<std::uint32_t>(versionValue->intValue);
	if (version > targetVersion)
	{
		error = "Gameplay asset schemaVersion is newer than this runtime";
		return false;
	}
	while (version < targetVersion)
	{
		const auto assetFound = m_Steps.find(assetType);
		const auto stepFound = assetFound == m_Steps.end()
			? std::unordered_map<std::uint32_t, Step>::const_iterator{}
			: assetFound->second.find(version);
		if (assetFound == m_Steps.end() || stepFound == assetFound->second.end())
		{
			error = "Gameplay asset migration path is incomplete at version " + std::to_string(version);
			return false;
		}
		VansSerializedValue migrated = document;
		if (!stepFound->second.callback(migrated, error))
		{
			if (error.empty()) error = "Gameplay asset migration step failed";
			return false;
		}
		SetSerializedObjectField(migrated, "schemaVersion",
			VansSerializedValue::Int(static_cast<std::int64_t>(version + 1)));
		document = std::move(migrated);
		report.push_back({ assetType, version, version + 1, stepFound->second.name });
		++version;
	}
	return true;
}

const VansGameplayAssetMigrationRegistry& VansGameplayAssetMigrationRegistry::BuiltIns()
{
	static const VansGameplayAssetMigrationRegistry registry = []
	{
		VansGameplayAssetMigrationRegistry value;
		std::string error;
		value.Seal(error);
		return value;
	}();
	return registry;
}
}
