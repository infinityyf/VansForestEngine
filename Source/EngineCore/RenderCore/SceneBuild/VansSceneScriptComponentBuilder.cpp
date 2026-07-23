#include "VansSceneScriptComponentBuilder.h"

#include "../../ScriptCore/VansScriptContext.h"

namespace VansGraphics
{
void VansSceneScriptComponentBuilder::BuildPythonScripts(VansScriptObject& object, const json& objectJson)
{
	if (!objectJson.contains("pyScripts"))
		return;

	for (const auto& scriptEntry : objectJson["pyScripts"])
	{
		auto* pyComp = new VanPyScriptComponent();
		pyComp->m_ComponentName = "PyScript";
		pyComp->m_ScriptPath = scriptEntry["path"].get<std::string>();
		pyComp->m_ScriptClassName = scriptEntry["class"].get<std::string>();
		pyComp->m_OwnerObject = &object;
		object.AddComponent(pyComp);
		if (auto* scriptContext = VansScriptContext::GetInstance())
			scriptContext->RegisterScriptComponent(&object, pyComp);
	}
}
}
