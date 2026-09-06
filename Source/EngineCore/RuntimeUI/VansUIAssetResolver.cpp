#include "VansUIAssetResolver.h"

#include "../ProjectSystem/VansProjectManager.h"

namespace VansRuntime
{
	bool VansUIAssetResolver::ResolveDocument(
		const std::string& assetGuid,
		Vans::VansAssetType expectedType,
		std::shared_ptr<const VansUIAssetDocument>& document,
		std::string& error)
	{
		document.reset();
		Vans::VansAssetGuid guid;
		if (!Vans::VansAssetGuid::TryParse(assetGuid, guid))
		{
			error = "UI asset reference is not a valid GUID: " + assetGuid;
			return false;
		}

		auto& projectManager = Vans::VansProjectManager::Get();
		const std::optional<Vans::VansAssetRecord> record = projectManager.FindAssetRecord(guid);
		if (!record)
		{
			error = "UI asset is not indexed: " + assetGuid;
			return false;
		}
		if (record->type != expectedType)
		{
			error = "UI asset type does not match reference: " + assetGuid;
			return false;
		}

		document = projectManager.GetAssetObjectRepository().ResolveLatest<VansUIAssetDocument>(guid);
		if (!document)
		{
			error = "UI asset has no published memory snapshot: " + assetGuid;
			return false;
		}
		return true;
	}

	bool VansUIAssetResolver::ResolveXamlUri(
		const std::string& assetGuid,
		std::string& uri,
		std::string& error)
	{
		uri.clear();
		Vans::VansAssetGuid guid;
		if (!Vans::VansAssetGuid::TryParse(assetGuid, guid))
		{
			error = "UI XAML reference is not a valid GUID: " + assetGuid;
			return false;
		}

		auto& projectManager = Vans::VansProjectManager::Get();
		const std::optional<Vans::VansAssetRecord> record = projectManager.FindAssetRecord(guid);
		if (!record || record->type != Vans::VansAssetType::UIXaml)
		{
			error = "UI XAML asset is not indexed with the expected type: " + assetGuid;
			return false;
		}
		if (!projectManager.GetAssetObjectRepository().ResolveLatest<VansUIXamlAsset>(guid))
		{
			error = "UI XAML asset has no published memory snapshot: " + assetGuid;
			return false;
		}

		uri = "asset://" + guid.ToString();
		return true;
	}
}
