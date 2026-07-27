#include "VansSceneScriptComponentBuilder.h"

#include "../../ScriptCore/VansScriptContext.h"

namespace VansGraphics
{

void VansSceneScriptComponentBuilder::BuildPythonScripts(
	VansScriptObject& object,
	const VansPythonScriptComponentDescriptors& scriptComponents)
{
	for (const VansPythonScriptComponentDescriptor& descriptor : scriptComponents)
	{
		auto* pyComp = new VanPyScriptComponent();
		pyComp->m_ComponentName = "PyScript";
		pyComp->m_ComponentGuid = descriptor.componentGuid;
		pyComp->m_ScriptPath = descriptor.scriptPath;
		pyComp->m_ScriptClassName = descriptor.scriptClassName;
		pyComp->m_SerializedFields = descriptor.serializedFields;
		pyComp->m_OwnerObject = &object;

		object.AddComponent(pyComp);
		if (auto* scriptContext = VansScriptContext::GetInstance())
		{
			scriptContext->RegisterScriptComponent(&object, pyComp);
		}
	}
}

}
