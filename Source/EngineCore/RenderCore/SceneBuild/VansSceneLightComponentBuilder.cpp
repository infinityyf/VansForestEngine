#include "VansSceneLightComponentBuilder.h"

#include "../../AssetCore/VansAssetBytes.h"
#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"
#include "../BRDFData/VansLight.h"
#include "../VansGraphicsDevice.h"
#include "../VansMaterial.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansVKDevice.h"

#include <algorithm>

namespace VansGraphics
{
namespace
{
int ResolveIesProfileIndex(
	const std::optional<std::string>& assetGuidText,
	const Vans::VansAssetObjectRepository& repository,
	VansIESProfileManager& iesProfileManager,
	const std::string& objectName,
	const char* lightType)
{
	if (!assetGuidText || assetGuidText->empty())
		return -1;

	Vans::VansAssetGuid assetGuid;
	Vans::VansAssetObjectSnapshotInfo info;
	if (!Vans::VansAssetGuid::TryParse(*assetGuidText, assetGuid) ||
		!repository.FindInfo(assetGuid, info) ||
		info.assetType != Vans::VansAssetType::IESProfile)
	{
		VANS_LOG_WARN("[LoadSceneObjects] " << lightType << " '" << objectName
			<< "' IES asset is unavailable: " << *assetGuidText);
		return -1;
	}

	const auto asset = repository.ResolveLatest<Vans::VansAssetBytes>(assetGuid);
	if (!asset || asset->bytes.empty())
	{
		VANS_LOG_WARN("[LoadSceneObjects] " << lightType << " '" << objectName
			<< "' IES memory snapshot is unavailable: " << *assetGuidText);
		return -1;
	}

	int profileIndex = -1;
	if (!iesProfileManager.LoadIESFromMemory(
		*assetGuidText, asset->bytes.data(), asset->bytes.size(), profileIndex))
	{
		VANS_LOG_WARN("[LoadSceneObjects] " << lightType << " '" << objectName
			<< "' IES parse or allocation failed: " << *assetGuidText);
		return -1;
	}
	return profileIndex;
}

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

void ApplyCookieConfig(
	VansLightCookieSettings& target,
	const Vans::VansSceneLightCookieConfig& source)
{
	target.enabled = source.enabled;
	target.textureGuid = source.textureGuid;
	target.strength = source.strength;
	target.sizeX = source.sizeX;
	target.sizeY = source.sizeY;
	target.scaleX = source.scaleX;
	target.scaleY = source.scaleY;
	target.offsetX = source.offsetX;
	target.offsetY = source.offsetY;
	target.rotationDegrees = source.rotationDegrees;
	target.repeat = source.repeat;
	target.useAlpha = source.useAlpha;
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

bool UploadRectLightEmissive(
	VansMaterialManager& materialManager,
	VansLightManager& lightManager,
	const std::string& objectName,
	const VansTexture& sourceTexture,
	int lightIndex)
{
	const VansRgba8Image& sourceImage = sourceTexture.GetRetainedRgba8Image();
	if (!sourceImage.IsValid())
	{
		VANS_LOG_WARN("[LoadSceneObjects] 面光源 '" << objectName
			<< "' 发光贴图没有可用的 RGBA8 内存数据，回退到单色");
		return false;
	}

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
	const int textureSlot = lightManager.AcquireRectLightTextureSlot(
		static_cast<uint32_t>(lightIndex));
	if (textureSlot < 0)
	{
		VANS_LOG_WARN("[LoadSceneObjects] 面光源 '" << objectName << "' 没有可用发光纹理槽位");
		return false;
	}

	if (emissiveArray->UpdateArrayLayerFromPixels(
		texVkDevice->GetCommandBuffer(),
		sourceImage.pixels.data(),
		sourceImage.width,
		sourceImage.height,
		textureSlot))
	{
		VANS_LOG("[LoadSceneObjects] 面光源 '" << objectName
			<< "' 上传 GUID 发光贴图 slot=" << textureSlot);
		return true;
	}

	lightManager.ReleaseRectLightTextureSlot(static_cast<uint32_t>(lightIndex));
	VANS_LOG_WARN("[LoadSceneObjects] 面光源 '" << objectName
		<< "' 发光贴图上传失败，回退到单色");
	return false;
}
}

VansSceneLightDependencies VansSceneLightComponentBuilder::ResolveDependencies(
	VansScene& scene,
	const Vans::VansSceneLightComponentConfig& config,
	const Vans::VansAssetObjectRepository& repository,
	VansIESProfileManager& iesProfileManager,
	const std::string& objectName)
{
	VansSceneLightDependencies dependencies;
	if (config.pointLight)
		dependencies.pointIesProfileIndex = ResolveIesProfileIndex(
			config.pointLight->iesProfileGuid, repository, iesProfileManager,
			objectName, "PointLight");
	if (config.spotLight)
		dependencies.spotIesProfileIndex = ResolveIesProfileIndex(
			config.spotLight->iesProfileGuid, repository, iesProfileManager,
			objectName, "SpotLight");
	if (config.rectLight && config.rectLight->emissiveTextureGuid &&
		!config.rectLight->emissiveTextureGuid->empty())
	{
		const std::string& guidText = *config.rectLight->emissiveTextureGuid;
		Vans::VansAssetGuid guid;
		Vans::VansAssetObjectSnapshotInfo info;
		if (!Vans::VansAssetGuid::TryParse(guidText, guid) ||
			!repository.FindInfo(guid, info) ||
			info.assetType != Vans::VansAssetType::Texture)
		{
			VANS_LOG_WARN("[LoadSceneObjects] RectLight '" << objectName
				<< "' emissive texture asset is unavailable: " << guidText);
		}
		else
		{
			auto* texture = static_cast<VansTexture*>(scene.FindTextureAssetByGuid(guidText));
			if (texture == nullptr || !texture->GetRetainedRgba8Image().IsValid())
			{
				VANS_LOG_WARN("[LoadSceneObjects] RectLight '" << objectName
					<< "' emissive texture memory is unavailable: " << guidText);
			}
			else
			{
				dependencies.rectEmissiveTexture = texture;
			}
		}
	}
	return dependencies;
}

VansSceneLightBuildResult VansSceneLightComponentBuilder::BuildLights(
	VansScene& scene,
	VansScriptObject& object,
	const Vans::VansSceneLightComponentConfig& config,
	const VansSceneLightDependencies& dependencies,
	const std::function<void()>& ensureObjectTransform)
{
	VansSceneLightBuildResult result;
	VansLightManager& lightManager = *scene.GetLightManager();
	VansMaterialManager& materialManager = *scene.GetMaterialManager();

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
		ApplyCookieConfig(lightManager.Cookie(0, idx), dl.cookie);

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
		pointLight.m_IESProfileIndex = static_cast<float>(dependencies.pointIesProfileIndex);
		pointLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
		pointLight.m_Position = glm::vec3(0.0f);
		const VansPunctualShadowSettings shadowSettings = ReadShadowSettings(pl.shadow, true);

		int idx = static_cast<int>(lightManager.GetPointLights().size());
		lightManager.AddPointLight(pointLight, shadowSettings);

		auto* plComp = new VansScriptPointLightComponent();
		plComp->m_LightManager = &lightManager;
		plComp->m_LightIndex = idx;
		ApplyCookieConfig(lightManager.Cookie(1, idx), pl.cookie);

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
		spotLight.m_IESProfileIndex = static_cast<float>(dependencies.spotIesProfileIndex);
		spotLight.m_IESIntensityScale = sl.iesIntensityScale.value_or(1.0f);
		spotLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
		spotLight.m_pad0 = 0.0f;
		spotLight.m_Position = glm::vec3(0.0f);
		spotLight.m_Direction = glm::vec3(0.0f, 1.0f, 0.0f);
		const VansPunctualShadowSettings shadowSettings = ReadShadowSettings(sl.shadow, true);

		int idx = static_cast<int>(lightManager.GetSpotLight().size());
		lightManager.AddSpotLight(spotLight, shadowSettings);

		auto* slComp = new VansScriptSpotLightComponent();
		slComp->m_LightManager = &lightManager;
		slComp->m_LightIndex = idx;
		ApplyCookieConfig(lightManager.Cookie(2, idx), sl.cookie);

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

		int idx = static_cast<int>(lightManager.GetRectLights().size());
		lightManager.AddRectLight(rectLight, shadowSettings);

		auto* rlComp = new VansScriptRectLightComponent();
		rlComp->m_LightManager = &lightManager;
		rlComp->m_LightIndex = idx;
		ApplyCookieConfig(lightManager.Cookie(3, idx), rl.cookie);

		if (dependencies.rectEmissiveTexture != nullptr)
		{
			UploadRectLightEmissive(
				materialManager,
				lightManager,
				object.m_ObjectName,
				*dependencies.rectEmissiveTexture,
				idx);
		}

		object.AddComponent(rlComp);
		result.rectLight = rlComp;
		VANS_LOG("[LoadSceneObjects] 创建面光源组件 '" << object.m_ObjectName << "' idx=" << idx);
	}
	return result;
}

void VansSceneLightComponentBuilder::BindVideo(
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
	VansLightManager& lightManager = *scene.GetLightManager();
	if (idx < 0 || idx >= static_cast<int>(lightManager.GetRectLights().size()))
		return;
	const int textureSlot = lightManager.AcquireRectLightTextureSlot(
		static_cast<uint32_t>(idx));
	if (textureSlot < 0)
		return;

	VansMaterialManager& materialManager = *scene.GetMaterialManager();
	rlComp->m_VideoComponent = videoComp;
	WriteWhiteRectLightEmissiveFallback(materialManager, textureSlot);

	VANS_LOG("[LoadSceneObjects] 面光源 '" << object.m_ObjectName
		<< "' 自动绑定 VideoComponent '" << videoComp->m_VideoAssetGuid
		<< "' slot=" << textureSlot);
}
}
