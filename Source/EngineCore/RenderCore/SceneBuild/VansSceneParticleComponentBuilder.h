#pragma once

#include "../VansScene.h"

#include "../../SceneCore/VansSceneParticleComponentConfig.h"

class VansScriptParticleComponent;

namespace VansGraphics
{
	class VansSceneParticleComponentBuilder
	{
	public:
		static VansScriptParticleComponent* BuildParticle(
			VansScene& scene,
			VkDevice& device,
			VansScriptObject& object,
			const Vans::VansSceneParticleComponentConfig& particleConfig,
			const std::string& projectRoot,
			bool hasObjectTransform,
			const glm::vec3& objectPosition,
			const glm::vec3& objectRotation,
			const glm::vec3& objectScale);

	};
}
