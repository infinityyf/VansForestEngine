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
glm::vec3 ReadColorOrWhite(const json& lightJson)
{
	if (lightJson.contains("color") && lightJson["color"].is_array())
	{
		return glm::vec3(
			lightJson["color"][0].get<float>(),
			lightJson["color"][1].get<float>(),
			lightJson["color"][2].get<float>());
	}
	return glm::vec3(1.0f);
}

VansShadowPolicy ReadShadowPolicy(const json& lightJson)
{
	const std::string value = lightJson.value("shadowPolicy", "Auto");
	if (value == "Disabled" || value == "disabled") return VansShadowPolicy::Disabled;
	if (value == "Hero" || value == "hero") return VansShadowPolicy::Hero;
	if (value == "DistanceDynamic" || value == "Distance Dynamic" || value == "distance_dynamic")
		return VansShadowPolicy::DistanceDynamic;
	return VansShadowPolicy::Auto;
}

VansShadowResolution ReadShadowResolution(const json& lightJson)
{
	if (!lightJson.contains("shadowResolution"))
		return VansShadowResolution::Auto;
	const json& value = lightJson["shadowResolution"];
	if (value.is_number_integer())
	{
		switch (value.get<int>())
		{
		case 128: return VansShadowResolution::R128;
		case 256: return VansShadowResolution::R256;
		case 512: return VansShadowResolution::R512;
		case 1024: return VansShadowResolution::R1024;
		default: return VansShadowResolution::Auto;
		}
	}
	if (value.is_string())
	{
		const std::string text = value.get<std::string>();
		if (text == "128" || text == "R128") return VansShadowResolution::R128;
		if (text == "256" || text == "R256") return VansShadowResolution::R256;
		if (text == "512" || text == "R512") return VansShadowResolution::R512;
		if (text == "1024" || text == "R1024") return VansShadowResolution::R1024;
	}
	return VansShadowResolution::Auto;
}

VansShadowUpdateMode ReadShadowUpdateMode(const json& lightJson)
{
	const std::string value = lightJson.value("shadowUpdateMode", "OnChange");
	if (value == "EveryFrame" || value == "every_frame") return VansShadowUpdateMode::EveryFrame;
	if (value == "Budgeted" || value == "budgeted") return VansShadowUpdateMode::Budgeted;
	return VansShadowUpdateMode::OnChange;
}

VansShadowFallback ReadShadowFallback(const json& lightJson)
{
	const std::string value = lightJson.value("shadowFallback", "ScreenSpace");
	if (value == "None" || value == "none") return VansShadowFallback::None;
	return VansShadowFallback::ScreenSpace;
}

VansPunctualShadowSettings ReadShadowSettings(const json& lightJson, bool defaultCastShadows)
{
	VansPunctualShadowSettings settings;
	settings.castShadows = lightJson.value("castShadows", defaultCastShadows);
	settings.policy = ReadShadowPolicy(lightJson);
	settings.priority = static_cast<uint8_t>(std::clamp(lightJson.value("shadowPriority", 128), 0, 255));
	settings.resolution = ReadShadowResolution(lightJson);
	settings.updateMode = ReadShadowUpdateMode(lightJson);
	settings.fallback = ReadShadowFallback(lightJson);
	settings.maxShadowDistance = (std::max)(lightJson.value("shadowMaxDistance", 30.0f), 0.01f);
	settings.nearPlaneOverride = (std::max)(lightJson.value("shadowNearPlane", 0.0f), 0.0f);
	settings.depthBiasTexels = (std::max)(lightJson.value("shadowDepthBiasTexels", 1.0f), 0.0f);
	settings.normalBiasTexels = (std::max)(lightJson.value("shadowNormalBiasTexels", 1.0f), 0.0f);
	settings.sourceRadius = (std::max)(lightJson.value("shadowSourceRadius", 0.02f), 0.0f);
	settings.affectsVolumetricFog = lightJson.value("shadowAffectsFog", true);
	settings.affectsGI = lightJson.value("shadowAffectsGI", true);
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

void VansSceneLightComponentBuilder::BuildLights(
	VansScene& scene,
	VansScriptObject& object,
	const json& components,
	const std::string& projectRoot,
	const std::function<void()>& ensureObjectTransform)
{
	VansLightManager& lightManager = *scene.GetLightManager();
	VansMaterialManager& materialManager = *scene.GetMaterialManager();
	VansIESProfileManager& iesProfileManager = *scene.GetIESProfileManager();
	VansVideoManager* videoManager = scene.GetVideoManager();

	if (components.contains("directional_light"))
	{
		ensureObjectTransform();
		const auto& dlJson = components["directional_light"];
		VansDirectionalLight dirLight;
		dirLight.m_Color = ReadColorOrWhite(dlJson);
		dirLight.m_Intensity = dlJson.value("intensity", 1.0f);
		dirLight.m_Direction = glm::vec3(0.0f, 1.0f, 0.0f);

		int idx = static_cast<int>(lightManager.GetDirectionLights().size());
		lightManager.AddDirectionalLight(dirLight);

		auto* dlComp = new VansScriptDirectionalLightComponent();
		dlComp->m_LightManager = &lightManager;
		dlComp->m_LightIndex = idx;
		object.AddComponent(dlComp);
		VANS_LOG("[LoadSceneObjects] 创建方向光组件 '" << object.m_ObjectName << "' idx=" << idx);
	}

	if (components.contains("point_light"))
	{
		ensureObjectTransform();
		const auto& plJson = components["point_light"];
		VansPointLight pointLight{};
		pointLight.m_Color = ReadColorOrWhite(plJson);
		pointLight.m_Intensity = plJson.value("intensity", 1.0f);
		pointLight.m_Radius = plJson.value("radius", 10.0f);
		pointLight.m_IESProfileIndex = -1.0f;
		pointLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
		pointLight.m_Position = glm::vec3(0.0f);
		const VansPunctualShadowSettings shadowSettings = ReadShadowSettings(plJson, true);

		if (plJson.contains("ies_profile") && plJson["ies_profile"].is_string())
		{
			std::string iesPath = projectRoot + plJson["ies_profile"].get<std::string>();
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
		VANS_LOG("[LoadSceneObjects] 创建点光源组件 '" << object.m_ObjectName << "' idx=" << idx);
	}

	if (components.contains("spot_light"))
	{
		ensureObjectTransform();
		const auto& slJson = components["spot_light"];
		VansSpotLight spotLight{};
		spotLight.m_Color = ReadColorOrWhite(slJson);
		spotLight.m_Intensity = slJson.value("intensity", 1.0f);
		spotLight.m_Radius = slJson.value("radius", 10.0f);
		spotLight.m_InnerCutOff = glm::radians(slJson.value("innercutoff", 30.0f));
		spotLight.m_OuterCutOff = glm::radians(slJson.value("outerCutoff", 45.0f));
		spotLight.m_IESProfileIndex = -1.0f;
		spotLight.m_IESIntensityScale = slJson.value("ies_intensity_scale", 1.0f);
		spotLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
		spotLight.m_pad0 = 0.0f;
		spotLight.m_Position = glm::vec3(0.0f);
		spotLight.m_Direction = glm::vec3(0.0f, 1.0f, 0.0f);
		const VansPunctualShadowSettings shadowSettings = ReadShadowSettings(slJson, true);

		if (slJson.contains("ies_profile") && slJson["ies_profile"].is_string())
		{
			std::string iesPath = projectRoot + slJson["ies_profile"].get<std::string>();
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
		VANS_LOG("[LoadSceneObjects] 创建聚光灯组件 '" << object.m_ObjectName << "' idx=" << idx);
	}

	if (components.contains("rect_light"))
	{
		ensureObjectTransform();
		const auto& rlJson = components["rect_light"];
		VansRectLight rectLight{};
		rectLight.m_Color = ReadColorOrWhite(rlJson);
		rectLight.m_Intensity = rlJson.value("intensity", 50.0f);
		rectLight.m_HalfWidth = rlJson.value("width", 1.0f) * 0.5f;
		rectLight.m_HalfHeight = rlJson.value("height", 1.0f) * 0.5f;
		rectLight.m_Range = rlJson.value("range", 10.0f);
		rectLight.m_TwoSided = rlJson.value("two_sided", false) ? 1.0f : 0.0f;
		rectLight.m_AttenuationExp = rlJson.value("attenuation_exp", 2.0f);
		rectLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
		rectLight.m_Position = glm::vec3(0.0f);
		rectLight.m_Normal = glm::vec3(0.0f, 0.0f, 1.0f);
		rectLight.m_Right = glm::vec3(1.0f, 0.0f, 0.0f);
		rectLight.m_Up = glm::vec3(0.0f, 1.0f, 0.0f);
		rectLight.m_TextureSlot = -1.0f;
		rectLight.m_TexLodBias = rlJson.value("texture_lod_bias", 0.0f);
		VansPunctualShadowSettings shadowSettings = ReadShadowSettings(rlJson, rlJson.value("shadow", false));

		std::string emissiveTexPath;
		std::string emissiveVideoName;
		if (rlJson.contains("emissive_texture") && rlJson["emissive_texture"].is_string())
			emissiveTexPath = rlJson["emissive_texture"].get<std::string>();
		if (rlJson.contains("emissive_video") && rlJson["emissive_video"].is_string())
			emissiveVideoName = rlJson["emissive_video"].get<std::string>();

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
		VANS_LOG("[LoadSceneObjects] 创建面光源组件 '" << object.m_ObjectName << "' idx=" << idx);
	}
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
