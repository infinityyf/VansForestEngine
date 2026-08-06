#include "VansSceneLightComponentBuilder.h"

#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"
#include "../BRDFData/VansLight.h"
#include "../VansGraphicsDevice.h"
#include "../VansMaterial.h"
#include "../VansVideoManager.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVideoTexture.h"

#include <algorithm>

namespace VansGraphics
{
namespace
{
glm::vec3 ReadColorOrWhite(const std::optional<std::array<float, 3>>& color)
{
	if (color.has_value())
	{
		return glm::vec3((*color)[0], (*color)[1], (*color)[2]);
	}
	return glm::vec3(1.0f);
}

VansShadowPolicy ReadShadowPolicy(const Vans::VansSceneLightShadowConfig& shadow)
{
	const std::string value = shadow.policy.value_or("Auto");
	if (value == "Disabled" || value == "disabled") return VansShadowPolicy::Disabled;
	if (value == "Hero" || value == "hero") return VansShadowPolicy::Hero;
	if (value == "DistanceDynamic" || value == "Distance Dynamic" || value == "distance_dynamic")
		return VansShadowPolicy::DistanceDynamic;
	return VansShadowPolicy::Auto;
}

VansShadowResolution ReadShadowResolution(const Vans::VansSceneLightShadowConfig& shadow)
{
	if (!shadow.resolution.has_value())
		return VansShadowResolution::Auto;

	const std::string& text = *shadow.resolution;
	if (text == "128" || text == "R128") return VansShadowResolution::R128;
	if (text == "256" || text == "R256") return VansShadowResolution::R256;
	if (text == "512" || text == "R512") return VansShadowResolution::R512;
	if (text == "1024" || text == "R1024") return VansShadowResolution::R1024;
	return VansShadowResolution::Auto;
}

VansShadowUpdateMode ReadShadowUpdateMode(const Vans::VansSceneLightShadowConfig& shadow)
{
	const std::string value = shadow.updateMode.value_or("OnChange");
	if (value == "EveryFrame" || value == "every_frame") return VansShadowUpdateMode::EveryFrame;
	if (value == "Budgeted" || value == "budgeted") return VansShadowUpdateMode::Budgeted;
	return VansShadowUpdateMode::OnChange;
}

VansShadowFallback ReadShadowFallback(const Vans::VansSceneLightShadowConfig& shadow)
{
	const std::string value = shadow.fallback.value_or("ScreenSpace");
	if (value == "None" || value == "none") return VansShadowFallback::None;
	return VansShadowFallback::ScreenSpace;
}

VansPunctualShadowSettings ReadShadowSettings(
	const Vans::VansSceneLightShadowConfig& shadow,
	bool defaultCastShadows)
{
	VansPunctualShadowSettings settings;
	settings.castShadows = shadow.castShadows.value_or(defaultCastShadows);
	settings.policy = ReadShadowPolicy(shadow);
	settings.priority = static_cast<uint8_t>(std::clamp(shadow.priority.value_or(128), 0, 255));
	settings.resolution = ReadShadowResolution(shadow);
	settings.updateMode = ReadShadowUpdateMode(shadow);
	settings.fallback = ReadShadowFallback(shadow);
	settings.maxShadowDistance = (std::max)(shadow.maxShadowDistance.value_or(30.0f), 0.01f);
	settings.nearPlaneOverride = (std::max)(shadow.nearPlaneOverride.value_or(0.0f), 0.0f);
	settings.depthBiasTexels = (std::max)(shadow.depthBiasTexels.value_or(1.0f), 0.0f);
	settings.normalBiasTexels = (std::max)(shadow.normalBiasTexels.value_or(1.0f), 0.0f);
	settings.sourceRadius = (std::max)(shadow.sourceRadius.value_or(0.02f), 0.0f);
	settings.affectsVolumetricFog = shadow.affectsVolumetricFog.value_or(true);
	settings.affectsGI = shadow.affectsGI.value_or(true);
	settings.shadowCasterMask = shadow.shadowCasterMask.value_or(0xffffffffu);
	return settings;
}

void WriteWhiteRectLightEmissiveFallback(VansMaterialManager& materialManager, int layer)
{
	VansTexture* emissiveArray = materialManager.GetRuntimeRenderTexture(
		VansMaterialManager::RT_RECT_LIGHT_EMISSIVE);
	if (emissiveArray == nullptr)
		return;

	VansVKDevice* texVkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	if (texVkDevice == nullptr)
	{
		VANS_LOG_WARN("[VansSceneLightComponentBuilder] Vulkan device unavailable, skip rect light emissive fallback");
		return;
	}

	static const uint8_t kWhitePixel[4] = { 255, 255, 255, 255 };
	emissiveArray->UpdateArrayLayerFromPixels(
		texVkDevice->GetCommandBuffer(), kWhitePixel, 1, 1, layer);
}

bool LoadRectLightStaticEmissiveTexture(
	VansMaterialManager& materialManager,
	VansLightManager& lightManager,
	const std::string& objectName,
	const std::string& projectRoot,
	const std::string& emissiveTexPath,
	int lightIndex,
	bool isFallback)
{
	VansTexture* emissiveArray = materialManager.GetRuntimeRenderTexture(
		VansMaterialManager::RT_RECT_LIGHT_EMISSIVE);
	if (emissiveArray == nullptr)
	{
		VANS_LOG_WARN("[LoadSceneObjects] RT_RECT_LIGHT_EMISSIVE 未就绪，跳过发光贴图加载");
		return false;
	}

	VansVKDevice* texVkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	if (texVkDevice == nullptr)
	{
		VANS_LOG_WARN("[VansSceneLightComponentBuilder] Vulkan device unavailable, skip rect light emissive texture");
		return false;
	}

	const std::string absTexPath = projectRoot + emissiveTexPath;
	if (emissiveArray->LoadTextureLayer(texVkDevice->GetCommandBuffer(), absTexPath, lightIndex))
	{
		lightManager.GetRectLights()[lightIndex].m_TextureSlot = static_cast<float>(lightIndex);
		if (isFallback)
			VANS_LOG("[LoadSceneObjects] 面光源 '" << objectName << "' 降级加载静态发光贴图 slot=" << lightIndex);
		else
			VANS_LOG("[LoadSceneObjects] 面光源 '" << objectName << "' 加载发光贴图 slot=" << lightIndex);
		return true;
	}

	VANS_LOG_WARN("[LoadSceneObjects] 面光源 '" << objectName << "' 发光贴图加载失败，回退到单色: " << absTexPath);
	return false;
}
}

VansSceneLightBuildResult VansSceneLightComponentBuilder::BuildLights(
	VansScene& scene,
	VansScriptObject& object,
	const Vans::VansSceneLightComponentConfig& config,
	const std::string& projectRoot,
	const std::function<void()>& ensureObjectTransform)
{
	VansSceneLightBuildResult result;
	VansLightManager& lightManager = *scene.GetLightManager();
	VansMaterialManager& materialManager = *scene.GetMaterialManager();
	VansIESProfileManager& iesProfileManager = *scene.GetIESProfileManager();
	VansVideoManager* videoManager = scene.GetVideoManager();

	if (config.directionalLight.has_value())
	{
		ensureObjectTransform();
		const Vans::VansSceneDirectionalLightComponentConfig& dl = *config.directionalLight;
		VansDirectionalLight dirLight;
		dirLight.m_Color = ReadColorOrWhite(dl.color);
		dirLight.m_Intensity = dl.intensity.value_or(1.0f);
		dirLight.m_Direction = glm::vec3(0.0f, 1.0f, 0.0f);

		int idx = static_cast<int>(lightManager.GetDirectionLights().size());
		lightManager.AddDirectionalLight(dirLight);

		auto* dlComp = new VansScriptDirectionalLightComponent();
		dlComp->m_LightManager = &lightManager;
		dlComp->m_LightIndex = idx;
		object.AddComponent(dlComp);
		result.directionalLight = dlComp;
		VANS_LOG("[LoadSceneObjects] 创建方向光组件 '" << object.m_ObjectName << "' idx=" << idx);
	}

	if (config.pointLight.has_value())
	{
		ensureObjectTransform();
		const Vans::VansScenePointLightComponentConfig& pl = *config.pointLight;
		VansPointLight pointLight{};
		pointLight.m_Color = ReadColorOrWhite(pl.color);
		pointLight.m_Intensity = pl.intensity.value_or(1.0f);
		pointLight.m_Radius = pl.radius.value_or(10.0f);
		pointLight.m_IESProfileIndex = -1.0f;
		pointLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
		pointLight.m_Position = glm::vec3(0.0f);
		const VansPunctualShadowSettings shadowSettings = ReadShadowSettings(pl.shadow, true);

		if (pl.iesProfile.has_value() && !pl.iesProfile->empty())
		{
			std::string iesPath = projectRoot + *pl.iesProfile;
			int iesIdx = -1;
			if (iesProfileManager.LoadIESFile(iesPath, iesIdx))
				pointLight.m_IESProfileIndex = static_cast<float>(iesIdx);
			else
				VANS_LOG_WARN("[LoadSceneObjects] 点光源 '" << object.m_ObjectName << "' IES 加载失败: " << iesPath);
		}

		int idx = static_cast<int>(lightManager.GetPointLights().size());
		lightManager.AddPointLight(pointLight, shadowSettings);

		auto* plComp = new VansScriptPointLightComponent();
		plComp->m_LightManager = &lightManager;
		plComp->m_LightIndex = idx;
		object.AddComponent(plComp);
		result.pointLight = plComp;
		VANS_LOG("[LoadSceneObjects] 创建点光源组件 '" << object.m_ObjectName << "' idx=" << idx);
	}

	if (config.spotLight.has_value())
	{
		ensureObjectTransform();
		const Vans::VansSceneSpotLightComponentConfig& sl = *config.spotLight;
		VansSpotLight spotLight{};
		spotLight.m_Color = ReadColorOrWhite(sl.color);
		spotLight.m_Intensity = sl.intensity.value_or(1.0f);
		spotLight.m_Radius = sl.radius.value_or(10.0f);
		spotLight.m_InnerCutOff = glm::radians(sl.innerCutoffDegrees.value_or(30.0f));
		spotLight.m_OuterCutOff = glm::radians(sl.outerCutoffDegrees.value_or(45.0f));
		spotLight.m_IESProfileIndex = -1.0f;
		spotLight.m_IESIntensityScale = sl.iesIntensityScale.value_or(1.0f);
		spotLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
		spotLight.m_pad0 = 0.0f;
		spotLight.m_Position = glm::vec3(0.0f);
		spotLight.m_Direction = glm::vec3(0.0f, 1.0f, 0.0f);
		const VansPunctualShadowSettings shadowSettings = ReadShadowSettings(sl.shadow, true);

		if (sl.iesProfile.has_value() && !sl.iesProfile->empty())
		{
			std::string iesPath = projectRoot + *sl.iesProfile;
			int iesIdx = -1;
			if (iesProfileManager.LoadIESFile(iesPath, iesIdx))
				spotLight.m_IESProfileIndex = static_cast<float>(iesIdx);
			else
				VANS_LOG_WARN("[LoadSceneObjects] 聚光灯 '" << object.m_ObjectName << "' IES 加载失败: " << iesPath);
		}

		int idx = static_cast<int>(lightManager.GetSpotLight().size());
		lightManager.AddSpotLight(spotLight, shadowSettings);

		auto* slComp = new VansScriptSpotLightComponent();
		slComp->m_LightManager = &lightManager;
		slComp->m_LightIndex = idx;
		object.AddComponent(slComp);
		result.spotLight = slComp;
		VANS_LOG("[LoadSceneObjects] 创建聚光灯组件 '" << object.m_ObjectName << "' idx=" << idx);
	}

	if (config.rectLight.has_value())
	{
		ensureObjectTransform();
		const Vans::VansSceneRectLightComponentConfig& rl = *config.rectLight;
		VansRectLight rectLight{};
		rectLight.m_Color = ReadColorOrWhite(rl.color);
		rectLight.m_Intensity = rl.intensity.value_or(50.0f);
		rectLight.m_HalfWidth = rl.width.value_or(1.0f) * 0.5f;
		rectLight.m_HalfHeight = rl.height.value_or(1.0f) * 0.5f;
		rectLight.m_Range = rl.range.value_or(10.0f);
		rectLight.m_TwoSided = rl.twoSided.value_or(false) ? 1.0f : 0.0f;
		rectLight.m_AttenuationExp = rl.attenuationExp.value_or(2.0f);
		rectLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
		rectLight.m_Position = glm::vec3(0.0f);
		rectLight.m_Normal = glm::vec3(0.0f, 0.0f, 1.0f);
		rectLight.m_Right = glm::vec3(1.0f, 0.0f, 0.0f);
		rectLight.m_Up = glm::vec3(0.0f, 1.0f, 0.0f);
		rectLight.m_TextureSlot = -1.0f;
		rectLight.m_TexLodBias = rl.textureLodBias.value_or(0.0f);
		VansPunctualShadowSettings shadowSettings =
			ReadShadowSettings(rl.shadow, false);

		const std::string emissiveTexPath = rl.emissiveTexture.value_or("");
		const std::string emissiveVideoName = rl.emissiveVideo.value_or("");

		int idx = static_cast<int>(lightManager.GetRectLights().size());
		lightManager.AddRectLight(rectLight, shadowSettings);

		auto* rlComp = new VansScriptRectLightComponent();
		rlComp->m_LightManager = &lightManager;
		rlComp->m_LightIndex = idx;
		rlComp->m_EmissiveTexturePath = emissiveTexPath;

		if (!emissiveVideoName.empty() && idx < 32)
		{
			VansVideoTexture* videoTex = videoManager ? videoManager->Get(emissiveVideoName) : nullptr;
			if (videoTex != nullptr)
			{
				lightManager.GetRectLights()[idx].m_TextureSlot = static_cast<float>(idx);
				WriteWhiteRectLightEmissiveFallback(materialManager, idx);
				VANS_LOG("[LoadSceneObjects] 面光源 '" << object.m_ObjectName << "' 绑定视频发光 '"
					<< emissiveVideoName << "' slot=" << idx);
			}
			else
			{
				VANS_LOG_WARN("[LoadSceneObjects] 面光源 '" << object.m_ObjectName
					<< "' emissive_video '" << emissiveVideoName << "' 未找到，回退到静态贴图");
				if (!emissiveTexPath.empty())
				{
					LoadRectLightStaticEmissiveTexture(
						materialManager,
						lightManager,
						object.m_ObjectName,
						projectRoot,
						emissiveTexPath,
						idx,
						true);
				}
			}
		}
		else if (!emissiveTexPath.empty() && idx < 32)
		{
			LoadRectLightStaticEmissiveTexture(
				materialManager,
				lightManager,
				object.m_ObjectName,
				projectRoot,
				emissiveTexPath,
				idx,
				false);
		}

		object.AddComponent(rlComp);
		result.rectLight = rlComp;
		VANS_LOG("[LoadSceneObjects] 创建面光源组件 '" << object.m_ObjectName << "' idx=" << idx);
	}
	return result;
}

void VansSceneLightComponentBuilder::BindExplicitVideoComponentToRectLight(
	VansScene& scene,
	VansScriptObject& object)
{
	auto* rlComp = object.GetComponent<VansScriptRectLightComponent>();
	auto* videoComp = object.GetComponent<VansScriptVideoComponent>();
	if (rlComp == nullptr ||
		videoComp == nullptr ||
		rlComp->m_VideoComponent != nullptr ||
		videoComp->m_VideoTex == nullptr)
	{
		return;
	}

	const int idx = rlComp->m_LightIndex;
	if (idx < 0 || idx >= 32)
		return;

	VansLightManager& lightManager = *scene.GetLightManager();
	VansMaterialManager& materialManager = *scene.GetMaterialManager();
	rlComp->m_VideoComponent = videoComp;
	lightManager.GetRectLights()[idx].m_TextureSlot = static_cast<float>(idx);
	WriteWhiteRectLightEmissiveFallback(materialManager, idx);

	VANS_LOG("[LoadSceneObjects] 面光源 '" << object.m_ObjectName
		<< "' 自动绑定 VideoComponent '" << videoComp->m_VideoName
		<< "' slot=" << idx);
}
}
