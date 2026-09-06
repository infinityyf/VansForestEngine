#pragma once

#include "VansAssetDatabase.h"

#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Vans
{
inline std::uint64_t AssetObjectContentHash(const VansAssetRecord& record)
{
	std::uint64_t hash = record.sourceHash;
	hash ^= record.metaHash + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
	return hash == 0 ? 1 : hash;
}

template <typename Asset>
struct VansAssetObjectHandle
{
	VansAssetGuid guid;
	std::uint64_t generation = 0;

	bool IsValid() const { return guid.IsValid() && generation != 0; }
};

struct VansAssetObjectSnapshotInfo
{
	VansAssetGuid guid;
	VansAssetType assetType = VansAssetType::Unknown;
	std::uint64_t generation = 0;
	std::uint64_t contentHash = 0;
	std::vector<VansAssetGuid> dependencies;

	bool IsValid() const
	{
		return guid.IsValid() && assetType != VansAssetType::Unknown && generation != 0;
	}

};

class VansAssetObjectRepository
{
public:
	template <typename Asset>
	VansAssetObjectHandle<Asset> Publish(
		VansAssetGuid guid,
		VansAssetType assetType,
		std::uint64_t contentHash,
		std::shared_ptr<const Asset> object,
		std::vector<VansAssetGuid> dependencies,
		std::string& error)
	{
		const std::uint64_t generation = PublishErased(
			guid,
			assetType,
			contentHash,
			std::type_index(typeid(Asset)),
			std::static_pointer_cast<const void>(std::move(object)),
			std::move(dependencies),
			error);
		return { guid, generation };
	}

	template <typename Asset>
	VansAssetObjectHandle<Asset> PublishView(
		VansAssetGuid guid,
		VansAssetType assetType,
		std::uint64_t contentHash,
		std::shared_ptr<const Asset> object,
		std::string& error)
	{
		const std::uint64_t generation = PublishErasedView(
			guid,
			assetType,
			contentHash,
			std::type_index(typeid(Asset)),
			std::static_pointer_cast<const void>(std::move(object)),
			error);
		return { guid, generation };
	}

	template <typename Asset>
	std::shared_ptr<const Asset> Resolve(VansAssetObjectHandle<Asset> handle) const
	{
		std::shared_ptr<const void> object = ResolveErased(
			handle.guid, handle.generation, std::type_index(typeid(Asset)));
		if (!object)
			return {};
		const Asset* typedObject = static_cast<const Asset*>(object.get());
		return std::shared_ptr<const Asset>(std::move(object), typedObject);
	}

	template <typename Asset>
	std::shared_ptr<const Asset> ResolveLatest(
		VansAssetGuid guid,
		VansAssetObjectHandle<Asset>* outHandle = nullptr) const
	{
		std::uint64_t generation = 0;
		std::shared_ptr<const void> object = ResolveLatestErased(
			guid, std::type_index(typeid(Asset)), generation);
		if (!object)
			return {};
		if (outHandle)
			*outHandle = { guid, generation };
		const Asset* typedObject = static_cast<const Asset*>(object.get());
		return std::shared_ptr<const Asset>(std::move(object), typedObject);
	}

	bool FindInfo(VansAssetGuid guid, VansAssetObjectSnapshotInfo& outInfo) const;
	bool Remove(VansAssetGuid guid);
	void Clear();
	std::size_t Size() const;

private:
	struct Entry
	{
		VansAssetType assetType = VansAssetType::Unknown;
		std::uint64_t generation = 0;
		std::uint64_t contentHash = 0;
		std::type_index objectType{ typeid(void) };
		std::shared_ptr<const void> object;
		std::unordered_map<std::type_index, std::shared_ptr<const void>> views;
		std::vector<VansAssetGuid> dependencies;
	};

	std::uint64_t PublishErased(
		VansAssetGuid guid,
		VansAssetType assetType,
		std::uint64_t contentHash,
		std::type_index objectType,
		std::shared_ptr<const void> object,
		std::vector<VansAssetGuid> dependencies,
		std::string& error);
	std::uint64_t PublishErasedView(
		VansAssetGuid guid,
		VansAssetType assetType,
		std::uint64_t contentHash,
		std::type_index objectType,
		std::shared_ptr<const void> object,
		std::string& error);
	std::shared_ptr<const void> ResolveErased(
		VansAssetGuid guid,
		std::uint64_t generation,
		std::type_index objectType) const;
	std::shared_ptr<const void> ResolveLatestErased(
		VansAssetGuid guid,
		std::type_index objectType,
		std::uint64_t& outGeneration) const;

	mutable std::shared_mutex m_Mutex;
	std::unordered_map<VansAssetGuid, Entry> m_Entries;
};
}
