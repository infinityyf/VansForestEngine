#pragma once

#include "../VansScene.h"

#include <memory>
#include "../../SceneCore/VansSceneAnimationComponentConfig.h"
#include <string>
#include <vector>

class VansScriptAnimationComponent;

namespace VansGraphics
{
	struct VansSceneAnimationBuildResult
	{
		bool success = false;
		std::string error;
	};

	class VansSceneAnimationComponentBuilder
	{
	public:
		struct PendingAnimationComponent
		{
			VansScriptObject* obj = nullptr;
			VansScriptAnimationComponent* component = nullptr;
			std::shared_ptr<Vans::VansSceneAnimationComponentConfig> animationConfig;
			std::string objectName;
		};

		static void AddAnimationPlaceholder(
			VansScriptObject& object,
			const Vans::VansSceneAnimationComponentConfig& animationConfig,
			std::vector<PendingAnimationComponent>& pendingAnimations);

		static VansSceneAnimationBuildResult BuildAnimations(
			VansScene& scene,
			const std::vector<PendingAnimationComponent>& pendingAnimations,
			const std::string& projectRoot);

	private:
		static VansAnimationNode* BuildAnimationRuntime(
			VansScene& scene,
			const Vans::VansSceneAnimationComponentConfig& animationConfig,
			const std::string& objectName,
			const std::string& entityGuid,
			const std::string& projectRoot);

		static bool BuildRagdollRuntime(
			VansScene& scene,
			VansScriptObject* obj,
			VansAnimationNode* animNode,
			const Vans::VansSceneRagdollComponentConfig& ragdollConfig,
			const std::string& projectRoot);
	};
}
