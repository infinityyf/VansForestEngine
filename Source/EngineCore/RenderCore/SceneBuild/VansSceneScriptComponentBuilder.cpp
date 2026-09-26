#include "VansSceneScriptComponentBuilder.h"

#include "../../ScriptCore/VansScriptContext.h"

namespace VansGraphics
{

VansSceneScriptBuildResult VansSceneScriptComponentBuilder::Build(
	VansScriptObject& object,
	const VansScriptUIComponentDescriptors& uiComponents,
	const VansScriptComponentDescriptors& scriptComponents)
{
	VansSceneScriptBuildResult result;
	VansScriptContext* scriptContext = nullptr;
	if (!scriptComponents.empty())
	{
		scriptContext = VansScriptContext::GetInstance();
		if (!scriptContext)
		{
			result.error = "Lua components require an initialized ScriptContext";
			return result;
		}
		for (const VansScriptComponentDescriptor& descriptor : scriptComponents)
		{
			if (descriptor.language != VansScriptLanguage::Lua)
			{
				result.error = "Scene contains an unsupported script language";
				return result;
			}
		}
	}

	result.uiControllers.reserve(uiComponents.size());
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
		result.uiControllers.push_back(uiComp);
	}

	result.scripts.reserve(scriptComponents.size());
	for (const VansScriptComponentDescriptor& descriptor : scriptComponents)
	{
		auto* luaComp = new VansLuaScriptComponent();
		luaComp->m_ComponentName = "LuaScript";
		luaComp->m_ComponentGuid = descriptor.componentGuid;
		luaComp->m_ScriptPath = descriptor.scriptPath;
		luaComp->m_EntryName = descriptor.entryName;
		luaComp->m_SerializedFields = descriptor.serializedFields;
		luaComp->m_OwnerObject = &object;
		luaComp->m_EnableRequested = descriptor.enabled;

		object.AddComponent(luaComp);
		if (!scriptContext->RegisterScriptComponent(&object, luaComp))
		{
			result.error = "Could not register Lua component with ScriptContext";
			return result;
		}
		result.scripts.push_back(luaComp);
	}
	result.success = true;
	return result;
}

}
