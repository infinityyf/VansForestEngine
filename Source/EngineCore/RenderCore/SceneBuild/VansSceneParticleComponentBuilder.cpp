#include "VansSceneParticleComponentBuilder.h"

#include "../../ParticleCore/VansParticleAsset.h"
#include "../../ParticleCore/VansParticleManager.h"
#include "../../ParticleCore/VansParticleRuntime.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"
#include "../VansParticleRenderNode.h"
#include "../VulkanCore/VansShader.h"

#include <filesystem>
#include <optional>

namespace VansGraphics
{
namespace
{
std::string ResolveParticleTexturePath(const std::string& projectRoot, const std::string& texturePath)
{
	if (texturePath.empty())
	{
		return "";
	}

	std::filesystem::path path(texturePath);
	if (path.is_absolute())
	{
		return texturePath;
	}

	return projectRoot + "/" + texturePath;
}
}

void VansSceneParticleComponentBuilder::BuildParticle(
	VansScene& scene,
	VkDevice& device,
	VansScriptObject& object,
	const Vans::VansSceneParticleComponentConfig& particleConfig,
	const std::string& projectRoot,
	bool hasObjectTransform,
	const glm::vec3& objectPosition,
	const glm::vec3& objectRotation,
	const glm::vec3& objectScale)
{
	if (particleConfig.assetPath.empty())
	{
		return;
	}

	const std::string absPath = projectRoot + "/" + particleConfig.assetPath;

	auto* particleComp = new VansScriptParticleComponent();
	particleComp->m_ParticleAssetPath = particleConfig.assetPath;
	particleComp->m_PlayOnAwake = particleConfig.playOnAwake;

	if (particleComp->LoadAsset(absPath))
	{
		auto* renderNode = new VansParticleRenderNode(device);
		if (renderNode->InitQuadBuffers(device))
		{
			renderNode->SetName(object.m_ObjectName);
			if (object.m_TransformID != 0)
			{
				renderNode->ShareTransform(object.m_TransformID);
			}
			else
			{
				object.m_TransformID = renderNode->m_TransformID;
				if (hasObjectTransform)
				{
					renderNode->SetTransformData(objectPosition, objectRotation, objectScale);
				}
			}

			renderNode->m_Shader = static_cast<VansGraphicsShader*>(scene.FindShaderAsset("Particle"));
			if (!renderNode->m_Shader)
			{
				VANS_LOG_WARN("[LoadSceneObjects] Particle shader 'Particle' was not found; particle will not render");
			}

			if (particleComp->m_ParticleAsset && !particleComp->m_ParticleAsset->m_Emitters.empty())
			{
				auto* emitter = particleComp->m_ParticleAsset->m_Emitters.front().get();
				if (emitter)
				{
					const VansParticleRendererConfig& rendererConfig = emitter->m_RendererConfig;
					renderNode->ApplyRendererConfig(rendererConfig);

					if (rendererConfig.m_LightingMode == VansParticleLightingMode::SixWayLit)
					{
						renderNode->m_SixWayShader = static_cast<VansGraphicsShader*>(scene.FindShaderAsset("ParticleSixWay"));
						if (!renderNode->m_SixWayShader)
						{
							VANS_LOG_WARN("[LoadSceneObjects] Particle shader 'ParticleSixWay' was not found; falling back to unlit flipbook");
							renderNode->m_LightingMode = VansParticleLightingMode::UnlitFlipbook;
						}

						const auto& sixWay = rendererConfig.m_SixWayLighting;
						renderNode->m_PositiveAxesTexture = scene.FindOrLoadTexture(
							ResolveParticleTexturePath(projectRoot, sixWay.m_PositiveAxesTexture), false);
						renderNode->m_NegativeAxesTexture = scene.FindOrLoadTexture(
							ResolveParticleTexturePath(projectRoot, sixWay.m_NegativeAxesTexture), false);

						if (!renderNode->m_PositiveAxesTexture || !renderNode->m_NegativeAxesTexture)
						{
							VANS_LOG_WARN("[LoadSceneObjects] Six-way particle textures are missing; falling back to unlit flipbook: "
								<< object.m_ObjectName);
							renderNode->m_LightingMode = VansParticleLightingMode::UnlitFlipbook;
						}
					}
					else if (!rendererConfig.m_Texture.empty())
					{
						renderNode->m_ParticleTexture = scene.FindOrLoadTexture(
							ResolveParticleTexturePath(projectRoot, rendererConfig.m_Texture), true);
					}
				}
			}

			particleComp->m_RenderNode = renderNode;

			VansParticleManager::Instance().Initialize();
			VansParticleManager::Instance().RegisterRuntime(particleComp->m_Runtime.get());

			if (particleComp->m_PlayOnAwake)
			{
				particleComp->Play();
			}

			scene.RegistRenderNode(renderNode, PARTICLE_NODE);

			VANS_LOG("[LoadSceneObjects] Particle component '" << particleConfig.assetPath
				<< "' attached to object: " << object.m_ObjectName);
		}
		else
		{
			VANS_LOG_WARN("[LoadSceneObjects] Particle quad buffer initialization failed: "
				<< object.m_ObjectName);
			delete renderNode;
		}
	}
	else
	{
		VANS_LOG_WARN("[LoadSceneObjects] Particle asset load failed '" << absPath
			<< "' for object: " << object.m_ObjectName);
	}

	object.AddComponent(particleComp);
}

}
