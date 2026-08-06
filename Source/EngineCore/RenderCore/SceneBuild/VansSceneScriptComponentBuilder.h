#pragma once

#include "../VansScene.h"

#include "../../ScriptCore/VansScriptTypes.h"

#include <vector>

class VansLuaScriptComponent;
class VansScriptUIComponent;

namespace VansGraphics
{
	struct VansSceneScriptBuildResult
	{
		std::vector<VansScriptUIComponent*> uiControllers;
		std::vector<VansLuaScriptComponent*> scripts;
	};

	class VansSceneScriptComponentBuilder
	{
	public:
		static std::vector<VansLuaScriptComponent*> BuildScripts(
			VansScriptObject& object,
			const VansScriptComponentDescriptors& scriptComponents);
		static std::vector<VansScriptUIComponent*> BuildUIControllers(
			VansScriptObject& object,
			const VansScriptUIComponentDescriptors& uiComponents);
	};
}
