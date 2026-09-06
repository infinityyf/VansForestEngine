#include "VansAssetObjectRepository.h"

#include <limits>
#include <mutex>
#include <utility>

namespace Vans
{
namespace
{
std::uint64_t NextGeneration(std::uint64_t generation)
{
	return generation == (std::numeric_limits<std::uint64_t>::max)()
		? 1
		: generation + 1;
}
}

std::uint64_t VansAssetObjectRepository::PublishErased(
	VansAssetGuid guid,
	VansAssetType assetType,
	std::uint64_t contentHash,
	std::type_index objectType,
	std::shared_ptr<const void> object,
	std::vector<VansAssetGuid> dependencies,
	std::string& error)
{
	error.clear();
	if (!guid.IsValid())
	{
		error = "Cannot publish an asset object without a valid GUID";
		return 0;
	}
	if (assetType == VansAssetType::Unknown)
	{
		error = "Cannot publish an asset object with an unknown asset type";
		return 0;
	}
	if (!object || objectType == std::type_index(typeid(void)))
	{
		error = "Cannot publish an empty asset object";
		return 0;
	}

	std::unique_lock lock(m_Mutex);
	Entry& entry = m_Entries[guid];
	if (entry.assetType != VansAssetType::Unknown && entry.assetType != assetType)
	{
		error = "An asset GUID cannot change its registered asset type";
		return 0;
	}
	if (entry.object && entry.objectType != objectType)
	{
		error = "An asset GUID cannot change its decoded object type";
		return 0;
	}
	if (entry.object && entry.contentHash == contentHash)
		return entry.generation;

	entry.assetType = assetType;
	entry.generation = NextGeneration(entry.generation);
	entry.contentHash = contentHash;
	entry.objectType = objectType;
	entry.object = std::move(object);
	entry.views.clear();
	entry.dependencies = std::move(dependencies);
	return entry.generation;
}

std::uint64_t VansAssetObjectRepository::PublishErasedView(
	VansAssetGuid guid,
	VansAssetType assetType,
	std::uint64_t contentHash,
	std::type_index objectType,
	std::shared_ptr<const void> object,
	std::string& error)
{
	error.clear();
	if (!guid.IsValid() || assetType == VansAssetType::Unknown || !object ||
		objectType == std::type_index(typeid(void)))
	{
		error = "Cannot publish an invalid asset object view";
		return 0;
	}
	std::unique_lock lock(m_Mutex);
	const auto found = m_Entries.find(guid);
	if (found == m_Entries.end() || !found->second.object)
	{
		error = "Cannot publish an asset object view before its primary object";
		return 0;
	}
	Entry& entry = found->second;
	if (entry.assetType != assetType)
	{
		error = "Asset object view does not match the primary asset type";
		return 0;
	}
	if (entry.objectType == objectType)
	{
		if (entry.contentHash != contentHash)
		{
			error = "An asset object view cannot replace the primary object";
			return 0;
		}
		return entry.generation;
	}
	if (entry.contentHash != contentHash)
	{
		entry.contentHash = contentHash;
		entry.generation = NextGeneration(entry.generation);
	}
	entry.views[objectType] = std::move(object);
	return entry.generation;
}

std::shared_ptr<const void> VansAssetObjectRepository::ResolveErased(
	VansAssetGuid guid,
	std::uint64_t generation,
	std::type_index objectType) const
{
	if (!guid.IsValid() || generation == 0)
		return {};
	std::shared_lock lock(m_Mutex);
	const auto found = m_Entries.find(guid);
	if (found == m_Entries.end() || !found->second.object ||
		found->second.generation != generation)
		return {};
	if (found->second.objectType == objectType)
		return found->second.object;
	const auto view = found->second.views.find(objectType);
	return view == found->second.views.end() ? std::shared_ptr<const void>{} : view->second;
}

std::shared_ptr<const void> VansAssetObjectRepository::ResolveLatestErased(
	VansAssetGuid guid,
	std::type_index objectType,
	std::uint64_t& outGeneration) const
{
	outGeneration = 0;
	if (!guid.IsValid())
		return {};
	std::shared_lock lock(m_Mutex);
	const auto found = m_Entries.find(guid);
	if (found == m_Entries.end() || !found->second.object)
		return {};
	outGeneration = found->second.generation;
	if (found->second.objectType == objectType)
		return found->second.object;
	const auto view = found->second.views.find(objectType);
	if (view == found->second.views.end())
	{
		outGeneration = 0;
		return {};
	}
	return view->second;
}

bool VansAssetObjectRepository::FindInfo(
	VansAssetGuid guid,
	VansAssetObjectSnapshotInfo& outInfo) const
{
	outInfo = {};
	std::shared_lock lock(m_Mutex);
	const auto found = m_Entries.find(guid);
	if (found == m_Entries.end() || !found->second.object)
		return false;
	outInfo.guid = guid;
	outInfo.assetType = found->second.assetType;
	outInfo.generation = found->second.generation;
	outInfo.contentHash = found->second.contentHash;
	outInfo.dependencies = found->second.dependencies;
	return true;
}

bool VansAssetObjectRepository::Remove(VansAssetGuid guid)
{
	std::unique_lock lock(m_Mutex);
	const auto found = m_Entries.find(guid);
	if (found == m_Entries.end() || !found->second.object)
		return false;
	found->second.generation = NextGeneration(found->second.generation);
	found->second.contentHash = 0;
	found->second.objectType = std::type_index(typeid(void));
	found->second.object.reset();
	found->second.views.clear();
	found->second.dependencies.clear();
	return true;
}

void VansAssetObjectRepository::Clear()
{
	std::unique_lock lock(m_Mutex);
	for (auto& [guid, entry] : m_Entries)
	{
		(void)guid;
		entry.generation = NextGeneration(entry.generation);
		entry.contentHash = 0;
		entry.objectType = std::type_index(typeid(void));
		entry.object.reset();
		entry.views.clear();
		entry.dependencies.clear();
	}
}

std::size_t VansAssetObjectRepository::Size() const
{
	std::shared_lock lock(m_Mutex);
	std::size_t count = 0;
	for (const auto& [guid, entry] : m_Entries)
	{
		(void)guid;
		if (entry.object)
			++count;
	}
	return count;
}
}
