#pragma once

#include "../VansScene.h"

#include <functional>
#include "../../SceneCore/VansScenePhysicsComponentConfig.h"

namespace VansGraphics
{
	class VansScenePhysicsComponentBuilder
	{
	public:
		static void BuildPhysicsClothAndCharacter(
			VansScene& scene,
			VansScriptObject& object,
			const Vans::VansScenePhysicsComponentsConfig& components,
			const std::string& projectRoot,
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
			std::string* outProfilePath = nullptr);

		static VansEngine::VansCharacterControllerNode* LoadCharacterControllerNode(
			VansScene& scene,
			const Vans::VansSceneCharacterControllerConfig& config,
			VansRenderNode* associatedRenderNode,
			uint32_t standaloneTransformID = UINT32_MAX);
	};
}
