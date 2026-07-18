#pragma once

#include "../VansScene.h"

namespace VansGraphics
{
	class VansSceneScriptComponentBuilder
	{
	public:
		static void BuildPythonScripts(VansScriptObject& object, const json& objectJson);
	};
}
