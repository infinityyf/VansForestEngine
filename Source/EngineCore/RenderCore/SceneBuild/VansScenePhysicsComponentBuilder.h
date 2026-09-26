#pragma once

#include "../VansScene.h"

#include <functional>
#include <memory>
#include "../../SceneCore/VansScenePhysicsComponentConfig.h"

class VansScriptCharacterControllerComponent;
class VansScriptClothComponent;
class VansScriptPhysicsComponent;

namespace VansGraphics
{
	struct VansScenePhysicsBuildResult
	{
		bool success = false;
		VansScriptPhysicsComponent* physics = nullptr;
		VansScriptClothComponent* cloth = nullptr;
		VansScriptCharacterControllerComponent* characterController = nullptr;
		std::string error;
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

	private:
		static std::unique_ptr<VansEngine::VansPhysicsNode> CreatePhysicsNode(
			VansScene& scene,
			const Vans::VansScenePhysicsNodeConfig& config,
			VansRenderNode* associatedRenderNode,
			uint32_t standaloneTransformID,
			std::string& error);

		static std::unique_ptr<VansEngine::VansClothNode> CreateClothNode(
			VansScene& scene,
			const Vans::VansSceneClothNodeConfig& config,
			VansRenderNode* associatedRenderNode,
			std::string& profileGuid,
			std::string& error);

		static std::unique_ptr<VansEngine::VansCharacterControllerNode> CreateCharacterControllerNode(
			const Vans::VansSceneCharacterControllerConfig& config,
			VansRenderNode* associatedRenderNode,
			uint32_t standaloneTransformID,
			std::string& error);
	};
}
