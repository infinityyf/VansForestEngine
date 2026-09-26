#include "VansSceneAnimationComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "Serialization/VansMotionMatchingConfigCodec.h"

#include <unordered_set>
#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AssetCore/VansAssetGuid.h"

namespace Vans
{
namespace
{
using VansGraphics::MotionMatchingSettings;

const VansSerializedValue* ReadObjectField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Object ? field : nullptr;
}

const VansSerializedValue* ReadArrayField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Array ? field : nullptr;
}

float ReadFloatField(const VansSerializedValue& object, const char* key, float fallback)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && (field->kind == VansSerializedValue::Kind::Float ||
		field->kind == VansSerializedValue::Kind::Int)
		? static_cast<float>(ReadSerializedNumber(*field))
		: fallback;
}

bool ReadBoolField(const VansSerializedValue& object, const char* key, bool fallback)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Bool ? field->boolValue : fallback;
}

std::string ReadAssetReferenceField(
	const VansSerializedValue& object,
	const char* key,
	const std::string& fallback)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Object
		? ReadSerializedStringField(*field, "guid", fallback)
		: fallback;
}

VansSceneAnimationRetargetConfig DecodeRetarget(const VansSerializedValue& retargetJson)
{
	VansSceneAnimationRetargetConfig config;
	config.enabled = ReadBoolField(retargetJson, "enabled", false);
	config.profileGuid = ReadAssetReferenceField(retargetJson, "profile", "");
	config.sourceModelGuid = ReadAssetReferenceField(retargetJson, "source_model", "");
	config.sourceAnimatorGuid = ReadAssetReferenceField(retargetJson, "source_animator", "");
	config.debugDraw = ReadBoolField(retargetJson, "debug_draw", false);
	return config;
}

VansSceneRagdollComponentConfig DecodeRagdollConfig(const VansSerializedValue& ragdollJson)
{
	VansSceneRagdollComponentConfig config;
	config.profileGuid = ReadAssetReferenceField(ragdollJson, "profile", "");
	config.driveMode = ReadSerializedStringField(ragdollJson, "drive_mode", "animation");
	config.blendWeight = ReadFloatField(ragdollJson, "blend_weight", 0.0f);
	return config;
}

const VansSerializedValue* FindAuthoringComponent(const VansSerializedValue& entity, const char* type)
{
	const VansSerializedValue* components = ReadArrayField(entity, "components");
	if (!components)
		return nullptr;

	for (const VansSerializedValue& component : components->arrayItems)
	{
		if (ReadSerializedStringField(component, "type") == type)
			return &component;
	}
	return nullptr;
}
}

std::optional<VansSceneAnimationComponentConfig>
VansSceneAnimationComponentReader::ReadFromComponents(const VansSerializedValue& components)
{
	const VansSerializedValue* animation = ReadObjectField(components, "animation");
	if (!animation)
		return std::nullopt;
	return ReadAnimation(*animation);
}

std::optional<VansSceneAnimationComponentConfig>
VansSceneAnimationComponentReader::ReadFromAuthoringEntity(const VansSerializedValue& entity)
{
	const VansSerializedValue* animationComponent = FindAuthoringComponent(entity, "Animation");
	if (!animationComponent)
		return std::nullopt;
	return ReadAuthoringAnimationComponent(*animationComponent);
}

VansSceneAnimationComponentConfig VansSceneAnimationComponentReader::ReadAuthoringAnimationComponent(
	const VansSerializedValue& animationComponent)
{
	VansSceneAnimationComponentConfig config;
	if (const VansSerializedValue* data = ReadObjectField(animationComponent, "data"))
		config = ReadAnimation(*data);
	else
		config.valid = true;

	config.enabled = ReadSerializedBoolField(animationComponent, "enabled", true);
	return config;
}

VansSceneAnimationComponentConfig VansSceneAnimationComponentReader::ReadAnimation(
	const VansSerializedValue& animationNode)
{
	VansSceneAnimationComponentConfig config;
	if (animationNode.kind != VansSerializedValue::Kind::Object)
		return config;

	config.valid = true;
	config.enabled = ReadBoolField(animationNode, "enabled", true);
	config.meshGroup = ReadSerializedStringField(animationNode, "mesh_group", "");
	config.animatorGuid = ReadAssetReferenceField(animationNode, "animator", "");
	config.rigGuid = ReadAssetReferenceField(animationNode, "rig", "");
	if (const auto* bindings = FindObjectField(animationNode, "targetBindings"))
	{
		if (bindings->kind != VansSerializedValue::Kind::Array) { config.valid = false; return config; }
		std::unordered_set<std::string> ids;
		for (const auto& item : bindings->arrayItems)
		{
			VansGraphics::VansAnimationTargetBinding binding;
			binding.id = ReadSerializedStringField(item, "id");
			const auto* target = FindObjectField(item, "target");
			SerializedObjectReferenceValue reference;
			VansAssetGuid guid;
			if (binding.id.empty() || !ids.insert(binding.id).second || !target ||
				!TryReadSerializedObjectReference(*target, reference) || reference.domain != "SceneEntity" ||
				!VansAssetGuid::TryParse(reference.entityGuid, guid))
			{ config.valid = false; return config; }
			binding.targetEntityGuid = guid.ToString();
			config.targetBindings.push_back(std::move(binding));
		}
	}
	config.externClips = ReadSerializedStringField(animationNode, "extern_clips", "");
	config.rootMotion = ReadBoolField(animationNode, "root_motion", false);
	config.normalizeRootPose = ReadBoolField(animationNode, "normalize_root_pose", true);
	config.autoPlay = ReadBoolField(animationNode, "auto_play", true);
	config.loop = ReadBoolField(animationNode, "loop", true);
	config.rootBone = ReadSerializedStringField(animationNode, "root_bone", "");
	config.name = ReadSerializedStringField(animationNode, "name", "");
	if (const VansSerializedValue* motionModel = FindObjectField(animationNode, "motion_model"))
	{
		VansCharacterMotionSettings settings;
		std::string error;
		if (motionModel->kind != VansSerializedValue::Kind::Object ||
			!VansMotionMatchingConfigCodec::DecodeMotionModel(*motionModel, settings, error))
		{
			config.valid = false;
			return config;
		}
		config.motionModel = settings;
	}

	if (const VansSerializedValue* motionMatchingField = FindObjectField(animationNode, "motion_matching"))
	{
		if (motionMatchingField->kind != VansSerializedValue::Kind::Object)
		{
			config.valid = false;
			return config;
		}
		MotionMatchingSettings settings;
		std::string error;
		if (!VansMotionMatchingConfigCodec::Decode(*motionMatchingField, settings, error))
		{
			config.valid = false;
			return config;
		}
		config.motionMatching = std::move(settings);
	}
	if (const VansSerializedValue* retarget = ReadObjectField(animationNode, "retarget"))
		config.retarget = DecodeRetarget(*retarget);
	if (const VansSerializedValue* ragdoll = ReadObjectField(animationNode, "ragdoll"))
		config.ragdoll = DecodeRagdollConfig(*ragdoll);

	return config;
}

VansSceneRagdollComponentConfig VansSceneAnimationComponentReader::ReadRagdoll(
	const VansSerializedValue& ragdollNode)
{
	if (ragdollNode.kind != VansSerializedValue::Kind::Object)
		return {};
	return DecodeRagdollConfig(ragdollNode);
}
}
