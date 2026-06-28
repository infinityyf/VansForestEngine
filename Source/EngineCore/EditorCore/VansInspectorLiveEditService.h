#pragma once

#include "../RenderCore/VansScene.h"
#include "../SceneCore/VansSceneDocument.h"

#include <nlohmann/json.hpp>
#include <string>

namespace VansGraphics
{
class VansInspectorLiveEditService
{
public:
	using Json = nlohmann::ordered_json;

	void Bind(VansScene* scene, Vans::VansSceneDocument* document)
	{
		m_Scene = scene;
		m_Document = document;
	}

	void ApplyEntityPatch(const Json& entity);
	void ApplyComponentEnabledFromPointer(const std::string& selectedGuid,
		const std::string& jsonPointer, bool enabled);

private:
	VansScene* m_Scene = nullptr;
	Vans::VansSceneDocument* m_Document = nullptr;
};
}
