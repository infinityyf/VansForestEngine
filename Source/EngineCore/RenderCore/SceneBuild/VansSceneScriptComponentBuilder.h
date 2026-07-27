#pragma once

#include "../VansScene.h"

#include "../../ScriptCore/VansPythonScriptComponentDescriptor.h"

namespace VansGraphics
{
	class VansSceneScriptComponentBuilder
	{
	public:
		static void BuildPythonScripts(
			VansScriptObject& object,
			const VansPythonScriptComponentDescriptors& scriptComponents);
	};
}
