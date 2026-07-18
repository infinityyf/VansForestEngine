#pragma once

#include "../VansScene.h"

#include <functional>

namespace VansGraphics
{
	class VansScenePhysicsComponentBuilder
	{
	public:
		static void BuildPhysicsClothAndCharacter(
			VansScene& scene,
			VansScriptObject& object,
			const json& components,
			const std::string& projectRoot,
			bool hasObjectTransform,
			const std::function<void()>& ensureObjectTransform);

		static VansEngine::VansPhysicsNode* LoadPhysicsNode(
			VansScene& scene,
			const json& physicsNodeJson,
			VansRenderNode* associatedRenderNode,
			uint32_t standaloneTransformID = UINT32_MAX);

		static VansEngine::VansClothNode* LoadClothNode(
			VansScene& scene,
			const json& clothNodeJson,
			VansRenderNode* associatedRenderNode,
			std::string* outProfilePath = nullptr);

		static VansEngine::VansCharacterControllerNode* LoadCharacterControllerNode(
			VansScene& scene,
			const json& charCtrlJson,
			VansRenderNode* associatedRenderNode,
			uint32_t standaloneTransformID = UINT32_MAX);
	};
}
