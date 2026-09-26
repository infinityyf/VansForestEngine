#include "VansComponentTypeCatalog.h"

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../GameplayActionSchema/VansGameplayActionHostAuthoring.h"
#include "VansSceneEntityFactory.h"
#include "VansSceneLocalVolumetricFogComponentConfig.h"

#include <utility>
#include <vector>
#include <initializer_list>

namespace Vans
{
namespace
{
using Value = VansSerializedValue;

Value GuidReference()
{
	return Value::Object({ { "guid", Value::String("") } });
}

Value Vec3(double x, double y, double z)
{
	return Value::Array({ Value::Float(x), Value::Float(y), Value::Float(z) });
}

std::vector<std::pair<std::string, Value>> ShadowFields(const char* updateMode = "OnChange")
{
	return {
		{ "castShadows", Value::Bool(true) },
		{ "shadowPolicy", Value::String("Auto") },
		{ "shadowPriority", Value::Int(128) },
		{ "shadowResolution", Value::String("Auto") },
		{ "shadowUpdateMode", Value::String(updateMode) },
		{ "shadowFallback", Value::String("ScreenSpace") },
		{ "shadowMaxDistance", Value::Float(30.0) },
		{ "shadowNearPlane", Value::Float(0.0) },
		{ "shadowDepthBiasTexels", Value::Float(1.0) },
		{ "shadowNormalBiasTexels", Value::Float(1.0) },
		{ "shadowSourceRadius", Value::Float(0.02) },
		{ "shadowAffectsFog", Value::Bool(true) },
		{ "shadowAffectsGI", Value::Bool(true) },
		{ "shadowCasterMask", Value::Int(0xffffffff) }
	};
}

Value LightCookie()
{
	return Value::Object({
		{ "enabled", Value::Bool(false) },
		{ "texture", Value::Object({
			{ "domain", Value::String("ProjectAsset") },
			{ "guid", Value::String("") },
			{ "assetType", Value::String("texture") }
		}) },
		{ "strength", Value::Float(1) },
		{ "sizeX", Value::Float(10) },
		{ "sizeY", Value::Float(10) },
		{ "scaleX", Value::Float(1) },
		{ "scaleY", Value::Float(1) },
		{ "offsetX", Value::Float(0) },
		{ "offsetY", Value::Float(0) },
		{ "rotationDegrees", Value::Float(0) },
		{ "repeat", Value::Bool(false) },
		{ "useAlpha", Value::Bool(false) }
	});
}

Value DefaultModelRenderer()
{
	return Value::Object({
		{ "model", GuidReference() },
		{ "castShadows", Value::Bool(true) },
		{ "receiveShadows", Value::Bool(true) },
		{ "rayTracingMode", Value::String("auto") },
		{ "visibilityMask", Value::Int(0xffffffff) },
		{ "shadowCasterMask", Value::Int(0xffffffff) },
		{ "materialOverrides", Value::Object({}) },
		{ "orphanOverrides", Value::Object({}) },
		{ "renderType", Value::String("opaque") }
	});
}

Value DefaultLodGroup()
{
	return Value::Object({
		{ "mode", Value::String("autoScreenError") },
		{ "pixelErrorBudget", Value::Float(1.0) },
		{ "qualityBias", Value::Float(1.0) },
		{ "hysteresis", Value::Float(0.1) },
		{ "levels", Value::Array({}) }
	});
}

Value DefaultPhysics()
{
	return Value::Object({
		{ "name", Value::String("Physics") },
		{ "bodyType", Value::String("static") },
		{ "colliderType", Value::String("box") },
		{ "boxExtents", Vec3(0.5, 0.5, 0.5) },
		{ "mass", Value::Float(1.0) },
		{ "layer", Value::String("Default") },
		{ "isTrigger", Value::Bool(false) },
		{ "material", Value::Object({
			{ "staticFriction", Value::Float(0.5) },
			{ "dynamicFriction", Value::Float(0.5) },
			{ "restitution", Value::Float(0.0) }
		}) }
	});
}

Value DefaultCamera()
{
	return Value::Object({
		{ "fov", Value::Float(60.0) },
		{ "nearClip", Value::Float(0.1) },
		{ "farClip", Value::Float(1000.0) }
	});
}

Value DefaultAnimation()
{
	return Value::Object({
		{ "name", Value::String("Animation") },
		{ "root_motion", Value::Bool(false) },
		{ "animator", Value::String("") }
	});
}

Value DefaultCharacterController()
{
	return Value::Object({
		{ "radius", Value::Float(0.5) },
		{ "height", Value::Float(1.8) },
		{ "slopeLimit", Value::Float(0.707) },
		{ "stepOffset", Value::Float(0.3) },
		{ "contactOffset", Value::Float(0.08) },
		{ "climbingMode", Value::String("easy") },
		{ "layer", Value::String("Default") },
		{ "positionOffset", Vec3(0.0, 0.9, 0.0) }
	});
}

Value DefaultDirectionalLight()
{
	return Value::Object({
		{ "cookie", LightCookie() },
		{ "color", Vec3(1.0, 1.0, 1.0) },
		{ "intensity", Value::Float(1.0) }
	});
}

Value DefaultPointLight()
{
	std::vector<std::pair<std::string, Value>> fields{
		{ "cookie", LightCookie() },
		{ "color", Vec3(1.0, 1.0, 1.0) },
		{ "intensity", Value::Float(1.0) },
		{ "radius", Value::Float(10.0) }
	};
	std::vector<std::pair<std::string, Value>> shadows = ShadowFields("EveryFrame");
	fields.insert(fields.end(), shadows.begin(), shadows.end());
	return Value::Object(std::move(fields));
}

Value DefaultSpotLight()
{
	std::vector<std::pair<std::string, Value>> fields{
		{ "cookie", LightCookie() },
		{ "color", Vec3(1.0, 1.0, 1.0) },
		{ "intensity", Value::Float(1.0) },
		{ "radius", Value::Float(10.0) },
		{ "innercutoff", Value::Float(15.0) },
		{ "outerCutoff", Value::Float(30.0) }
	};
	std::vector<std::pair<std::string, Value>> shadows = ShadowFields();
	fields.insert(fields.end(), shadows.begin(), shadows.end());
	return Value::Object(std::move(fields));
}

Value DefaultRectLight()
{
	std::vector<std::pair<std::string, Value>> fields{
		{ "cookie", LightCookie() },
		{ "color", Vec3(1.0, 1.0, 1.0) },
		{ "intensity", Value::Float(1.0) },
		{ "width", Value::Float(1.0) },
		{ "height", Value::Float(1.0) },
		{ "range", Value::Float(10.0) },
		{ "two_sided", Value::Bool(false) }
	};
	std::vector<std::pair<std::string, Value>> shadows = ShadowFields();
	for (auto& [fieldName, fieldValue] : shadows)
		if (fieldName == "castShadows")
			fieldValue = Value::Bool(false);
	fields.insert(fields.end(), shadows.begin(), shadows.end());
	return Value::Object(std::move(fields));
}

Value DefaultAudio()
{
	return Value::Object({
		{ "source", GuidReference() },
		{ "occlusionEnabled", Value::Bool(false) },
		{ "occlusionGain", Value::Float(0.45) },
		{ "occlusionHighFrequencyGain", Value::Float(0.35) },
		{ "occlusionMaterial", Value::String("custom") },
		{ "occlusionMaterialThickness", Value::Float(1.0) },
		{ "occlusionAttack", Value::Float(0.08) },
		{ "occlusionRelease", Value::Float(0.18) },
		{ "occlusionQueryInterval", Value::Float(0.12) },
		{ "occlusionMaxDistance", Value::Float(100.0) },
		{ "occlusionMaxQueriesPerFrame", Value::Int(4) },
		{ "coneEnabled", Value::Bool(false) },
		{ "coneInnerAngle", Value::Float(360.0) },
		{ "coneOuterAngle", Value::Float(360.0) },
		{ "coneOuterGain", Value::Float(1.0) },
		{ "dopplerEnabled", Value::Bool(false) }
	});
}

Value DefaultAudioVolume()
{
	return Value::Object({
		{ "shape", Value::String("sphere") },
		{ "preset", Value::String("generic") },
		{ "presetAsset", GuidReference() },
		{ "radius", Value::Float(8.0) },
		{ "halfExtents", Vec3(4.0, 4.0, 4.0) },
		{ "fadeDistance", Value::Float(2.0) },
		{ "wetGain", Value::Float(0.6) },
		{ "priority", Value::Int(0) },
		{ "overridePresetParameters", Value::Bool(false) },
		{ "density", Value::Float(1.0) },
		{ "diffusion", Value::Float(1.0) },
		{ "gain", Value::Float(0.32) },
		{ "gainHF", Value::Float(0.89) },
		{ "decayTime", Value::Float(1.49) }
	});
}

Value DefaultLocalVolumetricFog()
{
	return VansSceneEntityFactory::BuildLocalVolumetricFogComponentData(
		VansSceneLocalVolumetricFogComponentConfig{});
}

Value DefaultVideo()
{
	return Value::Object({ { "source", GuidReference() } });
}

Value DefaultParticle()
{
	return Value::Object({
		{ "asset", GuidReference() },
		{ "play_on_awake", Value::Bool(true) }
	});
}

Value DefaultCloth()
{
	return Value::Object({
		{ "profile", GuidReference() },
		{ "physicsAttachOffsetY", Value::Float(0.0) }
	});
}

Value DefaultVehicle()
{
	return Value::Object({
		{ "bodyObject", Value::String("") },
		{ "tireObjects", Value::Array({}) }
	});
}

Value DefaultActionHost()
{
	return VansGameplayActionHostAuthoring::CreateDefaultData();
}

Value DefaultScript()
{
	return Value::Object({
		{ "language", Value::String("lua") },
		{ "path", Value::String("Scripts/") },
		{ "entry", Value::String("") },
		{ "fields", Value::Object({}) }
	});
}
}

const std::array<VansComponentTypeDescriptor, 27>& VansComponentTypeCatalog::All()
{
	static const std::array<VansComponentTypeDescriptor, 27> descriptors = []
	{
		std::array<VansComponentTypeDescriptor, 27> result{{
		{ "Transform", "transform", VansRuntimeComponentType_Transform },
		{ "ModelRenderer", "render", VansRuntimeComponentType_Render,
			VansComponentTypeTrait::None, true, true, true, &DefaultModelRenderer, { "render" } },
		{ "LODGroup", "lod_group", VansInvalidComponentTypeId,
			VansComponentTypeTrait::None, true, true, true, &DefaultLodGroup, { "lodgroup" } },
		{ "Physics", "physics", VansRuntimeComponentType_Physics,
			VansComponentTypeTrait::None, true, true, true, &DefaultPhysics },
		{ "Camera", "camera", VansRuntimeComponentType_Camera,
			VansComponentTypeTrait::CameraMedia, true, true, false, &DefaultCamera },
		{ "Animation", "animation", VansRuntimeComponentType_Animation,
			VansComponentTypeTrait::Animation, true, true, false, &DefaultAnimation, { "animator" } },
		{ "CharacterController", "charController", VansRuntimeComponentType_CharacterController,
			VansComponentTypeTrait::None, true, true, false, &DefaultCharacterController, { "charcontroller" } },
		{ "DirectionalLight", "directional_light", VansRuntimeComponentType_DirectionalLight,
			VansComponentTypeTrait::Light, true, true, false, &DefaultDirectionalLight },
		{ "PointLight", "point_light", VansRuntimeComponentType_PointLight,
			VansComponentTypeTrait::Light, true, true, false, &DefaultPointLight },
		{ "SpotLight", "spot_light", VansRuntimeComponentType_SpotLight,
			VansComponentTypeTrait::Light, true, true, false, &DefaultSpotLight },
		{ "RectLight", "rect_light", VansRuntimeComponentType_RectLight,
			VansComponentTypeTrait::Light, true, true, false, &DefaultRectLight },
		{ "Audio", "audio", VansRuntimeComponentType_Audio,
			VansComponentTypeTrait::CameraMedia, true, true, false, &DefaultAudio },
		{ "AudioVolume", "audio_volume", VansRuntimeComponentType_AudioVolume,
			VansComponentTypeTrait::None, true, true, false, &DefaultAudioVolume },
		{ "AudioReverbZone", "audio_reverb_zone", VansRuntimeComponentType_AudioReverbZone,
			VansComponentTypeTrait::None, true, true, false, &DefaultAudioVolume },
		{ "LocalVolumetricFog", "localvolumetricfog", VansInvalidComponentTypeId,
			VansComponentTypeTrait::None, true, true, false, &DefaultLocalVolumetricFog },
		{ "Video", "video", VansRuntimeComponentType_Video,
			VansComponentTypeTrait::CameraMedia, true, true, false, &DefaultVideo },
		{ "Particle", "particle", VansRuntimeComponentType_Particle,
			VansComponentTypeTrait::Particle, true, true, false, &DefaultParticle },
		{ "Cloth", "cloth", VansRuntimeComponentType_Cloth,
			VansComponentTypeTrait::None, true, true, false, &DefaultCloth },
		{ "Vehicle", "vehicle", VansRuntimeComponentType_Vehicle,
			VansComponentTypeTrait::None, true, true, false, &DefaultVehicle },
		{ "ActionHost", "action_host", VansRuntimeComponentType_ActionHost,
			VansComponentTypeTrait::None, true, true, true, &DefaultActionHost,
			{ "gaf" }, "GameplayActionHost", { "gaf" } },
		{ "Script", "script", VansRuntimeComponentType_Script,
			VansComponentTypeTrait::None, true, true, false, &DefaultScript,
			{ "luascript" }, {}, { "luascript" } },
		{ "MultiMeshRoot", "multimeshroot", VansInvalidComponentTypeId,
			VansComponentTypeTrait::MultiMeshRoot },
		{ "Timeline", "timeline", VansRuntimeComponentType_Timeline },
		{ "NavigationAgent", "navigation_agent", VansRuntimeComponentType_NavigationAgent },
		{ "AIAgent", "ai_agent", VansRuntimeComponentType_AIAgent },
		{ "UIController", "ui", VansRuntimeComponentType_UI,
			VansComponentTypeTrait::None, true, false, false, nullptr,
			{ "uicontroller" }, {}, { "uicontroller" } },
		{ "Ragdoll", "ragdoll", VansRuntimeComponentType_Ragdoll,
			VansComponentTypeTrait::None, false },
		}};

		auto setAssetRules = [&](std::string_view authoringType,
			std::initializer_list<VansComponentAssetReferenceRule> rules)
		{
			auto descriptor = std::find_if(result.begin(), result.end(),
				[&](const VansComponentTypeDescriptor& candidate)
				{ return candidate.authoringType == authoringType; });
			if (descriptor == result.end() || rules.size() > descriptor->assetReferenceRules.size())
				return;
			descriptor->assetReferenceRuleCount = static_cast<std::uint8_t>(rules.size());
			std::copy(rules.begin(), rules.end(), descriptor->assetReferenceRules.begin());
		};

		using Storage = VansComponentAssetReferenceStorage;
		setAssetRules("DirectionalLight", {
			{ "cookie", "texture", "Texture", Storage::GuidObject } });
		setAssetRules("PointLight", {
			{ "cookie", "texture", "Texture", Storage::GuidObject },
			{ "data", "ies_profile_guid", "IESProfile", Storage::GuidString } });
		setAssetRules("SpotLight", {
			{ "cookie", "texture", "Texture", Storage::GuidObject },
			{ "data", "ies_profile_guid", "IESProfile", Storage::GuidString } });
		setAssetRules("RectLight", {
			{ "cookie", "texture", "Texture", Storage::GuidObject },
			{ "data", "emissive_texture_guid", "Texture", Storage::GuidString } });
		setAssetRules("ModelRenderer", {
			{ "data", "model", "Model", Storage::GuidObject },
			{ "materialoverrides", "", "Material", Storage::GuidObject } });
		setAssetRules("Audio", {
			{ "data", "source", "Audio", Storage::GuidObject } });
		setAssetRules("AudioVolume", {
			{ "data", "presetasset", "AudioReverbPreset", Storage::GuidObject } });
		setAssetRules("AudioReverbZone", {
			{ "data", "presetasset", "AudioReverbPreset", Storage::GuidObject } });
		setAssetRules("Video", {
			{ "data", "source", "Video", Storage::GuidObject } });
		setAssetRules("Timeline", {
			{ "data", "timeline", "Timeline", Storage::GuidObject } });
		setAssetRules("UIController", {
			{ "preload", "", "UIScreen", Storage::GuidObject },
			{ "autoopen", "", "UIScreen", Storage::GuidObject } });
		setAssetRules("LocalVolumetricFog", {
			{ "source", "asset", "Texture", Storage::GuidObject } });
		setAssetRules("Particle", {
			{ "data", "asset", "Particle", Storage::GuidObject } });
		setAssetRules("Animation", {
			{ "data", "animator", "AnimatorController", Storage::GuidString },
			{ "ragdoll", "profile", "RagdollProfile", Storage::GuidString } });
		setAssetRules("Cloth", {
			{ "data", "profile", "ClothProfile", Storage::GuidObject } });
		setAssetRules("ActionHost", {
			{ "actionsets", "", "ActionSet", Storage::GuidObject },
			{ "autoactivate", "", "ActionDefinition", Storage::GuidObject },
			{ "", "action", "ActionDefinition", Storage::GuidObject } });
		return result;
	}();
	return descriptors;
}

VansSerializedValue VansComponentTypeCatalog::CreateDefaultData(std::string_view name)
{
	const VansComponentTypeDescriptor* descriptor = Find(name);
	return descriptor && descriptor->defaultDataFactory
		? descriptor->defaultDataFactory()
		: VansSerializedValue::Object({});
}

const VansComponentAssetReferenceRule* VansComponentTypeCatalog::FindAssetReferenceRule(
	std::string_view authoringType,
	std::string_view parentKey,
	std::string_view fieldKey)
{
	const auto descriptor = std::find_if(All().begin(), All().end(),
		[&](const VansComponentTypeDescriptor& candidate)
		{ return EqualsIgnoreCase(authoringType, candidate.authoringType); });
	if (descriptor == All().end()) return nullptr;
	for (std::uint8_t index = 0; index < descriptor->assetReferenceRuleCount; ++index)
	{
		const VansComponentAssetReferenceRule& rule = descriptor->assetReferenceRules[index];
		if ((rule.parentKey.empty() || parentKey == rule.parentKey) &&
			(rule.fieldKey.empty() || fieldKey == rule.fieldKey))
			return &rule;
	}
	return nullptr;
}
}
