#pragma once

#include "../VansScene.h"

#include <memory>
#include "../../SceneCore/VansSceneAnimationComponentConfig.h"
#include <vector>

class VansScriptAnimationComponent;

namespace VansGraphics
{
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

		static void ResolveAnimations(
			VansScene& scene,
			const std::vector<PendingAnimationComponent>& pendingAnimations,
			const std::string& projectRoot);

		static VansAnimationNode* LoadAnimationComponent(
			VansScene& scene,
			const Vans::VansSceneAnimationComponentConfig& animationConfig,
			const std::string& objectName,
			const std::string& projectRoot);

		static bool LoadRagdollComponent(
			VansScene& scene,
			VansScriptObject* obj,
			VansAnimationNode* animNode,
			const Vans::VansSceneRagdollComponentConfig& ragdollConfig,
			const std::string& projectRoot);
	};
}
