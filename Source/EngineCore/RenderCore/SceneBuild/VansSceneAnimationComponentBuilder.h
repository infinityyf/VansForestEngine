#pragma once

#include "../VansScene.h"

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
			json animJson;
			std::string objectName;
		};

		static void AddAnimationPlaceholder(
			VansScriptObject& object,
			const json& components,
			std::vector<PendingAnimationComponent>& pendingAnimations);

		static void ResolveAnimations(
			VansScene& scene,
			const std::vector<PendingAnimationComponent>& pendingAnimations,
			const std::string& projectRoot);

		static VansAnimationNode* LoadAnimationComponent(
			VansScene& scene,
			const json& animJson,
			const std::string& objectName,
			const std::string& projectRoot);

		static bool LoadRagdollComponent(
			VansScene& scene,
			VansScriptObject* obj,
			VansAnimationNode* animNode,
			const json& ragdollJson,
			const std::string& projectRoot);
	};
}
