#include "VansSceneScriptComponentBuilder.h"

#include "../../ScriptCore/VansScriptContext.h"

namespace VansGraphics
{

void VansSceneScriptComponentBuilder::BuildUIControllers(
	VansScriptObject& object,
	const VansScriptUIComponentDescriptors& uiComponents)
{
	for (const VansScriptUIComponentDescriptor& descriptor : uiComponents)
	{
		auto* uiComp = new VansScriptUIComponent();
		uiComp->m_ComponentGuid = descriptor.componentGuid;
		uiComp->m_AutoOpenScreens = descriptor.autoOpenScreens;
		uiComp->m_PreloadScreens = descriptor.preloadScreens;
		uiComp->m_Enabled = false;

		object.AddComponent(uiComp);
		if (descriptor.enabled)
			uiComp->SetEnabled(true);
	}
}

void VansSceneScriptComponentBuilder::BuildScripts(
	VansScriptObject& object,
	const VansScriptComponentDescriptors& scriptComponents)
{
	for (const VansScriptComponentDescriptor& descriptor : scriptComponents)
	{
		if (descriptor.language != VansScriptLanguage::Lua)
			continue;

		auto* luaComp = new VansLuaScriptComponent();
		luaComp->m_ComponentName = "LuaScript";
		luaComp->m_ComponentGuid = descriptor.componentGuid;
		luaComp->m_ScriptPath = descriptor.scriptPath;
		luaComp->m_EntryName = descriptor.entryName;
		luaComp->m_SerializedFields = descriptor.serializedFields;
		luaComp->m_OwnerObject = &object;
		luaComp->m_EnableRequested = descriptor.enabled;

		object.AddComponent(luaComp);
		if (auto* scriptContext = VansScriptContext::GetInstance())
		{
			scriptContext->RegisterScriptComponent(&object, luaComp);
		}
	}
}

}
