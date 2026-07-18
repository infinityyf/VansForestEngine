#include "VansSceneParticleComponentBuilder.h"

#include "../../ParticleCore/VansParticleAsset.h"
#include "../../ParticleCore/VansParticleManager.h"
#include "../../ParticleCore/VansParticleRuntime.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"
#include "../VansParticleRenderNode.h"
#include "../VulkanCore/VansShader.h"

#include <filesystem>

namespace VansGraphics
{
namespace
{
std::string ResolveParticleTexturePath(const std::string& projectRoot, const std::string& texturePath)
{
	if (texturePath.empty())
		return "";

	std::filesystem::path path(texturePath);
	if (path.is_absolute())
		return texturePath;

	return projectRoot + "/" + texturePath;
}
}

void VansSceneParticleComponentBuilder::BuildParticle(
	VansScene& scene,
	VkDevice& device,
	VansScriptObject& object,
	const json& components,
	const std::string& projectRoot,
	bool hasObjectTransform,
	const glm::vec3& objectPosition,
	const glm::vec3& objectRotation,
	const glm::vec3& objectScale)
{
	if (!components.contains("particle"))
		return;

	const auto& particleJson = components["particle"];
	std::string assetPath = particleJson.value("asset", "");
	bool playOnAwake = particleJson.value("play_on_awake", true);

	if (assetPath.empty())
		return;

	const std::string absPath = projectRoot + "/" + assetPath;

	auto* particleComp = new VansScriptParticleComponent();
	particleComp->m_ParticleAssetPath = assetPath;
	particleComp->m_PlayOnAwake = playOnAwake;

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
					renderNode->SetTransformData(objectPosition, objectRotation, objectScale);
			}

			renderNode->m_Shader = static_cast<VansGraphicsShader*>(scene.FindShaderAsset("Particle"));
			if (!renderNode->m_Shader)
			{
				VANS_LOG_WARN("[LoadSceneObjects] 粒子 Shader 'Particle' 未找到，粒子将无法渲染");
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
							VANS_LOG_WARN("[LoadSceneObjects] 粒子 Shader 'ParticleSixWay' 未找到，将回退普通粒子 Shader");
							renderNode->m_LightingMode = VansParticleLightingMode::UnlitFlipbook;
						}

						const auto& sixWay = rendererConfig.m_SixWayLighting;
						renderNode->m_PositiveAxesTexture = scene.FindOrLoadTexture(
							ResolveParticleTexturePath(projectRoot, sixWay.m_PositiveAxesTexture), false);
						renderNode->m_NegativeAxesTexture = scene.FindOrLoadTexture(
							ResolveParticleTexturePath(projectRoot, sixWay.m_NegativeAxesTexture), false);

						if (!renderNode->m_PositiveAxesTexture || !renderNode->m_NegativeAxesTexture)
						{
							VANS_LOG_WARN("[LoadSceneObjects] Six-Way 粒子贴图缺失，将回退普通粒子 Shader: "
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
				particleComp->Play();

			scene.RegistRenderNode(renderNode, PARTICLE_NODE);

			VANS_LOG("[LoadSceneObjects] 粒子组件 '" << assetPath
				<< "' 已挂载到 object: " << object.m_ObjectName);
		}
		else
		{
			VANS_LOG_WARN("[LoadSceneObjects] 粒子 Quad 缓冲初始化失败: "
				<< object.m_ObjectName);
			delete renderNode;
		}
	}
	else
	{
		VANS_LOG_WARN("[LoadSceneObjects] 粒子资产加载失败 '" << absPath
			<< "'，对象: " << object.m_ObjectName);
	}

	object.AddComponent(particleComp);
}
}
