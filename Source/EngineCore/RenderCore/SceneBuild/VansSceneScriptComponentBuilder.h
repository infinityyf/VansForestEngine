#pragma once

#include "../VansScene.h"

#include "../../ScriptCore/VansScriptTypes.h"

namespace VansGraphics
{
	class VansSceneScriptComponentBuilder
	{
	public:
		static void BuildScripts(
			VansScriptObject& object,
			const VansScriptComponentDescriptors& scriptComponents);
	};
}
