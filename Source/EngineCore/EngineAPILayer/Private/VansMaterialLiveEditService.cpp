#include "VansMaterialLiveEditService.h"

#include "../../AssetCore/VansAssetDatabase.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RenderCore/VansMaterial.h"
#include "../../RenderCore/VansScene.h"

#include <system_error>
#include <vector>

namespace VansGraphics
{
namespace
{
const nlohmann::ordered_json* FindMaterialParameters(const nlohmann::ordered_json& root)
{
	if (root.contains("parameters") && root["parameters"].is_object())
		return &root["parameters"];
	return root.is_object() ? &root : nullptr;
}

std::filesystem::path NormalizeAssetPath(const std::filesystem::path& path)
{
	std::error_code error;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
	if (!error)
		return normalized;
	normalized = std::filesystem::absolute(path, error);
	return error ? path.lexically_normal() : normalized.lexically_normal();
}

std::string UnescapePointerToken(std::string token)
{
	std::string result;
	result.reserve(token.size());
	for (std::size_t i = 0; i < token.size(); ++i)
	{
		if (token[i] == '~' && i + 1 < token.size())
		{
			if (token[i + 1] == '0') { result.push_back('~'); ++i; continue; }
			if (token[i + 1] == '1') { result.push_back('/'); ++i; continue; }
		}
		result.push_back(token[i]);
	}
	return result;
}

std::vector<std::string> SplitJsonPointer(const std::string& pointer)
{
	std::vector<std::string> tokens;
	if (pointer.empty() || pointer[0] != '/')
		return tokens;

	std::size_t start = 1;
	while (start <= pointer.size())
	{
		const std::size_t slash = pointer.find('/', start);
		const std::size_t end = slash == std::string::npos ? pointer.size() : slash;
		tokens.push_back(UnescapePointerToken(pointer.substr(start, end - start)));
		if (slash == std::string::npos)
			break;
		start = slash + 1;
	}
	return tokens;
}
}

bool VansMaterialLiveEditService::ApplyMaterialAssetPatch(
	VansScene* scene,
	const std::filesystem::path& assetPath,
	const Json& assetRoot,
	const std::string& changedPointer)
{
	if (!scene) return false;
	auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
	if (!database) return false;

	std::string materialName;
	const std::filesystem::path selectedPath = NormalizeAssetPath(assetPath);
	for (const Vans::VansAssetRecord& record : database->All())
	{
		if (NormalizeAssetPath(record.sourcePath) == selectedPath)
		{
			materialName = record.guid.ToString();
			break;
		}
	}
	if (materialName.empty()) return false;

	const std::vector<std::string> pointerTokens = SplitJsonPointer(changedPointer);
	if (pointerTokens.size() >= 3 && pointerTokens[0] == "asset")
	{
		if (pointerTokens[1] == "parameters" &&
			assetRoot.contains("parameters") &&
			assetRoot["parameters"].is_object())
		{
			if (pointerTokens.size() == 3 && assetRoot["parameters"].contains(pointerTokens[2]))
				return ApplyMaterialParameter(scene, materialName, pointerTokens[2], assetRoot["parameters"][pointerTokens[2]]);
		}
		else if (pointerTokens[1] == "customParameters" &&
			assetRoot.contains("customParameters") &&
			assetRoot["customParameters"].is_object())
		{
			if (assetRoot["customParameters"].contains(pointerTokens[2]))
				return ApplyMaterialParameter(
					scene,
					materialName,
					"customParameters/" + pointerTokens[2],
					assetRoot["customParameters"][pointerTokens[2]]);
		}
	}

	const Json* params = FindMaterialParameters(assetRoot);

	bool changed = false;
	if (params)
		for (auto it = params->begin(); it != params->end(); ++it)
			changed |= ApplyMaterialParameter(scene, materialName, it.key(), it.value());
	if (assetRoot.contains("customParameters") && assetRoot["customParameters"].is_object())
		for (auto it = assetRoot["customParameters"].begin(); it != assetRoot["customParameters"].end(); ++it)
			changed |= ApplyMaterialParameter(scene, materialName, "customParameters/" + it.key(), it.value());
	return changed;
}

bool VansMaterialLiveEditService::ApplyMaterialParameter(
	VansScene* scene,
	const std::string& materialName, const std::string& parameterPath, const Json& value)
{
	if (!scene || materialName.empty()) return false;

	VansMaterial* material = nullptr;
	for (VansAsset* asset : scene->GetMaterialAssets())
	{
		if (asset && asset->m_AssetName == materialName)
		{
			material = dynamic_cast<VansMaterial*>(asset);
			break;
		}
	}
	if (!material) return false;
	VansMaterialManager* materialManager = scene->GetMaterialManager();
	return materialManager && materialManager->ApplyMaterialParameter(*material, parameterPath, value);
}
}

