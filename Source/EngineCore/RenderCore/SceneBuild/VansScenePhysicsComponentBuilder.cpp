#include "VansScenePhysicsComponentBuilder.h"

#include "../../ScriptCore/VansScriptContext.h"
#include "../../PhysicsCore/VansCharacterControllerNode.h"
#include "../../PhysicsCore/VansClothNode.h"
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
	auto* renderComponent = object.GetComponent<VansScriptRenderComponent>();
	VansRenderNode* associatedNode = renderComponent ? renderComponent->m_RenderNode : nullptr;
	std::unique_ptr<VansEngine::VansPhysicsNode> physicsNode;
	std::unique_ptr<VansEngine::VansClothNode> clothNode;
	std::unique_ptr<VansEngine::VansCharacterControllerNode> controllerNode;
	std::string clothProfileGuid;

	if (components.physics)
	{
		if (!associatedNode && hasObjectTransform)
			ensureObjectTransform();

		const uint32_t standaloneTransformID = object.m_OwnsTransform ? object.m_TransformID : UINT32_MAX;
		physicsNode = CreatePhysicsNode(
			scene,
			*components.physics,
			associatedNode,
			standaloneTransformID,
			result.error);
		if (!physicsNode)
			return result;
	}

	if (components.cloth)
	{
		clothNode = CreateClothNode(
			scene,
			*components.cloth,
			associatedNode,
			clothProfileGuid,
			result.error);
		if (!clothNode)
			return result;
	}

	if (components.characterController)
	{
		if (!associatedNode)
			ensureObjectTransform();

		const uint32_t standaloneTransformID = (!associatedNode && object.m_OwnsTransform)
			? object.m_TransformID
			: UINT32_MAX;
		controllerNode = CreateCharacterControllerNode(
			*components.characterController,
			associatedNode,
			standaloneTransformID,
			result.error);
		if (!controllerNode)
			return result;
	}

	// Cloth staging 是唯一仍可失败的发布资源；先完整创建，再提交其 runtime record。
	if (clothNode && !scene.RegisterClothNode(clothNode.get(), associatedNode, result.error))
		return result;

	// 三个请求及 Cloth GPU 资源全部成功后才统一发布 facade/owner。
	if (physicsNode)
	{
		auto* physicsComponent = new VansScriptPhysicsComponent();
		physicsComponent->m_ComponentName = "physics";
		physicsComponent->m_PhysicsNode = physicsNode.get();
		const bool enabled = components.physics->enabled.value_or(true);
		if (!enabled)
			physicsNode->SetEnabled(false);
		physicsComponent->m_Enabled = enabled;
		scene.RegisterPhysicsNode(physicsNode.release());
		object.AddComponent(physicsComponent);
		result.physics = physicsComponent;
	}
	if (clothNode)
	{
		auto* clothComponent = new VansScriptClothComponent();
		clothComponent->m_ComponentName = "cloth";
		clothComponent->m_ClothNode = clothNode.get();
		clothComponent->m_ProfileAssetGuid = clothProfileGuid;
		object.AddComponent(clothComponent);
		clothNode.release();
		result.cloth = clothComponent;
	}
	if (controllerNode)
	{
		auto* controllerComponent = new VansScriptCharacterControllerComponent();
		controllerComponent->m_ControllerNode = controllerNode.get();
		scene.RegisterCharacterControllerNode(controllerNode.release());
		object.AddComponent(controllerComponent);
		result.characterController = controllerComponent;
	}
	result.success = true;
	return result;
}
}
