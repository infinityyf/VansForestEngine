#pragma once

#include "../VansScene.h"

#include "../../ScriptCore/VansScriptTypes.h"

#include <string>
#include <vector>

class VansLuaScriptComponent;
class VansScriptUIComponent;

namespace VansGraphics
{
	struct VansSceneScriptBuildResult
	{
		bool success = false;
		std::string error;
		std::vector<VansScriptUIComponent*> uiControllers;
		std::vector<VansLuaScriptComponent*> scripts;
	};

	class VansSceneScriptComponentBuilder
	{
	public:
		static VansSceneScriptBuildResult Build(
			VansScriptObject& object,
			const VansScriptUIComponentDescriptors& uiComponents,
			const VansScriptComponentDescriptors& scriptComponents);
	};
}
