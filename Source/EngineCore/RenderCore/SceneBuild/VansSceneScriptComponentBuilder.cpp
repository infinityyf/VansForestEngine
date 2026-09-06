#include "VansSceneScriptComponentBuilder.h"

#include "../../ScriptCore/VansScriptContext.h"

namespace VansGraphics
{

std::vector<VansScriptUIComponent*> VansSceneScriptComponentBuilder::BuildUIControllers(
	VansScriptObject& object,
	const VansScriptUIComponentDescriptors& uiComponents)
{
	std::vector<VansScriptUIComponent*> result;
	result.reserve(uiComponents.size());
	for (const VansScriptUIComponentDescriptor& descriptor : uiComponents)
	{
		auto* uiComp = new VansScriptUIComponent();
		uiComp->m_ComponentGuid = descriptor.componentGuid;
		uiComp->m_AutoOpenScreenAssetGuids = descriptor.autoOpenScreenAssetGuids;
		uiComp->m_PreloadScreenAssetGuids = descriptor.preloadScreenAssetGuids;
		uiComp->m_Enabled = false;

		object.AddComponent(uiComp);
		if (descriptor.enabled)
			uiComp->SetEnabled(true);
		result.push_back(uiComp);
	}
	return result;
}

std::vector<VansLuaScriptComponent*> VansSceneScriptComponentBuilder::BuildScripts(
	VansScriptObject& object,
	const VansScriptComponentDescriptors& scriptComponents)
{
	std::vector<VansLuaScriptComponent*> result;
	result.reserve(scriptComponents.size());
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
		result.push_back(luaComp);
	}
	return result;
}

}
