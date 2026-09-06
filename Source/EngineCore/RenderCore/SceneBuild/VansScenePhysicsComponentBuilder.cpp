#include "VansScenePhysicsComponentBuilder.h"

#include "../../ScriptCore/VansScriptContext.h"
#include "../../PhysicsCore/VansPhysicsNode.h"

namespace VansGraphics
{
VansScenePhysicsBuildResult VansScenePhysicsComponentBuilder::BuildPhysicsClothAndCharacter(
	VansScene& scene,
	VansScriptObject& object,
	const Vans::VansScenePhysicsComponentsConfig& components,
	bool hasObjectTransform,
	const std::function<void()>& ensureObjectTransform)
{
	VansScenePhysicsBuildResult result;
	if (components.physics)
	{
		auto* renderComp = object.GetComponent<VansScriptRenderComponent>();
		VansRenderNode* associatedNode = renderComp ? renderComp->m_RenderNode : nullptr;
		if (!associatedNode && hasObjectTransform)
			ensureObjectTransform();

		const uint32_t standaloneTransformID = object.m_OwnsTransform ? object.m_TransformID : UINT32_MAX;
		VansEngine::VansPhysicsNode* physicsNode =
			LoadPhysicsNode(scene, *components.physics, associatedNode, standaloneTransformID);
		if (physicsNode)
		{
			auto* physicsComp = new VansScriptPhysicsComponent();
			physicsComp->m_ComponentName = "physics";
			physicsComp->m_PhysicsNode = physicsNode;

			const bool enabled = components.physics->enabled.value_or(true);
			if (!enabled && physicsComp->m_PhysicsNode)
				physicsComp->m_PhysicsNode->SetEnabled(false);
			physicsComp->m_Enabled = enabled;

			object.AddComponent(physicsComp);
			result.physics = physicsComp;
		}
	}

	if (components.cloth)
	{
		auto* renderComp = object.GetComponent<VansScriptRenderComponent>();
		VansRenderNode* associatedNode = renderComp ? renderComp->m_RenderNode : nullptr;
		std::string profileGuid;
		VansEngine::VansClothNode* clothNode = LoadClothNode(
			scene, *components.cloth, associatedNode, &profileGuid);
		if (clothNode)
		{
			auto* clothComp = new VansScriptClothComponent();
			clothComp->m_ComponentName = "cloth";
			clothComp->m_ClothNode = clothNode;
			clothComp->m_ProfileAssetGuid = profileGuid;
			object.AddComponent(clothComp);
			result.cloth = clothComp;
		}
	}

	if (components.characterController)
	{
		auto* renderComp = object.GetComponent<VansScriptRenderComponent>();
		VansRenderNode* associatedNode = renderComp ? renderComp->m_RenderNode : nullptr;
		if (!associatedNode)
			ensureObjectTransform();

		const uint32_t standaloneTransformID = (!associatedNode && object.m_OwnsTransform)
			? object.m_TransformID
			: UINT32_MAX;
		VansEngine::VansCharacterControllerNode* controllerNode =
			LoadCharacterControllerNode(scene, *components.characterController, associatedNode, standaloneTransformID);
		if (controllerNode)
		{
			auto* controllerComp = new VansScriptCharacterControllerComponent();
			controllerComp->m_ControllerNode = controllerNode;
			object.AddComponent(controllerComp);
			result.characterController = controllerComp;
		}
	}
	return result;
}
}
