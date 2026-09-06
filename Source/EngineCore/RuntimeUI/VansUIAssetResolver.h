#pragma once

#include "Serialization/VansUIAssetDocument.h"
#include "../AssetCore/VansAssetDatabase.h"

#include <memory>
#include <string>

namespace VansRuntime
{
	struct VansUIXamlAsset
	{
		std::string bytes;
	};

	class VansUIAssetResolver
	{
	public:
		static bool ResolveDocument(
			const std::string& assetGuid,
			Vans::VansAssetType expectedType,
			std::shared_ptr<const VansUIAssetDocument>& document,
			std::string& error);

		static bool ResolveXamlUri(
			const std::string& assetGuid,
			std::string& uri,
			std::string& error);
	};
}
