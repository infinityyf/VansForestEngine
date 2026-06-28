#pragma once

#include "../RenderCore/VansScene.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace VansGraphics
{
class VansMaterialLiveEditService
{
public:
	using Json = nlohmann::ordered_json;

	void Bind(VansScene* scene) { m_Scene = scene; }

	bool ApplyMaterialAssetPatch(const std::filesystem::path& assetPath, const Json& assetRoot);
	bool ApplyMaterialParameter(const std::string& materialName, const std::string& parameterPath, const Json& value);

private:
	VansScene* m_Scene = nullptr;
};
}
