#pragma once

#include "../VansScene.h"

namespace VansGraphics
{
	class VansSceneParticleComponentBuilder
	{
	public:
		static void BuildParticle(
			VansScene& scene,
			VkDevice& device,
			VansScriptObject& object,
			const json& components,
			const std::string& projectRoot,
			bool hasObjectTransform,
			const glm::vec3& objectPosition,
			const glm::vec3& objectRotation,
			const glm::vec3& objectScale);
	};
}
