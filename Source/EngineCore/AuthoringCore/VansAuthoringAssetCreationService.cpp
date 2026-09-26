#include "VansAuthoringAssetCreationService.h"

#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../AssetCore/Storage/VansFileStorage.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"
#include "../SceneCore/VansAssetObjectBootstrapper.h"

#include <nlohmann/json.hpp>

#include <unordered_set>
#include <utility>

namespace Vans
{
namespace
{
bool IsInsideBundle(
	const std::filesystem::path& source,
	const std::filesystem::path& bundle)
{
	auto sourcePart = source.begin();
	auto bundlePart = bundle.begin();
	const auto sourceEnd = source.end();
	const auto bundleEnd = bundle.end();
	for (; bundlePart != bundleEnd; ++bundlePart, ++sourcePart)
		if (sourcePart == sourceEnd || *sourcePart != *bundlePart)
			return false;
	return sourcePart != sourceEnd;
}

class PendingBundle final
{
public:
	PendingBundle(
		std::filesystem::path directory,
		VansAssetDatabase& database,
		VansAssetObjectRepository& repository)
		: m_Directory(std::move(directory))
		, m_Database(database)
		, m_Repository(repository)
		, m_DirectoryExisted(std::filesystem::exists(m_Directory))
	{
	}

	~PendingBundle()
	{
		if (m_Keep)
			return;
		for (const auto& source : m_Sources)
		{
			if (const auto record = m_Database.Find(source))
				m_Repository.Remove(record->guid);
			m_Database.RemovePath(source);
			std::error_code ignored;
			std::filesystem::remove(source, ignored);
			std::filesystem::remove(VansAssetMeta::MetaPathFor(source), ignored);
		}
		if (!m_DirectoryExisted)
		{
			std::error_code ignored;
			std::filesystem::remove(m_Directory, ignored);
		}
	}

	void Add(std::filesystem::path source)
	{
		m_Sources.push_back(std::move(source));
	}

	const std::vector<std::filesystem::path>& Sources() const
	{
		return m_Sources;
	}

	void Commit() { m_Keep = true; }

private:
	std::filesystem::path m_Directory;
	VansAssetDatabase& m_Database;
	VansAssetObjectRepository& m_Repository;
	std::vector<std::filesystem::path> m_Sources;
	bool m_Keep = false;
	bool m_DirectoryExisted = false;
};
} // namespace

VansAuthoringAssetCreationResult VansAuthoringAssetCreationService::CreateBundle(
	VansAssetDatabase& database,
	VansAssetObjectRepository& repository,
	std::filesystem::path bundleDirectory,
	std::vector<VansAuthoringAssetCreateItem> items,
	std::string ioOperation,
	FinalizeCallback finalize,
	bool requireNewDirectory)
{
	VansAuthoringAssetCreationResult result;
	auto fail = [&](std::string message)
	{
		result.message = std::move(message);
		return result;
	};
	bundleDirectory = bundleDirectory.lexically_normal();
	if (bundleDirectory.empty() || items.empty() || ioOperation.empty())
		return fail("Authoring asset bundle definition is incomplete.");
	if (requireNewDirectory && std::filesystem::exists(bundleDirectory))
		return fail("The new asset folder already exists.");

	std::unordered_set<std::string> uniquePaths;
	for (auto& item : items)
	{
		item.sourcePath = item.sourcePath.lexically_normal();
		if (!item.guid.IsValid() || item.type == VansAssetType::Unknown
			|| !IsInsideBundle(item.sourcePath, bundleDirectory)
			|| std::filesystem::exists(item.sourcePath)
			|| std::filesystem::exists(VansAssetMeta::MetaPathFor(item.sourcePath))
			|| !uniquePaths.insert(item.sourcePath.generic_string()).second)
			return fail("Authoring asset bundle contains an invalid or duplicate item.");
	}

	PendingBundle pending(bundleDirectory, database, repository);
	VansScopedIOContext io(VansIODomain::Authoring, std::move(ioOperation), true);
	VansStagedFileTransaction transaction;
	std::string error;
	for (auto& item : items)
	{
		VansStagedFile file;
		const bool staged = item.payloadKind == VansAuthoringAssetPayloadKind::Bytes
			? VansFileStorage::StageWriteBytes(item.sourcePath, item.bytes, file, error)
			: VansJsonFileStorage::StageWrite(
				item.sourcePath,
				EncodeSerializedValueJson<nlohmann::ordered_json>(item.serializedRoot),
				file,
				error);
		if (!staged)
			return fail(error);
		transaction.Add(std::move(file));

		VansAssetMeta meta;
		meta.guid = item.guid;
		meta.importer = VansAssetDatabase::ImporterFor(item.type);
		if (item.metaSettings)
			meta.SetSerializedSettings(std::move(*item.metaSettings));
		if (!VansAssetMetaStorage::StageSave(
			VansAssetMeta::MetaPathFor(item.sourcePath), meta, file, error))
			return fail(error);
		transaction.Add(std::move(file));
		pending.Add(item.sourcePath);
	}
	if (!transaction.Publish(error))
		return fail(error);

	result.records.reserve(pending.Sources().size());
	for (const auto& source : pending.Sources())
	{
		if (!database.RegisterOrRefresh(
			source, VansAssetOperationPolicy::ReadOnly(), error))
			return fail(error);
		const auto record = database.Find(source);
		if (!record)
			return fail("Created authoring asset was not registered.");
		result.records.push_back(*record);
	}
	const VansAssetObjectBootstrapResult published =
		VansAssetObjectBootstrapper::Publish(
			result.records, repository, database.All());
	if (!published)
		return fail(published.errors.empty()
			? "Cannot publish new authoring assets."
			: published.errors.front());
	if (finalize && !finalize(error))
		return fail(error.empty()
			? "Created authoring assets could not be finalized."
			: error);

	pending.Commit();
	result.success = true;
	return result;
}
} // namespace Vans
