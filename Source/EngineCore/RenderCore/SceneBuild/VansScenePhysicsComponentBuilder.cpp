#include "VansScenePhysicsComponentBuilder.h"

#include "../../ScriptCore/VansScriptContext.h"
#include "../../PhysicsCore/VansPhysicsNode.h"

namespace VansGraphics
{
void VansScenePhysicsComponentBuilder::BuildPhysicsClothAndCharacter(
	VansScene& scene,
	VansScriptObject& object,
	const json& components,
	const std::string& projectRoot,
	bool hasObjectTransform,
	const std::function<void()>& ensureObjectTransform)
{
	if (components.contains("physics"))
	{
		auto* renderComp = object.GetComponent<VansScriptRenderComponent>();
		VansRenderNode* associatedNode = renderComp ? renderComp->m_RenderNode : nullptr;
		if (!associatedNode && hasObjectTransform)
			ensureObjectTransform();

		const uint32_t standaloneTransformID = object.m_OwnsTransform ? object.m_TransformID : UINT32_MAX;
		VansEngine::VansPhysicsNode* physicsNode =
			LoadPhysicsNode(scene, components["physics"], associatedNode, standaloneTransformID);
		if (physicsNode)
		{
			auto* physicsComp = new VansScriptPhysicsComponent();
			physicsComp->m_ComponentName = "physics";
			physicsComp->m_PhysicsNode = physicsNode;

			const bool enabled = components["physics"].value("enabled", true);
			if (!enabled && physicsComp->m_PhysicsNode)
				physicsComp->m_PhysicsNode->SetEnabled(false);
			physicsComp->m_Enabled = enabled;

			object.AddComponent(physicsComp);
		}
	}

	if (components.contains("cloth"))
	{
		auto* renderComp = object.GetComponent<VansScriptRenderComponent>();
		VansRenderNode* associatedNode = renderComp ? renderComp->m_RenderNode : nullptr;
		std::string profilePath;
		json clothJson = components["cloth"];
		if (clothJson.contains("profilePath") && !projectRoot.empty())
		{
			const std::string relPath = clothJson["profilePath"].get<std::string>();
			clothJson["profilePath"] = projectRoot + relPath;
		}

		VansEngine::VansClothNode* clothNode = LoadClothNode(scene, clothJson, associatedNode, &profilePath);
		if (clothNode)
		{
			auto* clothComp = new VansScriptClothComponent();
			clothComp->m_ComponentName = "cloth";
			clothComp->m_ClothNode = clothNode;
			clothComp->m_ProfilePath = profilePath;
			object.AddComponent(clothComp);
		}
	}

	if (components.contains("charController"))
	{
		auto* renderComp = object.GetComponent<VansScriptRenderComponent>();
		VansRenderNode* associatedNode = renderComp ? renderComp->m_RenderNode : nullptr;
		if (!associatedNode)
			ensureObjectTransform();

		const uint32_t standaloneTransformID = (!associatedNode && object.m_OwnsTransform)
			? object.m_TransformID
			: UINT32_MAX;
		VansEngine::VansCharacterControllerNode* controllerNode =
			LoadCharacterControllerNode(scene, components["charController"], associatedNode, standaloneTransformID);
		if (controllerNode)
		{
			auto* controllerComp = new VansScriptCharacterControllerComponent();
			controllerComp->m_ControllerNode = controllerNode;
			object.AddComponent(controllerComp);
		}
	}
}
}
