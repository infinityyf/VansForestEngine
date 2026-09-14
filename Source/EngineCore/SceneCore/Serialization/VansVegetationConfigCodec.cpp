#include "VansVegetationConfigCodec.h"
#include "../../PcgCore/Serialization/VansPcgRecipeCodec.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

namespace Vans
{
bool VansVegetationConfigCodec::Decode(const VansSerializedValue& root, VansVegetationConfigAsset& asset, std::string& error)
{
	return VansPcgRecipeCodec::Decode(root, asset.config, error);
}

bool VansVegetationConfigCodec::Encode(const VansPcgRecipeAsset& config, VansSerializedValue& root, std::string& error)
{
	return VansPcgRecipeCodec::Encode(config, root, error);
}

std::vector<std::string> VansVegetationConfigCodec::Validate(const VansPcgRecipeAsset& config)
{
	return ValidatePcgRecipe(config, false);
}

std::string VansVegetationConfigCodec::ReadReferenceGuid(const VansSerializedValue& reference)
{
	if (reference.kind != VansSerializedValue::Kind::Object || reference.objectFields.size() != 1) return {};
	const auto* asset = FindObjectField(reference, "asset");
	if (!asset || asset->kind != VansSerializedValue::Kind::Object || asset->objectFields.size() != 1) return {};
	const auto text = ReadSerializedStringField(*asset, "guid");
	VansAssetGuid guid;
	return VansAssetGuid::TryParse(text, guid) && guid.IsValid() ? text : std::string{};
}

bool VansVegetationConfigCodec::ResolveReference(const VansSerializedValue& reference,
	const VansVegetationConfigAsset& asset, VansPcgRecipeAsset& config, std::string& error)
{
	error.clear();
	if (ReadReferenceGuid(reference).empty())
	{
		error = "Scene vegetation requires only asset.guid; distribution edits belong to its region/layer document";
		return false;
	}
	const auto diagnostics = ValidatePcgRecipe(asset.config, false);
	if (!diagnostics.empty()) { error = diagnostics.front(); return false; }
	config = asset.config;
	return true;
}
}
