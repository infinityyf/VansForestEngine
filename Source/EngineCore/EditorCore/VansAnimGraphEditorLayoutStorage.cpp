#include "VansAnimGraphEditorLayoutStorage.h"

#include "../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace Vans
{
	namespace
	{
		using Json = nlohmann::ordered_json;
	}

	bool VansAnimGraphEditorLayoutStorage::Save(
		const std::string& filePath,
		const std::vector<AnimGraphEditorNodeLayout>& layouts,
		std::string& error)
	{
		Json root;
		if (!VansJsonFileStorage::Read(filePath, root, error))
			return false;

		Json editorObj;
		Json layoutArray = Json::array();
		for (const AnimGraphEditorNodeLayout& layout : layouts)
		{
			Json item;
			item["state"] = layout.stateName;
			item["x"] = layout.x;
			item["y"] = layout.y;
			layoutArray.push_back(std::move(item));
		}

		editorObj["nodeLayouts"] = std::move(layoutArray);
		root["editor"] = std::move(editorObj);
		return VansJsonFileStorage::WriteAtomic(filePath, root, error);
	}
}
