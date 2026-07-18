#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace VansGraphics
{
class VansScene;

class VansMaterialLiveEditService
{
public:
	using Json = nlohmann::ordered_json;

	bool ApplyMaterialAssetPatch(
		VansScene* scene,
		const std::filesystem::path& assetPath,
		const Json& assetRoot,
		const std::string& changedPointer = {});
	bool ApplyMaterialParameter(
		VansScene* scene,
		const std::string& materialName,
		const std::string& parameterPath,
		const Json& value);
};
}
