#pragma once

#include "../VansScene.h"

#include <functional>
#include "../../SceneCore/VansScenePhysicsComponentConfig.h"

class VansScriptCharacterControllerComponent;
class VansScriptClothComponent;
class VansScriptPhysicsComponent;

namespace VansGraphics
{
	struct VansScenePhysicsBuildResult
	{
		VansScriptPhysicsComponent* physics = nullptr;
		VansScriptClothComponent* cloth = nullptr;
		VansScriptCharacterControllerComponent* characterController = nullptr;
	};

	class VansScenePhysicsComponentBuilder
	{
	public:
		static VansScenePhysicsBuildResult BuildPhysicsClothAndCharacter(
			VansScene& scene,
			VansScriptObject& object,
			const Vans::VansScenePhysicsComponentsConfig& components,
			bool hasObjectTransform,
			const std::function<void()>& ensureObjectTransform);

		static VansEngine::VansPhysicsNode* LoadPhysicsNode(
			VansScene& scene,
			const Vans::VansScenePhysicsNodeConfig& config,
			VansRenderNode* associatedRenderNode,
			uint32_t standaloneTransformID = UINT32_MAX);

		static VansEngine::VansClothNode* LoadClothNode(
			VansScene& scene,
			const Vans::VansSceneClothNodeConfig& config,
			VansRenderNode* associatedRenderNode,
			std::string* outProfileGuid = nullptr);

		static VansEngine::VansCharacterControllerNode* LoadCharacterControllerNode(
			VansScene& scene,
			const Vans::VansSceneCharacterControllerConfig& config,
			VansRenderNode* associatedRenderNode,
			uint32_t standaloneTransformID = UINT32_MAX);
	};
}
