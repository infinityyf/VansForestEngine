#include "VansScene.h"

#include "VansCamera.h"
#include "Storage/VansPostProcessProfileStorage.h"
#include "VulkanCore/VansVideoTexture.h"
#include "../AnimationCore/VansAnimationClip.h"
#include "../AnimationCore/Storage/VansBoneMaskStorage.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AssetCore/VansAssetResolver.h"
#include "../AudioCore/VansAudioSourceBinding.h"
#include "../EventCore/VansEventBus.h"
#include "../ParticleCore/VansParticleRuntime.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../RuntimeUI/Public/VansUIActionBus.h"
#include "../RuntimeUI/Public/VansUIElementHandle.h"
#include "../RuntimeUI/Public/VansUIScreen.h"
#include "../RuntimeUI/Public/VansUIScreenManager.h"
#include "../RuntimeUI/Public/VansUISystem.h"
#include "../RuntimeUI/Public/VansUIViewModel.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../TimelineCore/VansTimelineCompiler.h"
#include "../TimelineCore/VansTimelineSerialization.h"
#include "../TimelineRuntime/VansTimelineEvents.h"
#include "../TimelineRuntime/VansTimelinePropertyRegistry.h"
#include "../TimelineRuntime/VansTimelineRuntimeSystem.h"
#include "../Util/VansLog.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>

namespace
{
std::string LowerAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
	{
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

template <typename T>
T* GetRuntimePayload(Vans::VansRuntimeWorld* world, Vans::VansComponentHandle component, std::uint16_t typeId)
{
	if (!world || component.typeId != typeId)
		return nullptr;
	auto* storage = static_cast<Vans::VansComponentStorage<T>*>(world->FindStorage(typeId));
	return storage ? storage->Get(component) : nullptr;
}

Vans::VansComponentHandle ResolveComponent(
	Vans::VansRuntimeWorld* world,
	const Vans::VansResolvedTimelineTarget& target,
	std::uint16_t typeId)
{
	if (!world)
		return {};
	if (target.component.IsValid() && target.component.typeId == typeId && world->GetComponentHeader(target.component))
		return target.component;
	if (!target.entity.IsValid() || !world->IsAlive(target.entity))
		return {};
	for (Vans::VansComponentHandle component : world->CollectComponentsOwnedBy(target.entity))
		if (component.typeId == typeId)
			return component;
	return {};
}

std::uint32_t ResolveTransformId(Vans::VansRuntimeWorld* world, Vans::VansEntityHandle entity)
{
	if (!world || !world->IsAlive(entity))
		return UINT32_MAX;
	for (Vans::VansComponentHandle component : world->CollectComponentsOwnedBy(entity))
	{
		if (auto* transform = GetRuntimePayload<Vans::VansRuntimeTransformComponent>(
			world, component, Vans::VansRuntimeComponentType_Transform))
			return transform->transformStoreId;
	}
	return UINT32_MAX;
}

double NumberValue(const Vans::VansTimelineKeyValue& value, double fallback = 0.0)
{
	return std::visit([fallback](const auto& typed) -> double
	{
		using T = std::decay_t<decltype(typed)>;
		if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> ||
			std::is_same_v<T, float> || std::is_same_v<T, double>)
			return static_cast<double>(typed);
		return fallback;
	}, value);
}

bool BoolValue(const Vans::VansTimelineKeyValue& value, bool fallback = false)
{
	if (const auto* typed = std::get_if<bool>(&value))
		return *typed;
	return NumberValue(value, fallback ? 1.0 : 0.0) != 0.0;
}

glm::vec3 ToVec3(const Vans::VansTimelineVec3& value)
{
	return glm::vec3(
		static_cast<float>(value.value[0]),
		static_cast<float>(value.value[1]),
		static_cast<float>(value.value[2]));
}

glm::quat ToQuat(const Vans::VansTimelineQuaternion& value)
{
	glm::quat result(
		static_cast<float>(value.value[3]),
		static_cast<float>(value.value[0]),
		static_cast<float>(value.value[1]),
		static_cast<float>(value.value[2]));
	return glm::length(result) > 0.00001f ? glm::normalize(result) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

bool NearlyEqual(const glm::vec3& left, const glm::vec3& right, float tolerance = 0.0005f)
{
	return std::abs(left.x - right.x) <= tolerance &&
		std::abs(left.y - right.y) <= tolerance &&
		std::abs(left.z - right.z) <= tolerance;
}

void DecomposeTransform(const glm::mat4& matrix, glm::vec3& position, glm::quat& rotation, glm::vec3& scale)
{
	glm::vec3 skew;
	glm::vec4 perspective;
	if (!glm::decompose(matrix, scale, rotation, position, skew, perspective))
	{
		position = glm::vec3(matrix[3]);
		rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		scale = glm::vec3(1.0f);
	}
	rotation = glm::normalize(rotation);
}

int FindBoneIndex(const Skeleton& skeleton, const std::string& stableNameOrId)
{
	if (const auto found = skeleton.boneNameToIndex.find(stableNameOrId); found != skeleton.boneNameToIndex.end())
		return found->second;
	try
	{
		const int stableId = std::stoi(stableNameOrId);
		const auto found = std::find_if(skeleton.bones.begin(), skeleton.bones.end(),
			[&](const BoneInfo& bone) { return bone.id == stableId; });
		return found == skeleton.bones.end() ? -1 : static_cast<int>(found - skeleton.bones.begin());
	}
	catch (...) { return -1; }
}

glm::vec3 ColorValue(const Vans::VansTimelineKeyValue& value, const glm::vec3& fallback)
{
	if (const auto* color = std::get_if<Vans::VansTimelineColorLinear>(&value))
		return glm::vec3(color->value[0], color->value[1], color->value[2]);
	if (const auto* color = std::get_if<Vans::VansTimelineColorSrgb>(&value))
	{
		const glm::vec3 srgb(color->value[0], color->value[1], color->value[2]);
		return glm::pow(glm::max(srgb, glm::vec3(0.0f)), glm::vec3(2.2f));
	}
	if (const auto* vector = std::get_if<Vans::VansTimelineVec3>(&value))
		return ToVec3(*vector);
	return fallback;
}

glm::vec3 TemperatureColor(double kelvin)
{
	const double temperature = std::clamp(kelvin, 1000.0, 40000.0) / 100.0;
	double red = 255.0;
	double green = temperature <= 66.0
		? 99.4708025861 * std::log(temperature) - 161.1195681661
		: 288.1221695283 * std::pow(temperature - 60.0, -0.0755148492);
	double blue = temperature >= 66.0 ? 255.0 : (temperature <= 19.0
		? 0.0 : 138.5177312231 * std::log(temperature - 10.0) - 305.0447927307);
	return glm::pow(glm::vec3(
		static_cast<float>(std::clamp(red, 0.0, 255.0) / 255.0),
		static_cast<float>(std::clamp(green, 0.0, 255.0) / 255.0),
		static_cast<float>(std::clamp(blue, 0.0, 255.0) / 255.0)), glm::vec3(2.2f));
}

VansGraphics::VansPostProcessProfile BlendPostProcessProfile(
	const VansGraphics::VansPostProcessProfile& base,
	const VansGraphics::VansPostProcessProfile& target,
	float weight)
{
	const float alpha = std::clamp(weight, 0.0f, 1.0f);
	auto blendFloat = [alpha](float from, float to) { return from + (to - from) * alpha; };
	auto blendInt = [alpha](std::int32_t from, std::int32_t to)
	{
		return alpha < 0.5f ? from : to;
	};
	auto blendBool = [alpha](bool from, bool to) { return alpha < 0.5f ? from : to; };

	VansGraphics::VansPostProcessProfile result = base;
	result.m_EnablePostProcess = blendBool(base.m_EnablePostProcess, target.m_EnablePostProcess);
	result.m_EnableHDR = blendBool(base.m_EnableHDR, target.m_EnableHDR);
	result.m_EnableAutoExposure = blendBool(base.m_EnableAutoExposure, target.m_EnableAutoExposure);
	result.m_ExposureCompensation = blendFloat(base.m_ExposureCompensation, target.m_ExposureCompensation);
	result.m_MinEV100 = blendFloat(base.m_MinEV100, target.m_MinEV100);
	result.m_MaxEV100 = blendFloat(base.m_MaxEV100, target.m_MaxEV100);
	result.m_AdaptationSpeedUp = blendFloat(base.m_AdaptationSpeedUp, target.m_AdaptationSpeedUp);
	result.m_AdaptationSpeedDown = blendFloat(base.m_AdaptationSpeedDown, target.m_AdaptationSpeedDown);
	result.m_EnableBloom = blendBool(base.m_EnableBloom, target.m_EnableBloom);
	result.m_BloomThreshold = blendFloat(base.m_BloomThreshold, target.m_BloomThreshold);
	result.m_BloomKnee = blendFloat(base.m_BloomKnee, target.m_BloomKnee);
	result.m_BloomIntensity = blendFloat(base.m_BloomIntensity, target.m_BloomIntensity);
	result.m_BloomScatter = blendFloat(base.m_BloomScatter, target.m_BloomScatter);
	result.m_BloomClamp = blendFloat(base.m_BloomClamp, target.m_BloomClamp);
	result.m_ToneMapperType = blendInt(base.m_ToneMapperType, target.m_ToneMapperType);
	result.m_WhitePoint = blendFloat(base.m_WhitePoint, target.m_WhitePoint);
	result.m_EnableColorGrading = blendBool(base.m_EnableColorGrading, target.m_EnableColorGrading);
	result.m_Contrast = blendFloat(base.m_Contrast, target.m_Contrast);
	result.m_Saturation = blendFloat(base.m_Saturation, target.m_Saturation);
	result.m_HueShift = blendFloat(base.m_HueShift, target.m_HueShift);
	result.m_Temperature = blendFloat(base.m_Temperature, target.m_Temperature);
	result.m_Tint = blendFloat(base.m_Tint, target.m_Tint);
	result.m_EnableDOF = blendBool(base.m_EnableDOF, target.m_EnableDOF);
	result.m_FocusDistance = blendFloat(base.m_FocusDistance, target.m_FocusDistance);
	result.m_FocusRange = blendFloat(base.m_FocusRange, target.m_FocusRange);
	result.m_Aperture = blendFloat(base.m_Aperture, target.m_Aperture);
	result.m_MaxCoC = blendFloat(base.m_MaxCoC, target.m_MaxCoC);
	result.m_EnableMotionBlur = blendBool(base.m_EnableMotionBlur, target.m_EnableMotionBlur);
	result.m_ShutterScale = blendFloat(base.m_ShutterScale, target.m_ShutterScale);
	result.m_MotionBlurSamples = blendInt(base.m_MotionBlurSamples, target.m_MotionBlurSamples);
	result.m_EnableChromaticAberration = blendBool(
		base.m_EnableChromaticAberration, target.m_EnableChromaticAberration);
	result.m_ChromaticAberrationIntensity = blendFloat(
		base.m_ChromaticAberrationIntensity, target.m_ChromaticAberrationIntensity);
	result.m_EnableSharpen = blendBool(base.m_EnableSharpen, target.m_EnableSharpen);
	result.m_SharpenIntensity = blendFloat(base.m_SharpenIntensity, target.m_SharpenIntensity);
	result.m_IsDirty = true;
	return result;
}

Vans::VansComponentHandle ResolveLightComponent(
	Vans::VansRuntimeWorld* world,
	const Vans::VansResolvedTimelineTarget& target)
{
	constexpr std::uint16_t lightTypes[] = {
		Vans::VansRuntimeComponentType_DirectionalLight,
		Vans::VansRuntimeComponentType_PointLight,
		Vans::VansRuntimeComponentType_SpotLight,
		Vans::VansRuntimeComponentType_RectLight
	};
	for (std::uint16_t typeId : lightTypes)
	{
		const Vans::VansComponentHandle component = ResolveComponent(world, target, typeId);
		if (component.IsValid()) return component;
	}
	return {};
}

VansRuntime::VansUIVariant ToUIVariant(const Vans::VansTimelineKeyValue& value)
{
	return std::visit([](const auto& typed) -> VansRuntime::VansUIVariant
	{
		using T = std::decay_t<decltype(typed)>;
		if constexpr (std::is_same_v<T, bool>) return VansRuntime::VansUIVariant(typed);
		else if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t>)
			return VansRuntime::VansUIVariant(static_cast<std::int64_t>(typed));
		else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
			return VansRuntime::VansUIVariant(static_cast<double>(typed));
		else if constexpr (std::is_same_v<T, std::string>) return VansRuntime::VansUIVariant(typed);
		else return VansRuntime::VansUIVariant();
	}, value);
}

std::string UIStringValue(const Vans::VansTimelineKeyValue& value)
{
	return std::visit([](const auto& typed) -> std::string
	{
		using T = std::decay_t<decltype(typed)>;
		if constexpr (std::is_same_v<T, bool>) return typed ? "true" : "false";
		else if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> ||
			std::is_same_v<T, float> || std::is_same_v<T, double>) return std::to_string(typed);
		else if constexpr (std::is_same_v<T, std::string>) return typed;
		else return {};
	}, value);
}

const char* UIElementProperty(std::uint32_t setterId, const std::string& descriptorId)
{
	struct Entry { std::uint32_t id; const char* descriptor; const char* property; };
	static constexpr Entry entries[] = {
		{ 1, "Element.Text", "text" }, { 2, "Element.Visible", "visible" },
		{ 3, "Element.Opacity", "opacity" }, { 4, "Element.Left", "left" },
		{ 5, "Element.Top", "top" }, { 6, "Element.Width", "width" },
		{ 7, "Element.Height", "height" }, { 8, "Element.TranslateX", "translateX" },
		{ 9, "Element.TranslateY", "translateY" }
	};
	for (const Entry& entry : entries)
		if (entry.id == setterId && descriptorId == entry.descriptor) return entry.property;
	return nullptr;
}

std::string DiagnosticsText(const Vans::VansTimelineDiagnostics& diagnostics)
{
	std::ostringstream stream;
	for (const auto& diagnostic : diagnostics)
	{
		if (diagnostic.severity != Vans::VansTimelineDiagnosticSeverity::Error)
			continue;
		if (stream.tellp() > 0)
			stream << "; ";
		stream << diagnostic.objectId << "." << diagnostic.propertyPath << ": " << diagnostic.message;
	}
	return stream.str();
}

Vans::VansEventLane EventLane(const std::string& value)
{
	if (value == "Script") return Vans::VansEventLane::Script;
	if (value == "GameLogic") return Vans::VansEventLane::GameLogic;
	if (value == "Diagnostics") return Vans::VansEventLane::Diagnostics;
	return Vans::VansEventLane::MainThread;
}

struct TimelineSceneAdapterState
{
	struct AnimationPlayback
	{
		Vans::VansComponentHandle component;
		VansGraphics::VansSlotPlaybackHandle handle;
		bool previousRootMotionEnabled = false;
		bool previousRootMotionApplyToOwner = true;
	};

	std::unordered_map<std::string, AnimationPlayback> animations;
	struct ConstraintOffset
	{
		VansGraphics::VansTransform value;
	};
	std::unordered_map<std::string, ConstraintOffset> constraintOffsets;
	std::unordered_map<std::string, std::unique_ptr<VansEngine::VansAudioSourceBinding>> audioInstances;
	struct CameraCutState
	{
		VansGraphics::VansCamera* previousCamera = nullptr;
		VansGraphics::VansCamera* drivenCamera = nullptr;
		VansGraphics::VansTransform previousTransform;
		bool previousHasTransform = false;
		bool transformAltered = false;
		float previousFov = 45.0f;
		float previousNear = 0.01f;
		float previousFar = 10000.0f;
		float previousAspect = 1.0f;
	};
	std::unordered_map<std::string, CameraCutState> cameraCuts;
	struct CameraShakeState
	{
		std::uint32_t transformId = std::numeric_limits<std::uint32_t>::max();
		bool applied = false;
		glm::vec3 basePosition{ 0.0f };
		glm::vec3 baseRotation{ 0.0f };
		glm::vec3 appliedPosition{ 0.0f };
		glm::vec3 appliedRotation{ 0.0f };
	};
	std::unordered_map<std::string, CameraShakeState> cameraShakes;
	struct VirtualCameraState
	{
		bool hasFov = false;
		bool hasNearClip = false;
		bool hasFarClip = false;
		bool hasAspect = false;
		float fov = 45.0f;
		float nearClip = 0.01f;
		float farClip = 10000.0f;
		float aspect = 1.0f;
	};
	std::unordered_map<std::string, VirtualCameraState> virtualCameras;
	std::unordered_map<std::string, VansGraphics::VansPostProcessProfile> postProcessBase;
	std::unordered_map<std::string, VansGraphics::VansPostProcessProfile> postProcessProfiles;
	struct MaterialInstanceBinding
	{
		VansGraphics::VansRenderNode* node = nullptr;
		VansGraphics::VansMaterial* source = nullptr;
		VansGraphics::VansMaterial* instance = nullptr;
		std::unordered_map<std::string, bool> writers;
	};
	std::unordered_map<std::string, MaterialInstanceBinding> materialInstances;
};

std::string RuntimeKey(
	const Vans::VansTimelineApplyContext& context,
	const Vans::VansResolvedTimelineTarget& target)
{
	return context.writerId + ":" + context.propertyKey + ":" +
		std::to_string(target.entity.index) + ":" + std::to_string(target.entity.generation);
}

std::string TimelineTargetKey(const Vans::VansResolvedTimelineTarget& target)
{
	if (target.component.IsValid())
		return "component:" + std::to_string(target.component.typeId) + ":" +
			std::to_string(target.component.index) + ":" + std::to_string(target.component.generation);
	if (target.entity.IsValid())
		return "entity:" + std::to_string(target.entity.index) + ":" + std::to_string(target.entity.generation);
	if (!target.bindingId.empty())
		return "binding:" + target.bindingId;
	return "global";
}

VansGraphics::VansCamera* ResolveTimelineCamera(
	Vans::VansRuntimeWorld* world,
	const Vans::VansResolvedTimelineTarget& target)
{
	const Vans::VansComponentHandle component = ResolveComponent(world, target, Vans::VansRuntimeComponentType_Camera);
	auto* runtime = GetRuntimePayload<Vans::VansRuntimeCameraComponent>(
		world, component, Vans::VansRuntimeComponentType_Camera);
	return runtime ? runtime->camera : nullptr;
}

struct TimelineCameraPose
{
	VansGraphics::VansTransform transform;
	bool hasTransform = false;
	float fov = 45.0f;
	float nearClip = 0.01f;
	float farClip = 10000.0f;
	float aspect = 1.0f;
};

void CaptureCameraPose(VansGraphics::VansCamera* camera, TimelineCameraPose& pose)
{
	if (!camera)
		return;
	pose.fov = camera->GetFov();
	pose.nearClip = camera->GetNearClip();
	pose.farClip = camera->GetFarClip();
	pose.aspect = camera->GetAspectRatio();
	pose.hasTransform = camera->HasTransform() &&
		camera->GetTransformID() < VansTransformStore::GlobalTransforms.size();
	if (pose.hasTransform)
		pose.transform = VansTransformStore::GetTransform(camera->GetTransformID());
}

void ApplyCameraPose(VansGraphics::VansCamera* camera, const TimelineCameraPose& pose)
{
	if (!camera)
		return;
	if (pose.hasTransform && camera->HasTransform() &&
		camera->GetTransformID() < VansTransformStore::GlobalTransforms.size())
	{
		const auto transformId = camera->GetTransformID();
		VansTransformStore::GetTransform(transformId) = pose.transform;
		VansTransformStore::TransformIDToTransformDirty[transformId] = true;
	}
	camera->SetFov(pose.fov);
	camera->SetNearClip(pose.nearClip);
	camera->SetFarClip(pose.farClip);
	camera->SetAspectRatio(pose.aspect);
}

TimelineCameraPose BlendCameraPose(
	const TimelineCameraPose& source,
	const TimelineCameraPose& target,
	float alpha,
	bool matchAspect)
{
	TimelineCameraPose result = target;
	alpha = std::clamp(alpha, 0.0f, 1.0f);
	if (source.hasTransform && target.hasTransform)
	{
		result.hasTransform = true;
		result.transform.m_Position = glm::mix(source.transform.m_Position, target.transform.m_Position, alpha);
		const glm::quat sourceRotation = glm::quat(glm::radians(source.transform.m_Rotation));
		const glm::quat targetRotation = glm::quat(glm::radians(target.transform.m_Rotation));
		result.transform.m_Rotation = glm::degrees(glm::eulerAngles(glm::slerp(sourceRotation, targetRotation, alpha)));
		result.transform.m_Scale = glm::mix(source.transform.m_Scale, target.transform.m_Scale, alpha);
	}
	result.fov = glm::mix(source.fov, target.fov, alpha);
	result.nearClip = glm::mix(source.nearClip, target.nearClip, alpha);
	result.farClip = glm::mix(source.farClip, target.farClip, alpha);
	if (matchAspect)
		result.aspect = glm::mix(source.aspect, target.aspect, alpha);
	return result;
}

bool ResolveTimelineCameraPose(
	Vans::VansRuntimeWorld* world,
	const Vans::VansResolvedTimelineTarget& target,
	const TimelineSceneAdapterState& adapterState,
	VansGraphics::VansCamera* fallbackLensCamera,
	TimelineCameraPose& pose,
	std::string& error)
{
	if (VansGraphics::VansCamera* camera = ResolveTimelineCamera(world, target))
	{
		CaptureCameraPose(camera, pose);
		if (!pose.hasTransform)
		{
			error = "Camera Cut source camera requires a runtime Transform binding";
			return false;
		}
		return true;
	}

	const std::uint32_t transformId = ResolveTransformId(world, target.entity);
	if (transformId >= VansTransformStore::GlobalTransforms.size())
	{
		error = "Camera Cut virtual camera source requires a valid Transform binding";
		return false;
	}
	pose.transform = VansTransformStore::GetTransform(transformId);
	pose.hasTransform = true;
	if (fallbackLensCamera)
	{
		pose.fov = fallbackLensCamera->GetFov();
		pose.nearClip = fallbackLensCamera->GetNearClip();
		pose.farClip = fallbackLensCamera->GetFarClip();
		pose.aspect = fallbackLensCamera->GetAspectRatio();
	}
	const auto found = adapterState.virtualCameras.find(TimelineTargetKey(target));
	if (found != adapterState.virtualCameras.end())
	{
		const auto& lens = found->second;
		if (lens.hasFov) pose.fov = lens.fov;
		if (lens.hasNearClip) pose.nearClip = lens.nearClip;
		if (lens.hasFarClip) pose.farClip = lens.farClip;
		if (lens.hasAspect) pose.aspect = lens.aspect;
	}
	return true;
}

std::vector<VansGraphics::VansRenderNode*> ResolveRenderNodes(
	Vans::VansRuntimeWorld* world,
	const Vans::VansResolvedTimelineTarget& target,
	const std::string& slot)
{
	std::vector<VansGraphics::VansRenderNode*> result;
	const Vans::VansComponentHandle component = ResolveComponent(
		world, target, Vans::VansRuntimeComponentType_Render);
	auto* runtime = GetRuntimePayload<Vans::VansRuntimeRenderComponent>(
		world, component, Vans::VansRuntimeComponentType_Render);
	if (!runtime) return result;
	if (!runtime->renderNodes.empty()) result = runtime->renderNodes;
	else if (runtime->renderNode) result.push_back(runtime->renderNode);

	auto matches = [&](VansGraphics::VansRenderNode* node)
	{
		if (!node) return false;
		if (slot.empty() || slot == "default" || slot == "all" || slot == "*") return true;
		if (slot == "0" && node->m_SubmeshIndex == UINT32_MAX) return true;
		return node->m_SubmeshIndex != UINT32_MAX && slot == std::to_string(node->m_SubmeshIndex);
	};
	result.erase(std::remove_if(result.begin(), result.end(),
		[&](VansGraphics::VansRenderNode* node) { return !matches(node); }), result.end());
	return result;
}

VansGraphics::RenderNodeType RuntimeNodeTypeForMaterial(
	const VansGraphics::VansMaterial& material,
	VansGraphics::RenderNodeType fallback)
{
	using namespace VansGraphics;
	if (material.m_MaterialType == VAN_HAIR) return HAIR_NODE;
	if (material.m_MaterialType == VAN_TRANSPARENT || material.m_MaterialType == VAN_PBR_TRANSMISSION)
		return TRANSPARENT_NODE;
	if (material.m_MaterialType == VAN_CUSTOM_SHADER)
		return material.HasPass(VansPass::GBUFFER) ? OPAQUE_NODE
			: (material.m_CustomShaderDepthWrite ? FORWARD_OPAQUE_AFTER_DEFERRED_NODE : TRANSPARENT_NODE);
	if (fallback == DECAL_NODE || material.m_MaterialType == VAN_DECAL) return DECAL_NODE;
	return OPAQUE_NODE;
}

bool RuntimeNodeClassCompatible(
	const VansGraphics::VansRenderNode& node,
	VansGraphics::RenderNodeType targetType)
{
	using namespace VansGraphics;
	if ((node.GetNodeType() == DECAL_NODE) != (targetType == DECAL_NODE)) return false;
	return (node.GetNodeType() == TRANSPARENT_NODE) == (targetType == TRANSPARENT_NODE);
}

VansGraphics::VansMaterialParameterValue ToMaterialValue(const Vans::VansTimelineKeyValue& value)
{
	return std::visit([](const auto& typed) -> VansGraphics::VansMaterialParameterValue
	{
		using T = std::decay_t<decltype(typed)>;
		if constexpr (std::is_same_v<T, std::monostate>) return std::monostate{};
		else if constexpr (std::is_same_v<T, bool>) return typed;
		else if constexpr (std::is_same_v<T, std::int32_t>) return typed;
		else if constexpr (std::is_same_v<T, std::int64_t>)
			return static_cast<std::int32_t>(std::clamp<std::int64_t>(
				typed, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
		else if constexpr (std::is_same_v<T, float>) return typed;
		else if constexpr (std::is_same_v<T, double>) return static_cast<float>(typed);
		else if constexpr (std::is_same_v<T, std::string>) return typed;
		else if constexpr (std::is_same_v<T, Vans::VansTimelineVec2>)
			return glm::vec2(typed.value[0], typed.value[1]);
		else if constexpr (std::is_same_v<T, Vans::VansTimelineVec3>) return ToVec3(typed);
		else if constexpr (std::is_same_v<T, Vans::VansTimelineVec4> ||
			std::is_same_v<T, Vans::VansTimelineColorLinear> ||
			std::is_same_v<T, Vans::VansTimelineColorSrgb>)
			return glm::vec4(typed.value[0], typed.value[1], typed.value[2], typed.value[3]);
		else if constexpr (std::is_same_v<T, Vans::VansTimelineQuaternion>)
			return glm::vec4(typed.value[0], typed.value[1], typed.value[2], typed.value[3]);
		else if constexpr (std::is_same_v<T, Vans::VansTimelineObjectReference>) return typed.guid;
		else if constexpr (std::is_same_v<T, Vans::VansTimelineEventPayload>) return std::monostate{};
		else return std::monostate{};
	}, value);
}
}

void VansGraphics::VansScene::ConfigureTimelineRuntime()
{
	if (!m_RuntimeWorld)
		return;
	if (!m_TimelineRuntime)
		m_TimelineRuntime = std::make_unique<Vans::VansTimelineRuntimeSystem>();

	auto records = Vans::VansProjectManager::Get().EnumerateAssetRecords();
	const Vans::VansAssetAccessMode accessMode = m_UsingPackagedProjectAssets
		? Vans::VansAssetAccessMode::Package
		: Vans::VansAssetAccessMode::Editor;
	auto resolver = std::make_shared<Vans::VansAssetResolver>(accessMode, records);
	auto resolveCurrentTimeline = [accessMode](const std::string& assetGuid, std::uint64_t* revision)
	{
		const auto currentRecords = Vans::VansProjectManager::Get().EnumerateAssetRecords();
		if (revision)
		{
			const auto found = std::find_if(currentRecords.begin(), currentRecords.end(), [&](const auto& record)
			{
				return record.guid.ToString() == assetGuid;
			});
			*revision = found == currentRecords.end() ? 0 : found->generation;
		}
		return Vans::VansAssetResolver(accessMode, currentRecords).Resolve(
			assetGuid, Vans::VansAssetType::Timeline);
	};
	using TimelineCacheEntry = std::pair<std::uint64_t,
		std::weak_ptr<const Vans::VansCompiledTimeline>>;
	auto compiledCache = std::make_shared<std::unordered_map<std::string, TimelineCacheEntry>>();
	auto adapterState = std::make_shared<TimelineSceneAdapterState>();
	m_TimelinePropertyRegistry = std::make_shared<Vans::VansTimelinePropertyRegistry>();
	auto propertyRegistry = m_TimelinePropertyRegistry;
	Vans::VansRuntimeWorld* world = m_RuntimeWorld.get();
	auto registerProperty = [&](Vans::VansTimelineRuntimePropertyDescriptor descriptor)
	{
		std::string registrationError;
		if (!propertyRegistry->Register(std::move(descriptor), registrationError))
			VANS_LOG_ERROR("[Timeline] " << registrationError);
	};
	auto transformReader = [world](const Vans::VansResolvedTimelineTarget& target,
		Vans::VansTimelineKeyValue& value, std::string& error, const std::string& property)
	{
		const std::uint32_t transformId = ResolveTransformId(world, target.entity);
		if (transformId >= VansTransformStore::GlobalTransforms.size())
		{
			error = "Registered Transform property requires a valid Transform component";
			return false;
		}
		const VansTransform& transform = VansTransformStore::GetTransform(transformId);
		if (property == "position") value = Vans::VansTimelineVec3{ { transform.m_Position.x, transform.m_Position.y, transform.m_Position.z } };
		else if (property == "scale") value = Vans::VansTimelineVec3{ { transform.m_Scale.x, transform.m_Scale.y, transform.m_Scale.z } };
		else
		{
			const glm::quat rotation = glm::quat(glm::radians(transform.m_Rotation));
			value = Vans::VansTimelineQuaternion{ { rotation.x, rotation.y, rotation.z, rotation.w } };
		}
		return true;
	};
	auto transformWriter = [world](const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineKeyValue& value, std::string& error, const std::string& property)
	{
		const std::uint32_t transformId = ResolveTransformId(world, target.entity);
		if (transformId >= VansTransformStore::GlobalTransforms.size())
		{
			error = "Registered Transform property requires a valid Transform component";
			return false;
		}
		VansTransform& transform = VansTransformStore::GetTransform(transformId);
		if (property == "position" || property == "scale")
		{
			const auto* vector = std::get_if<Vans::VansTimelineVec3>(&value);
			if (!vector) { error = "Registered Transform vector property received the wrong value type"; return false; }
			(property == "position" ? transform.m_Position : transform.m_Scale) = ToVec3(*vector);
		}
		else
		{
			const auto* rotation = std::get_if<Vans::VansTimelineQuaternion>(&value);
			if (!rotation) { error = "Registered Transform rotation property received the wrong value type"; return false; }
			transform.m_Rotation = glm::degrees(glm::eulerAngles(ToQuat(*rotation)));
		}
		VansTransformStore::TransformIDToTransformDirty[transformId] = true;
		return true;
	};
	registerProperty({ "Transform.Position", Vans::VansRuntimeComponentType_Transform, Vans::VansTimelineChannelType::Vec3,
		[transformReader](const auto& target, auto& value, auto& error) { return transformReader(target, value, error, "position"); },
		[transformWriter](const auto& target, const auto& value, auto& error) { return transformWriter(target, value, error, "position"); } });
	registerProperty({ "Transform.Rotation", Vans::VansRuntimeComponentType_Transform, Vans::VansTimelineChannelType::Quaternion,
		[transformReader](const auto& target, auto& value, auto& error) { return transformReader(target, value, error, "rotation"); },
		[transformWriter](const auto& target, const auto& value, auto& error) { return transformWriter(target, value, error, "rotation"); } });
	registerProperty({ "Transform.Scale", Vans::VansRuntimeComponentType_Transform, Vans::VansTimelineChannelType::Vec3,
		[transformReader](const auto& target, auto& value, auto& error) { return transformReader(target, value, error, "scale"); },
		[transformWriter](const auto& target, const auto& value, auto& error) { return transformWriter(target, value, error, "scale"); } });

	auto cameraForTarget = [world](const Vans::VansResolvedTimelineTarget& target)
	{
		const Vans::VansComponentHandle component = ResolveComponent(world, target, Vans::VansRuntimeComponentType_Camera);
		auto* runtime = GetRuntimePayload<Vans::VansRuntimeCameraComponent>(world, component, Vans::VansRuntimeComponentType_Camera);
		return runtime ? runtime->camera : nullptr;
	};
	auto registerCameraFloat = [&](const char* id, auto getter, auto setter)
	{
		registerProperty({ id, Vans::VansRuntimeComponentType_Camera, Vans::VansTimelineChannelType::Float,
			[cameraForTarget, getter](const auto& target, auto& value, auto& error)
			{
				VansCamera* camera = cameraForTarget(target);
				if (!camera) { error = "Registered Camera property requires a valid Camera component"; return false; }
				value = static_cast<float>(getter(*camera)); return true;
			},
			[cameraForTarget, setter](const auto& target, const auto& value, auto& error)
			{
				VansCamera* camera = cameraForTarget(target);
				const float* number = std::get_if<float>(&value);
				if (!camera || !number) { error = "Registered Camera property target or value type is invalid"; return false; }
				setter(*camera, *number); return true;
			} });
	};
	registerCameraFloat("Camera.FieldOfView", [](const VansCamera& camera) { return camera.GetFov(); },
		[](VansCamera& camera, float value) { camera.SetFov(value); });
	registerCameraFloat("Camera.NearClip", [](const VansCamera& camera) { return camera.GetNearClip(); },
		[](VansCamera& camera, float value) { camera.SetNearClip(value); });
	registerCameraFloat("Camera.FarClip", [](const VansCamera& camera) { return camera.GetFarClip(); },
		[](VansCamera& camera, float value) { camera.SetFarClip(value); });

	auto audioForTarget = [world](const Vans::VansResolvedTimelineTarget& target)
	{
		const Vans::VansComponentHandle component = ResolveComponent(world, target, Vans::VansRuntimeComponentType_Audio);
		auto* runtime = GetRuntimePayload<Vans::VansRuntimeAudioComponent>(world, component, Vans::VansRuntimeComponentType_Audio);
		return runtime ? runtime->sourceBinding : nullptr;
	};
	auto registerAudioFloat = [&](const char* id, auto getter, auto setter)
	{
		registerProperty({ id, Vans::VansRuntimeComponentType_Audio, Vans::VansTimelineChannelType::Float,
			[audioForTarget, getter](const auto& target, auto& value, auto& error)
			{
				auto* audio = audioForTarget(target);
				if (!audio) { error = "Registered Audio property requires a bound Audio source"; return false; }
				value = static_cast<float>(getter(*audio)); return true;
			},
			[audioForTarget, setter](const auto& target, const auto& value, auto& error)
			{
				auto* audio = audioForTarget(target); const float* number = std::get_if<float>(&value);
				if (!audio || !number) { error = "Registered Audio property target or value type is invalid"; return false; }
				setter(*audio, *number); return true;
			} });
	};
	registerAudioFloat("Audio.Volume", [](const auto& audio) { return audio.GetVolume(); }, [](auto& audio, float value) { audio.SetVolume(value); });
	registerAudioFloat("Audio.Pitch", [](const auto& audio) { return audio.GetPitch(); }, [](auto& audio, float value) { audio.SetPitch(value); });
	registerAudioFloat("Audio.ReferenceDistance", [](const auto& audio) { return audio.GetRefDistance(); }, [](auto& audio, float value) { audio.SetRefDistance(value); });
	registerAudioFloat("Audio.MaxDistance", [](const auto& audio) { return audio.GetMaxDistance(); }, [](auto& audio, float value) { audio.SetMaxDistance(value); });
	registerAudioFloat("Audio.Rolloff", [](const auto& audio) { return audio.GetRolloff(); }, [](auto& audio, float value) { audio.SetRolloff(value); });
	registerAudioFloat("Audio.ReverbSend", [](const auto& audio) { return audio.GetReverbSend(); }, [](auto& audio, float value) { audio.SetReverbSend(value); });
	registerProperty({ "Audio.Loop", Vans::VansRuntimeComponentType_Audio, Vans::VansTimelineChannelType::Bool,
		[audioForTarget](const auto& target, auto& value, auto& error) { auto* audio = audioForTarget(target); if (!audio) { error = "Audio source unavailable"; return false; } value = audio->GetLoop(); return true; },
		[audioForTarget](const auto& target, const auto& value, auto& error) { auto* audio = audioForTarget(target); const bool* typed = std::get_if<bool>(&value); if (!audio || !typed) { error = "Audio Loop target or type is invalid"; return false; } audio->SetLoop(*typed); return true; } });
	registerProperty({ "Audio.Spatial", Vans::VansRuntimeComponentType_Audio, Vans::VansTimelineChannelType::Bool,
		[audioForTarget](const auto& target, auto& value, auto& error) { auto* audio = audioForTarget(target); if (!audio) { error = "Audio source unavailable"; return false; } value = audio->GetSpatial(); return true; },
		[audioForTarget](const auto& target, const auto& value, auto& error) { auto* audio = audioForTarget(target); const bool* typed = std::get_if<bool>(&value); if (!audio || !typed) { error = "Audio Spatial target or type is invalid"; return false; } audio->SetSpatial(*typed); return true; } });
	registerProperty({ "Audio.Bus", Vans::VansRuntimeComponentType_Audio, Vans::VansTimelineChannelType::String,
		[audioForTarget](const auto& target, auto& value, auto& error) { auto* audio = audioForTarget(target); if (!audio) { error = "Audio source unavailable"; return false; } value = audio->GetBusName(); return true; },
		[audioForTarget](const auto& target, const auto& value, auto& error) { auto* audio = audioForTarget(target); const std::string* typed = std::get_if<std::string>(&value); if (!audio || !typed) { error = "Audio Bus target or type is invalid"; return false; } audio->SetBusName(*typed); return true; } });

	m_TimelineRuntime->RegisterWorld(world);
	m_TimelineRuntime->SetAssetLoader(
		[resolveCurrentTimeline, compiledCache, propertyRegistry](
			const Vans::VansRuntimeTimelineComponent& component,
			std::shared_ptr<const Vans::VansCompiledTimeline>& timeline,
			std::string& error)
		{
			error.clear();
			if (component.assetGuid.empty())
			{
				error = "Timeline component requires an indexed asset GUID";
				return false;
			}
			std::uint64_t revision = 0;
			const Vans::VansResolvedAsset resolved = resolveCurrentTimeline(component.assetGuid, &revision);
			const auto cached = compiledCache->find(component.assetGuid);
			if (cached != compiledCache->end())
			{
				timeline = cached->second.first == revision ? cached->second.second.lock() : nullptr;
				if (timeline)
					return true;
			}

			if (!resolved.valid)
			{
				error = resolved.error;
				return false;
			}
			Vans::VansTimelineAsset source;
			if (!Vans::VansTimelineSerialization::Load(resolved.readPath, source, error))
				return false;

			Vans::VansTimelineCompileOptions options;
			options.validation.supportsPropertyDescriptor = [propertyRegistry](
				std::uint16_t componentTypeId,
				const std::string& descriptorId,
				Vans::VansTimelineChannelType valueType)
			{
				const Vans::VansTimelineRuntimePropertyDescriptor* descriptor =
					propertyRegistry->Find(descriptorId);
				return descriptor && descriptor->componentTypeId == componentTypeId &&
					descriptor->valueType == valueType;
			};
			options.dependencyLoader = [resolveCurrentTimeline](
				const Vans::VansTimelineAssetReference& reference,
				Vans::VansTimelineAsset& nested,
				std::string& identity,
				std::string& nestedError)
			{
				if (reference.assetGuid.empty())
				{
					nestedError = "Nested Timeline dependency requires an indexed asset GUID";
					return false;
				}
				const Vans::VansResolvedAsset child = resolveCurrentTimeline(reference.assetGuid, nullptr);
				if (!child.valid)
				{
					nestedError = child.error;
					return false;
				}
				identity = reference.assetGuid;
				return Vans::VansTimelineSerialization::Load(child.readPath, nested, nestedError);
			};
			Vans::VansTimelineCompileResult compiled = Vans::VansTimelineCompiler::Compile(source, options);
			if (!compiled)
			{
				error = DiagnosticsText(compiled.diagnostics);
				if (error.empty()) error = "Timeline compilation failed";
				return false;
			}
			timeline = std::move(compiled.timeline);
			(*compiledCache)[component.assetGuid] = { revision, timeline };
			VANS_LOG("[Timeline] Loaded and compiled asset guid='" << component.assetGuid
				<< "' from '" << resolved.readPath << "'");
			return true;
		});
	m_TimelineRuntime->SetAssetRevisionQuery([](const std::string& assetGuid, std::uint64_t& revision)
	{
		Vans::VansAssetGuid guid;
		if (!Vans::VansAssetGuid::TryParse(assetGuid, guid)) return false;
		const Vans::VansAssetDatabase* database = Vans::VansProjectManager::Get().GetAssetDatabase();
		const auto record = database ? database->Find(guid) : std::nullopt;
		if (!record || record->state == Vans::VansAssetState::Missing) return false;
		revision = record->generation;
		return true;
	});

	Vans::VansTimelineRuntimeAdapters adapters;
	adapters.property = [propertyRegistry](
		const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelinePropertyOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		return propertyRegistry->Apply(context.blendMode, target, output, restore, error);
	};
	adapters.transform = [this, world](
		const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineTransformOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		const std::uint32_t transformId = ResolveTransformId(world, target.entity);
		if (transformId >= VansTransformStore::GlobalTransforms.size())
		{
			error = "Transform track target has no valid runtime Transform component";
			return false;
		}
		const Vans::VansComponentHandle physicsComponent = ResolveComponent(
			world, target, Vans::VansRuntimeComponentType_Physics);
		auto* physics = GetRuntimePayload<Vans::VansRuntimePhysicsComponent>(
			world, physicsComponent, Vans::VansRuntimeComponentType_Physics);
		if (physics && physics->physicsNode &&
			physics->physicsNode->GetProperties().bodyType == VansEngine::PhysicsBodyType::Dynamic)
		{
			error = "Transform track cannot drive a dynamic rigid body before a deterministic PrePhysics command boundary exists";
			return false;
		}
		const VansTransform previous = VansTransformStore::GetTransform(transformId);
		VansTransform& transform = VansTransformStore::GetTransform(transformId);
		glm::vec3 position = ToVec3(output.position);
		glm::vec3 scale = ToVec3(output.scale);
		glm::quat rotation = ToQuat(output.rotation);
		std::uint32_t originTransformId = UINT32_MAX;
		if (output.space == "Local" && m_TransformParentSystem.HasParent(transformId))
			originTransformId = m_TransformParentSystem.GetParent(transformId);
		else if (output.space == "OwnerRelative")
			originTransformId = ResolveTransformId(world, target.rootOwner);
		if (originTransformId < VansTransformStore::GlobalTransforms.size() && originTransformId != transformId)
		{
			const glm::mat4 sampled = glm::translate(glm::mat4(1.0f), position) *
				glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
			DecomposeTransform(VansTransformStore::GetTransform(originTransformId).GetModelMatrix() * sampled,
				position, rotation, scale);
		}
		const glm::quat currentRotation = glm::quat(glm::radians(transform.m_Rotation));
		if (context.blendMode == Vans::VansTimelineBlendMode::Additive ||
			context.blendMode == Vans::VansTimelineBlendMode::Relative)
		{
			position += transform.m_Position;
			rotation = glm::normalize(currentRotation * rotation);
			scale *= transform.m_Scale;
		}
		else if (context.blendMode == Vans::VansTimelineBlendMode::Multiply)
		{
			position *= transform.m_Position;
			rotation = glm::normalize(currentRotation * rotation);
			scale *= transform.m_Scale;
		}
		if ((output.channels & 0x1u) != 0) transform.m_Position.x = position.x;
		if ((output.channels & 0x2u) != 0) transform.m_Position.y = position.y;
		if ((output.channels & 0x4u) != 0) transform.m_Position.z = position.z;
		if ((output.channels & 0x78u) != 0)
			transform.m_Rotation = glm::degrees(glm::eulerAngles(rotation));
		if ((output.channels & 0x80u) != 0) transform.m_Scale.x = scale.x;
		if ((output.channels & 0x100u) != 0) transform.m_Scale.y = scale.y;
		if ((output.channels & 0x200u) != 0) transform.m_Scale.z = scale.z;
		VansTransformStore::TransformIDToTransformDirty[transformId] = true;
		m_TransformParentSystem.MarkOffsetDirty(transformId);
		restore = [this, world, entity = target.entity, transformId, previous]
		{
			if (ResolveTransformId(world, entity) != transformId || transformId >= VansTransformStore::GlobalTransforms.size())
				return;
			VansTransformStore::GetTransform(transformId) = previous;
			VansTransformStore::TransformIDToTransformDirty[transformId] = true;
			m_TransformParentSystem.MarkOffsetDirty(transformId);
		};
		return true;
	};

	adapters.constraint = [world, adapterState](
		const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineConstraintOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		const std::uint32_t sourceId = ResolveTransformId(world, output.sourceTarget.entity);
		const Vans::VansResolvedTimelineTarget& constrained = output.constrainedTarget.valid
			? output.constrainedTarget : target;
		const std::uint32_t targetId = ResolveTransformId(world, constrained.entity);
		if (sourceId >= VansTransformStore::GlobalTransforms.size() ||
			targetId >= VansTransformStore::GlobalTransforms.size())
		{
			error = "Constraint track requires valid source and target Transform bindings";
			return false;
		}

		const VansTransform previous = VansTransformStore::GetTransform(targetId);
		VansTransform source = VansTransformStore::GetTransform(sourceId);
		VansTransform& destination = VansTransformStore::GetTransform(targetId);
		const double weight = std::clamp(output.weight, 0.0, 1.0);
		const glm::quat sourceRotation = glm::quat(glm::radians(source.m_Rotation));
		VansTransform offset;
		offset.m_Position = ToVec3(output.config.offsetPosition);
		offset.m_Rotation = glm::degrees(glm::eulerAngles(ToQuat(output.config.offsetRotation)));
		offset.m_Scale = ToVec3(output.config.offsetScale);
		const std::string constraintKey = RuntimeKey(context, constrained);
		if (output.config.maintainOffset)
		{
			auto found = adapterState->constraintOffsets.find(constraintKey);
			if (found == adapterState->constraintOffsets.end())
			{
				glm::vec3 position, scale; glm::quat rotation;
				DecomposeTransform(glm::inverse(source.GetModelMatrix()) * destination.GetModelMatrix(),
					position, rotation, scale);
				VansTransform captured;
				captured.m_Position = position;
				captured.m_Rotation = glm::degrees(glm::eulerAngles(rotation));
				captured.m_Scale = scale;
				found = adapterState->constraintOffsets.emplace(constraintKey,
					TimelineSceneAdapterState::ConstraintOffset{ captured }).first;
			}
			offset = found->second.value;
		}
		const glm::quat offsetRotation = glm::quat(glm::radians(offset.m_Rotation));
		glm::vec3 desiredPosition = source.m_Position + sourceRotation * offset.m_Position;
		glm::quat desiredRotation = glm::normalize(sourceRotation * offsetRotation);
		glm::vec3 desiredScale = source.m_Scale * offset.m_Scale;

		if (output.config.constraintType == "Aim" || output.config.constraintType == "LookAt")
		{
			const glm::vec3 direction = source.m_Position - destination.m_Position;
			if (glm::length2(direction) > 0.000001f)
			{
				glm::vec3 up = output.config.upAxis == "Z" ? glm::vec3(0.0f, 0.0f, 1.0f)
					: (output.config.upAxis == "X" ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f));
				desiredRotation = glm::quatLookAt(glm::normalize(direction), up);
				if (output.config.aimAxis == "X")
					desiredRotation *= glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
				else if (output.config.aimAxis == "Y")
					desiredRotation *= glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
			}
		}

		auto blendAxis = [&](glm::vec3& current, const glm::vec3& desired)
		{
			for (int axis = 0; axis < 3; ++axis)
				if ((output.config.axisMask & (1u << axis)) != 0)
					current[axis] = static_cast<float>(glm::mix(
						static_cast<double>(current[axis]), static_cast<double>(desired[axis]), weight));
		};
		const std::string& type = output.config.constraintType;
		if (type == "Parent" || type == "Position" || type == "Point") blendAxis(destination.m_Position, desiredPosition);
		if (type == "Parent" || type == "Rotation" || type == "Orient" || type == "Aim" || type == "LookAt")
		{
			const glm::quat current = glm::quat(glm::radians(destination.m_Rotation));
			destination.m_Rotation = glm::degrees(glm::eulerAngles(glm::slerp(current, desiredRotation, static_cast<float>(weight))));
		}
		if (type == "Parent" || type == "Scale") blendAxis(destination.m_Scale, desiredScale);
		VansTransformStore::TransformIDToTransformDirty[targetId] = true;
		restore = [world, adapterState, constraintKey, entity = constrained.entity, targetId, previous]
		{
			adapterState->constraintOffsets.erase(constraintKey);
			if (ResolveTransformId(world, entity) != targetId || targetId >= VansTransformStore::GlobalTransforms.size()) return;
			VansTransformStore::GetTransform(targetId) = previous;
			VansTransformStore::TransformIDToTransformDirty[targetId] = true;
		};
		return true;
	};

	adapters.activation = [world](
		const Vans::VansTimelineApplyContext&,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineActivationOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		if (output.scope == "RenderVisibility")
		{
			std::vector<VansRenderNode*> nodes = ResolveRenderNodes(world, target, "*");
			if (nodes.empty())
			{
				error = "Render visibility track requires a valid Render component";
				return false;
			}
			std::vector<bool> previous; previous.reserve(nodes.size());
			for (VansRenderNode* node : nodes)
			{
				previous.push_back(node && node->IsEnabled());
				if (node) node->SetEnabled(output.active);
			}
			restore = [nodes = std::move(nodes), previous = std::move(previous)]
			{
				for (std::size_t index = 0; index < nodes.size(); ++index)
					if (nodes[index]) nodes[index]->SetEnabled(previous[index]);
			};
			return true;
		}
		if (output.scope == "ComponentEnabled")
		{
			if (!target.component.IsValid() || !world->GetComponentHeader(target.component))
			{
				error = "Component activation track requires a valid component binding";
				return false;
			}
			const bool previous = world->IsComponentSelfEnabled(target.component);
			world->Commands().SetComponentEnabled(target.component, output.active);
			restore = [world, component = target.component, previous]
			{
				if (world->GetComponentHeader(component))
					world->Commands().SetComponentEnabled(component, previous);
			};
			return true;
		}
		const Vans::VansEntityRecord* record = world->Entities().Get(target.entity);
		if (!record)
		{
			error = "Entity activation track requires a living entity binding";
			return false;
		}
		const bool previous = record->selfActive;
		world->Commands().SetEntityActive(target.entity, output.active);
		restore = [world, entity = target.entity, previous]
		{
			if (world->IsAlive(entity)) world->Commands().SetEntityActive(entity, previous);
		};
		return true;
	};

	adapters.animation = [world, resolver, adapterState](
		const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineAnimationOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		const Vans::VansComponentHandle component = ResolveComponent(world, target, Vans::VansRuntimeComponentType_Animation);
		auto* runtime = GetRuntimePayload<Vans::VansRuntimeAnimationComponent>(world, component, Vans::VansRuntimeComponentType_Animation);
		auto* controller = runtime && runtime->animationNode ? runtime->animationNode->GetController() : nullptr;
		if (!controller || output.slot.empty())
		{
			error = "Animation track requires an Animation component with a controller and a configured slot";
			return false;
		}
		const VansAnimationSlotDefinition* slotDefinition = controller->FindSlotDefinition(output.slot);
		if (!slotDefinition)
		{
			error = "Animation controller does not expose the Timeline slot '" + output.slot + "'";
			return false;
		}
		if (!output.layer.empty() && output.layer != slotDefinition->layerId)
		{
			error = "Animation Timeline slot belongs to layer '" + slotDefinition->layerId +
				"', not configured layer '" + output.layer + "'";
			return false;
		}
		const std::string key = RuntimeKey(context, target);
		auto active = adapterState->animations.find(key);
		if (!output.active || output.exited)
		{
			if (active != adapterState->animations.end())
			{
				controller->StopSlot(active->second.handle, 0.0f, true);
				controller->EnableRootMotion(active->second.previousRootMotionEnabled);
				controller->SetRootMotionApplyToOwner(active->second.previousRootMotionApplyToOwner);
				adapterState->animations.erase(active);
			}
			return true;
		}

		if (active == adapterState->animations.end())
		{
			std::string clipName;
			if (!output.assetGuid.empty())
			{
				const Vans::VansResolvedAsset clipAsset = resolver->Resolve(output.assetGuid, Vans::VansAssetType::AnimationClip);
				if (!clipAsset.valid)
				{
					error = clipAsset.error;
					return false;
				}
				VansAnimationClip clip;
				Skeleton skeleton;
				if (!VansAnimationClipIO::Load(clipAsset.readPath.string(), clip, skeleton))
				{
					error = "Failed to load Timeline animation clip " + output.assetGuid;
					return false;
				}
				clipName = clip.clipName;
				if (!controller->GetClip(clipName))
					controller->AddClip(clipName, std::move(clip));
			}
			if (clipName.empty() || !controller->GetClip(clipName))
			{
				error = "Timeline animation clip is not available in the bound controller";
				return false;
			}
			const bool previousRootMotionEnabled = controller->IsRootMotionEnabled();
			const bool previousRootMotionApplyToOwner = controller->ShouldApplyRootMotionToOwner();
			VansSlotPlayRequest request;
			request.clipName = clipName;
			request.startTime = static_cast<float>(std::max(0.0, output.localSeconds));
			request.loopCount = 1;
			request.priority = context.priority;
			request.blendIn = static_cast<float>(std::max(0.0, output.blendInSeconds));
			request.blendOut = static_cast<float>(std::max(0.0, output.blendOutSeconds));
			request.weight = static_cast<float>(std::max(0.0, output.weight));
			request.externallyDriven = true;
			request.suppressRootMotion = output.rootMotionPolicy == "Ignore";
			request.additive = output.additive;
			request.syncGroup = output.syncGroup;
			request.markerSync = output.markerSync;
			if (!output.avatarMaskGuid.empty())
			{
				const Vans::VansResolvedAsset maskAsset = resolver->Resolve(
					output.avatarMaskGuid, Vans::VansAssetType::BoneMask);
				if (!maskAsset.valid)
				{
					controller->EnableRootMotion(previousRootMotionEnabled);
					controller->SetRootMotionApplyToOwner(previousRootMotionApplyToOwner);
					error = maskAsset.error;
					return false;
				}
				VansBoneMaskAsset mask;
				if (!VansBoneMaskStorage::Load(maskAsset.readPath, mask, error))
				{
					controller->EnableRootMotion(previousRootMotionEnabled);
					controller->SetRootMotionApplyToOwner(previousRootMotionApplyToOwner);
					return false;
				}
				VansCompiledBoneMask compiledMask = VansBoneMaskCompiler::Compile(mask, runtime->animationNode->GetSkeleton());
				if (!compiledMask.valid)
				{
					controller->EnableRootMotion(previousRootMotionEnabled);
					controller->SetRootMotionApplyToOwner(previousRootMotionApplyToOwner);
					error = "Animation avatar mask is invalid for the bound skeleton";
					return false;
				}
				request.boneMaskWeights = std::move(compiledMask.weights);
			}
			request.tag = key;
			if (output.rootMotionPolicy == "ApplyToOwner" || output.rootMotionPolicy == "ExtractOnly")
			{
				controller->EnableRootMotion(true);
				controller->SetRootMotionApplyToOwner(output.rootMotionPolicy == "ApplyToOwner");
			}
			const VansSlotPlaybackHandle handle = controller->PlaySlot(output.slot, request);
			if (!handle)
			{
				controller->EnableRootMotion(previousRootMotionEnabled);
				controller->SetRootMotionApplyToOwner(previousRootMotionApplyToOwner);
				error = "Animation controller rejected the Timeline slot request";
				return false;
			}
			adapterState->animations.emplace(key, TimelineSceneAdapterState::AnimationPlayback{
				component, handle, previousRootMotionEnabled, previousRootMotionApplyToOwner });
			active = adapterState->animations.find(key);
		}
		if (!controller->DriveSlot(active->second.handle,
			static_cast<float>(std::max(0.0, output.localSeconds)),
			static_cast<float>(std::max(0.0, output.weight))))
		{
			error = "Timeline could not drive the active Animation slot";
			return false;
		}
		restore = [world, adapterState, key]
		{
			const auto found = adapterState->animations.find(key);
			if (found == adapterState->animations.end()) return;
			auto* runtime = GetRuntimePayload<Vans::VansRuntimeAnimationComponent>(
				world, found->second.component, Vans::VansRuntimeComponentType_Animation);
			if (runtime && runtime->animationNode && runtime->animationNode->GetController())
			{
				auto* controller = runtime->animationNode->GetController();
				controller->StopSlot(found->second.handle, 0.0f, true);
				controller->EnableRootMotion(found->second.previousRootMotionEnabled);
				controller->SetRootMotionApplyToOwner(found->second.previousRootMotionApplyToOwner);
			}
			adapterState->animations.erase(found);
		};
		return true;
	};

	adapters.animatorParameter = [world](
		const Vans::VansTimelineApplyContext&,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineAnimatorParameterOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		const Vans::VansComponentHandle component = ResolveComponent(world, target, Vans::VansRuntimeComponentType_Animation);
		auto* runtime = GetRuntimePayload<Vans::VansRuntimeAnimationComponent>(world, component, Vans::VansRuntimeComponentType_Animation);
		auto* controller = runtime && runtime->animationNode ? runtime->animationNode->GetController() : nullptr;
		if (!controller || !controller->HasParameter(output.parameterName))
		{
			if (output.missingParameterPolicy == "WarningAndSkip") return true;
			error = "Animator parameter track target or parameter is unavailable";
			return false;
		}
		const auto found = controller->GetParameters().find(output.parameterName);
		const AnimatorParameter previous = found->second;
		auto apply = [](VansAnimationController& targetController, const std::string& name,
			const std::string& type, const Vans::VansTimelineKeyValue& value)
		{
			if (type == "Bool") targetController.SetBool(name, BoolValue(value));
			else if (type == "Int") targetController.SetInt(name, static_cast<int>(NumberValue(value)));
			else if (type == "Trigger") { if (BoolValue(value, true)) targetController.SetTrigger(name); else targetController.ResetTrigger(name); }
			else if (type == "Vector3") { if (const auto* v = std::get_if<Vans::VansTimelineVec3>(&value)) targetController.SetVector3(name, ToVec3(*v)); }
			else if (type == "Quaternion") { if (const auto* q = std::get_if<Vans::VansTimelineQuaternion>(&value)) targetController.SetQuaternion(name, ToQuat(*q)); }
			else targetController.SetFloat(name, static_cast<float>(NumberValue(value)));
		};
		apply(*controller, output.parameterName, output.parameterType, output.value);
		restore = [world, component, name = output.parameterName, previous]
		{
			auto* runtime = GetRuntimePayload<Vans::VansRuntimeAnimationComponent>(world, component, Vans::VansRuntimeComponentType_Animation);
			auto* controller = runtime && runtime->animationNode ? runtime->animationNode->GetController() : nullptr;
			if (!controller) return;
			switch (previous.type)
			{
			case AnimatorParamType::Bool: controller->SetBool(name, previous.boolVal); break;
			case AnimatorParamType::Int: controller->SetInt(name, previous.intVal); break;
			case AnimatorParamType::Trigger: previous.boolVal ? controller->SetTrigger(name) : controller->ResetTrigger(name); break;
			case AnimatorParamType::Vector3: controller->SetVector3(name, previous.vec3Val); break;
			case AnimatorParamType::Quaternion: controller->SetQuaternion(name, previous.quatVal); break;
			default: controller->SetFloat(name, previous.floatVal); break;
			}
		};
		return true;
	};

	adapters.boneOverride = [world](
		const Vans::VansTimelineApplyContext&,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineBoneOverrideOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		const Vans::VansComponentHandle component = ResolveComponent(world, target, Vans::VansRuntimeComponentType_Animation);
		auto* runtime = GetRuntimePayload<Vans::VansRuntimeAnimationComponent>(world, component, Vans::VansRuntimeComponentType_Animation);
		VansAnimationNode* node = runtime ? runtime->animationNode : nullptr;
		const std::string bone = output.config.boneId.empty() ? output.config.bone : output.config.boneId;
		if (!node || bone.empty())
		{
			error = "Bone override track requires a valid Animation component and stable bone identifier";
			return false;
		}
		glm::mat4 previous(1.0f);
		const bool hadPrevious = node->TryGetBoneLocalTransform(bone, previous);
		glm::mat4 current(1.0f);
		if (!node->TryGetCurrentBoneLocalTransform(bone, current))
		{
			error = "Bone Override could not resolve the configured bone in the bound skeleton";
			return false;
		}
		glm::vec3 currentPosition, currentScale;
		glm::quat currentRotation;
		DecomposeTransform(current, currentPosition, currentRotation, currentScale);
		glm::vec3 desiredPosition = ToVec3(output.transform.position);
		glm::quat desiredRotation = ToQuat(output.transform.rotation);
		glm::vec3 desiredScale = ToVec3(output.transform.scale);
		bool ikPosition = false;
		bool ikRotation = false;
		if (!output.config.ikTargetBindingId.empty())
		{
			if (!output.ikTarget.valid)
			{
				error = "Bone Override IK target binding could not be resolved";
				return false;
			}
			const std::uint32_t ikTransformId = ResolveTransformId(world, output.ikTarget.entity);
			const std::uint32_t ownerTransformId = ResolveTransformId(world, target.entity);
			const Skeleton& skeleton = node->GetSkeleton();
			const int boneIndex = FindBoneIndex(skeleton, bone);
			const auto* controller = node->GetController();
			if (ikTransformId >= VansTransformStore::GlobalTransforms.size() ||
				ownerTransformId >= VansTransformStore::GlobalTransforms.size() || !controller || boneIndex < 0)
			{
				error = "Bone Override IK requires valid owner, target and evaluated skeleton transforms";
				return false;
			}
			const glm::mat4 ownerInverse = glm::inverse(VansTransformStore::GetTransform(ownerTransformId).GetModelMatrix());
			const glm::vec3 targetModel = glm::vec3(ownerInverse * glm::vec4(
				VansTransformStore::GetTransform(ikTransformId).m_Position, 1.0f));
			const auto& globals = controller->GetCachedGlobalTransforms();
			const int parentIndex = skeleton.bones[boneIndex].parentIndex;
			const glm::mat4 parentModel = parentIndex >= 0 && parentIndex < static_cast<int>(globals.size())
				? globals[parentIndex] : glm::mat4(1.0f);
			desiredPosition = glm::vec3(glm::inverse(parentModel) * glm::vec4(targetModel, 1.0f));
			ikPosition = true;
			if (boneIndex < static_cast<int>(globals.size()))
			{
				const glm::vec3 boneModelPosition = glm::vec3(globals[boneIndex][3]);
				const glm::vec3 direction = targetModel - boneModelPosition;
				if (glm::length2(direction) > 0.000001f)
				{
					glm::vec3 up(0.0f, 1.0f, 0.0f);
					if (output.poleTarget.valid)
					{
						const std::uint32_t poleId = ResolveTransformId(world, output.poleTarget.entity);
						if (poleId < VansTransformStore::GlobalTransforms.size())
						{
							const glm::vec3 poleDirection = glm::vec3(ownerInverse * glm::vec4(
								VansTransformStore::GetTransform(poleId).m_Position, 1.0f)) - boneModelPosition;
							if (glm::length2(poleDirection) > 0.000001f)
								up = glm::normalize(poleDirection);
						}
					}
					const glm::quat desiredModelRotation = glm::quatLookAt(glm::normalize(direction), up);
					glm::vec3 ignoredPosition, ignoredScale; glm::quat parentRotation;
					DecomposeTransform(parentModel, ignoredPosition, parentRotation, ignoredScale);
					desiredRotation = glm::normalize(glm::inverse(parentRotation) * desiredModelRotation);
					ikRotation = true;
				}
			}
		}
		const float weight = static_cast<float>(std::clamp(output.config.weight, 0.0, 1.0));
		const float positionWeight = weight * static_cast<float>(std::clamp(output.config.positionWeight, 0.0, 1.0));
		const float rotationWeight = weight * static_cast<float>(std::clamp(output.config.rotationWeight, 0.0, 1.0));
		glm::vec3 finalPosition = currentPosition;
		glm::quat finalRotation = currentRotation;
		glm::vec3 finalScale = currentScale;
		if (ikPosition || (output.transform.channels & 0x7u) != 0)
			finalPosition = output.config.additive ? currentPosition + desiredPosition * positionWeight
				: glm::mix(currentPosition, desiredPosition, positionWeight);
		if (ikRotation || (output.transform.channels & 0x78u) != 0)
			finalRotation = output.config.additive
				? glm::normalize(currentRotation * glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), desiredRotation, rotationWeight))
				: glm::normalize(glm::slerp(currentRotation, desiredRotation, rotationWeight));
		if ((output.transform.channels & 0x380u) != 0)
			finalScale = output.config.additive ? currentScale * glm::mix(glm::vec3(1.0f), desiredScale, weight)
				: glm::mix(currentScale, desiredScale, weight);
		const glm::mat4 transform = glm::translate(glm::mat4(1.0f), finalPosition) *
			glm::mat4_cast(finalRotation) * glm::scale(glm::mat4(1.0f), finalScale);
		node->SetBoneLocalTransform(bone, transform);
		restore = [world, component, bone, previous, hadPrevious]
		{
			auto* runtime = GetRuntimePayload<Vans::VansRuntimeAnimationComponent>(world, component, Vans::VansRuntimeComponentType_Animation);
			if (!runtime || !runtime->animationNode) return;
			if (hadPrevious) runtime->animationNode->SetBoneLocalTransform(bone, previous);
			else runtime->animationNode->ClearBoneOverride(bone);
		};
		return true;
	};

	adapters.audio = [this, world, adapterState](
		const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineAudioOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		const std::string key = RuntimeKey(context, target);
		VansEngine::VansAudioSourceBinding* binding = nullptr;
		Vans::VansComponentHandle component;
		if (output.config.useBoundSource)
		{
			component = ResolveComponent(world, target, Vans::VansRuntimeComponentType_Audio);
			auto* runtime = GetRuntimePayload<Vans::VansRuntimeAudioComponent>(world, component, Vans::VansRuntimeComponentType_Audio);
			binding = runtime ? runtime->sourceBinding : nullptr;
		}
		else
		{
			auto found = adapterState->audioInstances.find(key);
			if (found == adapterState->audioInstances.end())
			{
				if (output.assetGuid.empty())
				{
					error = "Audio section requires an asset GUID when useBoundSource is false";
					return false;
				}
				auto owned = std::make_unique<VansEngine::VansAudioSourceBinding>();
				if (!owned->Bind(&m_AudioManager, output.assetGuid))
				{
					error = "Audio asset is not loaded in the scene resource closure: " + output.assetGuid;
					return false;
				}
				binding = owned.get();
				adapterState->audioInstances.emplace(key, std::move(owned));
			}
			else binding = found->second.get();
		}
		if (!binding)
		{
			error = "Audio track requires a valid Audio component or loaded asset";
			return false;
		}
		const bool previousPlaying = binding->IsPlaying();
		const bool previousPaused = binding->IsPaused();
		const float previousVolume = binding->GetVolume();
		const float previousPitch = binding->GetPitch();
		const bool previousLoop = binding->GetLoop();
		const bool previousSpatial = binding->GetSpatial();
		const float previousPan = binding->GetStereoPan();
		const float previousReverb = binding->GetReverbSend();
		const float previousReferenceDistance = binding->GetRefDistance();
		const float previousMaxDistance = binding->GetMaxDistance();
		const float previousRolloff = binding->GetRolloff();
		const std::string previousBus = binding->GetBusName();
		const double previousOffset = binding->GetPlaybackOffsetSeconds();
		binding->SetVolume(static_cast<float>(std::max(0.0,
			output.config.volume * output.envelopeWeight)));
		binding->SetPitch(static_cast<float>(std::max(0.01, output.config.pitch)));
		binding->SetLoop(output.loop);
		binding->SetSpatial(output.config.spatialBlend >= 0.5);
		binding->SetStereoPan(static_cast<float>(output.config.stereoPan));
		binding->SetBusName(output.config.bus);
		binding->SetReverbSend(static_cast<float>(std::clamp(output.config.reverbSend, 0.0, 1.0)));
		binding->SetRefDistance(static_cast<float>(std::max(0.01, output.config.referenceDistance)));
		binding->SetMaxDistance(static_cast<float>(std::max(output.config.referenceDistance, output.config.maxDistance)));
		binding->SetRolloff(static_cast<float>(std::max(0.0, output.config.rolloff)));
		if (output.active)
		{
			if (output.config.seekPolicy != "Disabled" &&
				(output.entered || std::abs(binding->GetPlaybackOffsetSeconds() - output.localSeconds) > 0.05))
			{
				if (!binding->Seek(output.localSeconds) && output.config.seekPolicy == "Exact")
				{
					binding->SetVolume(previousVolume);
					binding->SetPitch(previousPitch);
					binding->SetLoop(previousLoop);
					binding->SetSpatial(previousSpatial);
					binding->SetStereoPan(previousPan);
					binding->SetReverbSend(previousReverb);
					binding->SetRefDistance(previousReferenceDistance);
					binding->SetMaxDistance(previousMaxDistance);
					binding->SetRolloff(previousRolloff);
					binding->SetBusName(previousBus);
					if (!output.config.useBoundSource) adapterState->audioInstances.erase(key);
					error = "Audio backend rejected an Exact Timeline seek";
					return false;
				}
			}
			if (!binding->IsPlaying()) binding->Play();
		}
		else if (output.exited)
		{
			if (output.config.onSectionEnd == "PlayToCompletion") {}
			else if (output.config.onSectionEnd == "FadeOut") { binding->SetVolume(0.0f); binding->Stop(); }
			else binding->Stop();
		}
		restore = [world, adapterState, key, component, useBound = output.config.useBoundSource,
			previousPlaying, previousPaused, previousVolume, previousPitch, previousLoop,
			previousSpatial, previousPan, previousReverb, previousReferenceDistance,
			previousMaxDistance, previousRolloff, previousBus, previousOffset]
		{
			VansEngine::VansAudioSourceBinding* current = nullptr;
			if (useBound)
			{
				auto* runtime = GetRuntimePayload<Vans::VansRuntimeAudioComponent>(world, component, Vans::VansRuntimeComponentType_Audio);
				current = runtime ? runtime->sourceBinding : nullptr;
			}
			else
			{
				const auto found = adapterState->audioInstances.find(key);
				current = found == adapterState->audioInstances.end() ? nullptr : found->second.get();
			}
			if (current)
			{
				current->SetVolume(previousVolume);
				current->SetPitch(previousPitch);
				current->SetLoop(previousLoop);
				current->SetSpatial(previousSpatial);
				current->SetStereoPan(previousPan);
				current->SetReverbSend(previousReverb);
				current->SetRefDistance(previousReferenceDistance);
				current->SetMaxDistance(previousMaxDistance);
				current->SetRolloff(previousRolloff);
				current->SetBusName(previousBus);
				current->Seek(previousOffset);
				if (previousPlaying) current->Play();
				else if (previousPaused) current->Pause();
				else current->Stop();
			}
			if (!useBound) adapterState->audioInstances.erase(key);
		};
		return true;
	};

	adapters.media = [this, world](
		const Vans::VansTimelineApplyContext&,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineMediaOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		if (output.config.targetKind != "VideoComponent")
		{
			error = "Media target kind has no registered runtime video setter: " + output.config.targetKind;
			return false;
		}
		if (output.config.outputAudio)
		{
			error = "Media audio output is unavailable in the current Video backend";
			return false;
		}
		Vans::VansComponentHandle component = ResolveComponent(world, target, Vans::VansRuntimeComponentType_Video);
		auto* runtime = GetRuntimePayload<Vans::VansRuntimeVideoComponent>(world, component, Vans::VansRuntimeComponentType_Video);
		VansVideoTexture* video = runtime ? runtime->videoTexture : nullptr;
		if (!video && !output.assetGuid.empty()) video = m_VideoManager.GetByAssetGuid(output.assetGuid);
		if (!video)
		{
			error = "Media track requires a bound Video component or loaded video asset";
			return false;
		}
		const bool previousPlaying = video->IsPlaying();
		const double previousTime = video->GetPlayTime();
		const double previousRate = video->GetPlaybackRate();
		if (output.reverse)
		{
			error = "Media reverse playback is unavailable in the current Video backend";
			return false;
		}
		if (output.config.syncMode == "AudioClock")
		{
			error = "Media AudioClock sync requires a registered Audio clock source";
			return false;
		}
		if (output.active)
		{
			if (output.config.syncMode == "TimelineClock")
			{
				video->SetPlaybackRate(0.0);
				if (!video->Seek(output.localSeconds))
				{
					video->SetPlaybackRate(previousRate);
					error = "Video backend rejected a TimelineClock seek";
					return false;
				}
				video->Pause();
			}
			else
			{
				video->SetPlaybackRate(output.playbackRate);
				if (output.entered && !video->Seek(output.localSeconds))
				{
					video->SetPlaybackRate(previousRate);
					error = "Video backend rejected the section start seek";
					return false;
				}
				video->Play();
			}
		}
		else if (output.exited)
		{
			if (output.config.onSectionEnd == "PauseLastFrame") video->Pause();
			else video->Stop();
		}
		restore = [video, previousPlaying, previousTime, previousRate]
		{
			video->SetPlaybackRate(previousRate);
			video->Seek(previousTime);
			previousPlaying ? video->Play() : video->Pause();
		};
		return true;
	};

	adapters.particle = [world](
		const Vans::VansTimelineApplyContext&,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineParticleOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		const Vans::VansComponentHandle component = ResolveComponent(world, target, Vans::VansRuntimeComponentType_Particle);
		auto* runtime = GetRuntimePayload<Vans::VansRuntimeParticleComponent>(world, component, Vans::VansRuntimeComponentType_Particle);
		VansParticleRuntime* particle = runtime ? runtime->runtime : nullptr;
		if (!particle)
		{
			error = "Particle track requires a valid Particle component";
			return false;
		}
		const bool previousPlaying = particle->IsPlaying();
		const float previousTime = particle->GetPlayTime();
		const std::uint32_t previousSeed = particle->GetRandomSeed();
		const float previousRate = particle->GetSimulationRate();
		if (output.active)
		{
			if (output.entered)
			{
				particle->SetRandomSeed(output.config.randomSeed);
				particle->SetSimulationRate(static_cast<float>(output.config.simulationRate));
				if (output.config.resetOnEnter) particle->Restart();
			}
			const std::string& action = output.config.action;
			if (action == "Stop") particle->Stop();
			else if (action == "Pause") particle->Pause();
			else if (action == "Burst")
			{
				if (output.entered) particle->Burst();
				particle->Pause();
			}
			else
			{
				if (action == "Restart" && output.entered) particle->Restart();
				const float targetTime = static_cast<float>(std::max(0.0,
					output.localSeconds + output.prewarmSeconds));
				if (output.config.seekPolicy == "DeterministicResimulate" &&
					(output.entered || std::abs(particle->GetPlayTime() - targetTime) > 0.05f))
					particle->Seek(targetTime);
				particle->Play();
			}
		}
		else if (output.exited)
		{
			output.config.clearOnExit ? particle->Stop() : particle->Pause();
		}
		restore = [world, component, previousPlaying, previousTime, previousSeed, previousRate]
		{
			auto* runtime = GetRuntimePayload<Vans::VansRuntimeParticleComponent>(world, component, Vans::VansRuntimeComponentType_Particle);
			if (!runtime || !runtime->runtime) return;
			runtime->runtime->SetRandomSeed(previousSeed);
			runtime->runtime->SetSimulationRate(previousRate);
			runtime->runtime->Seek(previousTime);
			previousPlaying ? runtime->runtime->Play() : runtime->runtime->Pause();
		};
		return true;
	};

	adapters.cameraCut = [this, world, adapterState](
		const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineCameraCutOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		const std::string key = RuntimeKey(context, target);
		if (!output.active)
		{
			const auto found = adapterState->cameraCuts.find(key);
			if (found != adapterState->cameraCuts.end())
			{
				auto& state = found->second;
				if (state.drivenCamera && state.transformAltered && state.previousHasTransform &&
					state.drivenCamera->GetTransformID() < VansTransformStore::GlobalTransforms.size())
				{
					const auto transformId = state.drivenCamera->GetTransformID();
					VansTransformStore::GetTransform(transformId) = state.previousTransform;
					VansTransformStore::TransformIDToTransformDirty[transformId] = true;
					state.drivenCamera->SetFov(state.previousFov);
					state.drivenCamera->SetNearClip(state.previousNear);
					state.drivenCamera->SetFarClip(state.previousFar);
					state.drivenCamera->SetAspectRatio(state.previousAspect);
				}
				InjectCamera(state.previousCamera);
				adapterState->cameraCuts.erase(found);
			}
			return true;
		}

		VansCamera* sourceCamera = ResolveTimelineCamera(world, output.cameraTarget);
		const bool explicitTargetCamera = !output.targetCameraBindingId.empty();
		if (!explicitTargetCamera && sourceCamera)
		{
			auto state = adapterState->cameraCuts.find(key);
			if (state == adapterState->cameraCuts.end())
			{
				TimelineSceneAdapterState::CameraCutState captured;
				captured.previousCamera = GetCamera();
				captured.drivenCamera = captured.previousCamera;
				if (captured.previousCamera)
				{
					captured.previousFov = captured.previousCamera->GetFov();
					captured.previousNear = captured.previousCamera->GetNearClip();
					captured.previousFar = captured.previousCamera->GetFarClip();
					captured.previousAspect = captured.previousCamera->GetAspectRatio();
					captured.previousHasTransform = captured.previousCamera->HasTransform() &&
						captured.previousCamera->GetTransformID() < VansTransformStore::GlobalTransforms.size();
					if (captured.previousHasTransform)
						captured.previousTransform = VansTransformStore::GetTransform(captured.previousCamera->GetTransformID());
				}
				state = adapterState->cameraCuts.emplace(key, std::move(captured)).first;
			}
			if (output.config.cutMode == "Blend" && output.blendAlpha < 1.0)
			{
				VansCamera* blendCamera = state->second.previousCamera;
				if (!blendCamera || !blendCamera->HasTransform() || !sourceCamera->HasTransform() ||
					blendCamera->GetTransformID() >= VansTransformStore::GlobalTransforms.size() ||
					sourceCamera->GetTransformID() >= VansTransformStore::GlobalTransforms.size())
				{
					adapterState->cameraCuts.erase(state);
					error = "Camera Blend requires source and target cameras with runtime Transform bindings";
					return false;
				}
				const float alpha = static_cast<float>(std::clamp(output.blendAlpha, 0.0, 1.0));
				const VansTransform targetTransform = VansTransformStore::GetTransform(sourceCamera->GetTransformID());
				VansTransform& blended = VansTransformStore::GetTransform(blendCamera->GetTransformID());
				blended.m_Position = glm::mix(state->second.previousTransform.m_Position, targetTransform.m_Position, alpha);
				const glm::quat sourceRotation = glm::quat(glm::radians(state->second.previousTransform.m_Rotation));
				const glm::quat targetRotation = glm::quat(glm::radians(targetTransform.m_Rotation));
				blended.m_Rotation = glm::degrees(glm::eulerAngles(glm::slerp(sourceRotation, targetRotation, alpha)));
				blended.m_Scale = glm::mix(state->second.previousTransform.m_Scale, targetTransform.m_Scale, alpha);
				VansTransformStore::TransformIDToTransformDirty[blendCamera->GetTransformID()] = true;
				blendCamera->SetFov(glm::mix(state->second.previousFov, sourceCamera->GetFov(), alpha));
				blendCamera->SetNearClip(glm::mix(state->second.previousNear, sourceCamera->GetNearClip(), alpha));
				blendCamera->SetFarClip(glm::mix(state->second.previousFar, sourceCamera->GetFarClip(), alpha));
				if (output.config.aspectPolicy == "MatchViewport")
					blendCamera->SetAspectRatio(glm::mix(state->second.previousAspect, sourceCamera->GetAspectRatio(), alpha));
				state->second.transformAltered = true;
				InjectCamera(blendCamera);
			}
			else
			{
				if (state->second.transformAltered && state->second.previousCamera && state->second.previousHasTransform)
				{
					const auto transformId = state->second.previousCamera->GetTransformID();
					VansTransformStore::GetTransform(transformId) = state->second.previousTransform;
					VansTransformStore::TransformIDToTransformDirty[transformId] = true;
					state->second.previousCamera->SetFov(state->second.previousFov);
					state->second.previousCamera->SetNearClip(state->second.previousNear);
					state->second.previousCamera->SetFarClip(state->second.previousFar);
					state->second.previousCamera->SetAspectRatio(state->second.previousAspect);
					state->second.transformAltered = false;
				}
				InjectCamera(sourceCamera);
			}
			restore = [this, adapterState, key]
			{
				const auto found = adapterState->cameraCuts.find(key);
				if (found == adapterState->cameraCuts.end()) return;
				auto& state = found->second;
				if (state.drivenCamera && state.transformAltered && state.previousHasTransform &&
					state.drivenCamera->GetTransformID() < VansTransformStore::GlobalTransforms.size())
				{
					const auto transformId = state.drivenCamera->GetTransformID();
					VansTransformStore::GetTransform(transformId) = state.previousTransform;
					VansTransformStore::TransformIDToTransformDirty[transformId] = true;
					state.drivenCamera->SetFov(state.previousFov);
					state.drivenCamera->SetNearClip(state.previousNear);
					state.drivenCamera->SetFarClip(state.previousFar);
					state.drivenCamera->SetAspectRatio(state.previousAspect);
				}
				InjectCamera(state.previousCamera);
				adapterState->cameraCuts.erase(found);
			};
			return true;
		}

		VansCamera* drivenCamera = nullptr;
		if (explicitTargetCamera)
			drivenCamera = ResolveTimelineCamera(world, output.targetCameraTarget);
		if (!drivenCamera)
			drivenCamera = GetCamera();
		if (!drivenCamera || !drivenCamera->HasTransform() ||
			drivenCamera->GetTransformID() >= VansTransformStore::GlobalTransforms.size())
		{
			error = explicitTargetCamera
				? "Camera Cut targetCameraBindingId must resolve to a real Camera with a runtime Transform binding"
				: "Camera Cut requires an active real Camera with a runtime Transform binding";
			return false;
		}

		auto state = adapterState->cameraCuts.find(key);
		if (state == adapterState->cameraCuts.end())
		{
			TimelineSceneAdapterState::CameraCutState captured;
			captured.previousCamera = GetCamera();
			captured.drivenCamera = drivenCamera;
			captured.previousFov = drivenCamera->GetFov();
			captured.previousNear = drivenCamera->GetNearClip();
			captured.previousFar = drivenCamera->GetFarClip();
			captured.previousAspect = drivenCamera->GetAspectRatio();
			captured.previousHasTransform = drivenCamera->HasTransform() &&
				drivenCamera->GetTransformID() < VansTransformStore::GlobalTransforms.size();
			if (captured.previousHasTransform)
				captured.previousTransform = VansTransformStore::GetTransform(drivenCamera->GetTransformID());
			state = adapterState->cameraCuts.emplace(key, std::move(captured)).first;
		}
		state->second.drivenCamera = drivenCamera;

		TimelineCameraPose sourcePose;
		if (!ResolveTimelineCameraPose(world, output.cameraTarget, *adapterState, drivenCamera, sourcePose, error))
			return false;
		TimelineCameraPose previousPose;
		previousPose.transform = state->second.previousTransform;
		previousPose.hasTransform = state->second.previousHasTransform;
		previousPose.fov = state->second.previousFov;
		previousPose.nearClip = state->second.previousNear;
		previousPose.farClip = state->second.previousFar;
		previousPose.aspect = state->second.previousAspect;
		const bool blending = output.config.cutMode == "Blend" && output.blendAlpha < 1.0;
		const TimelineCameraPose drivenPose = blending
			? BlendCameraPose(previousPose, sourcePose,
				static_cast<float>(std::clamp(output.blendAlpha, 0.0, 1.0)),
				output.config.aspectPolicy == "MatchViewport")
			: sourcePose;
		ApplyCameraPose(drivenCamera, drivenPose);
		state->second.transformAltered = true;
		InjectCamera(drivenCamera);
		restore = [this, adapterState, key]
		{
			const auto found = adapterState->cameraCuts.find(key);
			if (found == adapterState->cameraCuts.end()) return;
			auto& state = found->second;
			if (state.drivenCamera && state.transformAltered && state.previousHasTransform &&
				state.drivenCamera->GetTransformID() < VansTransformStore::GlobalTransforms.size())
			{
				const auto transformId = state.drivenCamera->GetTransformID();
				VansTransformStore::GetTransform(transformId) = state.previousTransform;
				VansTransformStore::TransformIDToTransformDirty[transformId] = true;
				state.drivenCamera->SetFov(state.previousFov);
				state.drivenCamera->SetNearClip(state.previousNear);
				state.drivenCamera->SetFarClip(state.previousFar);
				state.drivenCamera->SetAspectRatio(state.previousAspect);
			}
			InjectCamera(state.previousCamera);
			adapterState->cameraCuts.erase(found);
		};
		return true;
	};

	adapters.cameraProperty = [world, adapterState](
		const Vans::VansTimelineApplyContext&,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineCameraPropertyOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		const Vans::VansComponentHandle component = ResolveComponent(world, target, Vans::VansRuntimeComponentType_Camera);
		auto* runtime = GetRuntimePayload<Vans::VansRuntimeCameraComponent>(world, component, Vans::VansRuntimeComponentType_Camera);
		VansCamera* camera = runtime ? runtime->camera : nullptr;
		const std::string property = LowerAscii(output.property);
		if (!camera)
		{
			const std::uint32_t transformId = ResolveTransformId(world, target.entity);
			if (transformId >= VansTransformStore::GlobalTransforms.size())
			{
				error = "Camera Property track requires a valid Camera component or virtual camera Transform binding";
				return false;
			}

			const std::string key = TimelineTargetKey(target);
			auto& virtualCamera = adapterState->virtualCameras[key];
			if (property == "fieldofview" || property == "fov")
			{
				const bool previousHasValue = virtualCamera.hasFov;
				const float previousValue = virtualCamera.fov;
				virtualCamera.fov = static_cast<float>(NumberValue(output.value, virtualCamera.fov));
				virtualCamera.hasFov = true;
				restore = [adapterState, key, previousHasValue, previousValue]
				{
					auto found = adapterState->virtualCameras.find(key);
					if (found == adapterState->virtualCameras.end()) return;
					found->second.hasFov = previousHasValue;
					found->second.fov = previousValue;
				};
			}
			else if (property == "nearclip")
			{
				const bool previousHasValue = virtualCamera.hasNearClip;
				const float previousValue = virtualCamera.nearClip;
				virtualCamera.nearClip = static_cast<float>(NumberValue(output.value, virtualCamera.nearClip));
				virtualCamera.hasNearClip = true;
				restore = [adapterState, key, previousHasValue, previousValue]
				{
					auto found = adapterState->virtualCameras.find(key);
					if (found == adapterState->virtualCameras.end()) return;
					found->second.hasNearClip = previousHasValue;
					found->second.nearClip = previousValue;
				};
			}
			else if (property == "farclip")
			{
				const bool previousHasValue = virtualCamera.hasFarClip;
				const float previousValue = virtualCamera.farClip;
				virtualCamera.farClip = static_cast<float>(NumberValue(output.value, virtualCamera.farClip));
				virtualCamera.hasFarClip = true;
				restore = [adapterState, key, previousHasValue, previousValue]
				{
					auto found = adapterState->virtualCameras.find(key);
					if (found == adapterState->virtualCameras.end()) return;
					found->second.hasFarClip = previousHasValue;
					found->second.farClip = previousValue;
				};
			}
			else if (property == "aspect" || property == "aspectratio")
			{
				const bool previousHasValue = virtualCamera.hasAspect;
				const float previousValue = virtualCamera.aspect;
				virtualCamera.aspect = static_cast<float>(NumberValue(output.value, virtualCamera.aspect));
				virtualCamera.hasAspect = true;
				restore = [adapterState, key, previousHasValue, previousValue]
				{
					auto found = adapterState->virtualCameras.find(key);
					if (found == adapterState->virtualCameras.end()) return;
					found->second.hasAspect = previousHasValue;
					found->second.aspect = previousValue;
				};
			}
			else
			{
				error = "Virtual Camera Property exposes lens channels only: " + output.property;
				return false;
			}
			return true;
		}
		if (property == "fieldofview" || property == "fov")
		{
			const float previous = camera->GetFov();
			camera->SetFov(static_cast<float>(NumberValue(output.value, previous)));
			restore = [camera, previous] { camera->SetFov(previous); };
		}
		else if (property == "nearclip")
		{
			const float previous = camera->GetNearClip();
			camera->SetNearClip(static_cast<float>(NumberValue(output.value, previous)));
			restore = [camera, previous] { camera->SetNearClip(previous); };
		}
		else if (property == "farclip")
		{
			const float previous = camera->GetFarClip();
			camera->SetFarClip(static_cast<float>(NumberValue(output.value, previous)));
			restore = [camera, previous] { camera->SetFarClip(previous); };
		}
		else if (property == "position" || property == "transform.position")
		{
			if (!camera->HasTransform() || camera->GetTransformID() >= VansTransformStore::GlobalTransforms.size() ||
				!std::holds_alternative<Vans::VansTimelineVec3>(output.value))
			{
				error = "Camera Position requires a runtime Transform and Vec3 channel";
				return false;
			}
			const auto transformId = camera->GetTransformID();
			VansTransform& transform = VansTransformStore::GetTransform(transformId);
			const glm::vec3 previous = transform.m_Position;
			transform.m_Position = ToVec3(std::get<Vans::VansTimelineVec3>(output.value));
			VansTransformStore::TransformIDToTransformDirty[transformId] = true;
			restore = [transformId, previous]
			{
				if (transformId >= VansTransformStore::GlobalTransforms.size()) return;
				VansTransformStore::GetTransform(transformId).m_Position = previous;
				VansTransformStore::TransformIDToTransformDirty[transformId] = true;
			};
		}
		else if (property == "rotation" || property == "transform.rotation")
		{
			if (!camera->HasTransform() || camera->GetTransformID() >= VansTransformStore::GlobalTransforms.size() ||
				!std::holds_alternative<Vans::VansTimelineQuaternion>(output.value))
			{
				error = "Camera Rotation requires a runtime Transform and Quaternion channel";
				return false;
			}
			const auto transformId = camera->GetTransformID();
			VansTransform& transform = VansTransformStore::GetTransform(transformId);
			const glm::vec3 previous = transform.m_Rotation;
			transform.m_Rotation = glm::degrees(glm::eulerAngles(ToQuat(
				std::get<Vans::VansTimelineQuaternion>(output.value))));
			VansTransformStore::TransformIDToTransformDirty[transformId] = true;
			restore = [transformId, previous]
			{
				if (transformId >= VansTransformStore::GlobalTransforms.size()) return;
				VansTransformStore::GetTransform(transformId).m_Rotation = previous;
				VansTransformStore::TransformIDToTransformDirty[transformId] = true;
			};
		}
		else if (property == "scale" || property == "transform.scale")
		{
			if (!camera->HasTransform() || camera->GetTransformID() >= VansTransformStore::GlobalTransforms.size() ||
				!std::holds_alternative<Vans::VansTimelineVec3>(output.value))
			{
				error = "Camera Scale requires a runtime Transform and Vec3 channel";
				return false;
			}
			const auto transformId = camera->GetTransformID();
			VansTransform& transform = VansTransformStore::GetTransform(transformId);
			const glm::vec3 previous = transform.m_Scale;
			transform.m_Scale = ToVec3(std::get<Vans::VansTimelineVec3>(output.value));
			VansTransformStore::TransformIDToTransformDirty[transformId] = true;
			restore = [transformId, previous]
			{
				if (transformId >= VansTransformStore::GlobalTransforms.size()) return;
				VansTransformStore::GetTransform(transformId).m_Scale = previous;
				VansTransformStore::TransformIDToTransformDirty[transformId] = true;
			};
		}
		else
		{
			error = "Camera property is not exposed by the runtime Camera adapter: " + output.property;
			return false;
		}
		return true;
	};

	adapters.cameraShake = [world, adapterState](
		const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineCameraShakeOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		if (!output.active)
			return true;

		const Vans::VansComponentHandle component = ResolveComponent(world, target, Vans::VansRuntimeComponentType_Camera);
		auto* runtime = GetRuntimePayload<Vans::VansRuntimeCameraComponent>(world, component, Vans::VansRuntimeComponentType_Camera);
		VansCamera* camera = runtime ? runtime->camera : nullptr;
		if (!camera || !camera->HasTransform() || camera->GetTransformID() >= VansTransformStore::GlobalTransforms.size())
		{
			error = "Camera Shake track requires a real Camera with a runtime Transform binding";
			return false;
		}

		const std::uint32_t transformId = camera->GetTransformID();
		VansTransform& transform = VansTransformStore::GetTransform(transformId);
		const std::string key = RuntimeKey(context, target);
		auto& state = adapterState->cameraShakes[key];
		if (state.applied && state.transformId == transformId &&
			NearlyEqual(transform.m_Position, state.appliedPosition) &&
			NearlyEqual(transform.m_Rotation, state.appliedRotation))
		{
			transform.m_Position = state.basePosition;
			transform.m_Rotation = state.baseRotation;
		}

		const glm::vec3 basePosition = transform.m_Position;
		const glm::vec3 baseRotation = transform.m_Rotation;
		const float weight = static_cast<float>(std::max(0.0, output.weight));
		glm::vec3 positionOffset = output.config.position ? ToVec3(output.positionOffset) * weight : glm::vec3(0.0f);
		const glm::vec3 rotationOffset = output.config.rotation ? ToVec3(output.rotationOffset) * weight : glm::vec3(0.0f);
		if (LowerAscii(output.config.space) == "cameralocal")
			positionOffset = glm::quat(glm::radians(baseRotation)) * positionOffset;

		transform.m_Position = basePosition + positionOffset;
		transform.m_Rotation = baseRotation + rotationOffset;
		VansTransformStore::TransformIDToTransformDirty[transformId] = true;

		state.transformId = transformId;
		state.applied = true;
		state.basePosition = basePosition;
		state.baseRotation = baseRotation;
		state.appliedPosition = transform.m_Position;
		state.appliedRotation = transform.m_Rotation;

		restore = [adapterState, key]
		{
			const auto found = adapterState->cameraShakes.find(key);
			if (found == adapterState->cameraShakes.end()) return;
			const auto transformId = found->second.transformId;
			if (transformId < VansTransformStore::GlobalTransforms.size())
			{
				VansTransform& current = VansTransformStore::GetTransform(transformId);
				if (found->second.applied &&
					NearlyEqual(current.m_Position, found->second.appliedPosition) &&
					NearlyEqual(current.m_Rotation, found->second.appliedRotation))
				{
					current.m_Position = found->second.basePosition;
					current.m_Rotation = found->second.baseRotation;
					VansTransformStore::TransformIDToTransformDirty[transformId] = true;
				}
			}
			adapterState->cameraShakes.erase(found);
		};
		return true;
	};

	adapters.fadePostProcess = [this, resolver, adapterState](
		const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineFadePostProcessOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		VansMaterialManager* materialManager = GetMaterialManager();
		if (!materialManager)
		{
			error = "Fade/PostProcess track requires the scene PostProcess compositor";
			return false;
		}

		const std::string key = RuntimeKey(context, target);
		auto [baseIt, inserted] = adapterState->postProcessBase.try_emplace(
			key, materialManager->m_PostProcessProfile);
		VansPostProcessProfile composed = baseIt->second;

		if (output.config.mode == "Fade")
		{
			composed.m_TimelineFadeColorR = static_cast<float>(output.config.color.value[0]);
			composed.m_TimelineFadeColorG = static_cast<float>(output.config.color.value[1]);
			composed.m_TimelineFadeColorB = static_cast<float>(output.config.color.value[2]);
			composed.m_TimelineFadeOpacity = static_cast<float>(std::clamp(output.value, 0.0, 1.0));
			composed.m_IsDirty = true;
		}
		else if (output.config.mode == "PostProcess")
		{
			if (output.config.profileGuid.empty())
			{
				if (inserted) adapterState->postProcessBase.erase(baseIt);
				error = "PostProcess mode requires an indexed Profile GUID";
				return false;
			}
			auto profileIt = adapterState->postProcessProfiles.find(output.config.profileGuid);
			if (profileIt == adapterState->postProcessProfiles.end())
			{
				const Vans::VansResolvedAsset resolved = resolver->Resolve(
					output.config.profileGuid, Vans::VansAssetType::PostProcessProfile);
				if (!resolved.valid)
				{
					if (inserted) adapterState->postProcessBase.erase(baseIt);
					error = resolved.error;
					return false;
				}
				VansPostProcessProfile profile;
				if (!VansPostProcessProfileStorage::Load(resolved.readPath, profile, error))
				{
					if (inserted) adapterState->postProcessBase.erase(baseIt);
					return false;
				}
				profileIt = adapterState->postProcessProfiles.emplace(
					output.config.profileGuid, std::move(profile)).first;
			}
			composed = BlendPostProcessProfile(
				baseIt->second,
				profileIt->second,
				static_cast<float>(std::clamp(output.value, 0.0, 1.0)));
		}
		else
		{
			if (inserted) adapterState->postProcessBase.erase(baseIt);
			error = "Fade/PostProcess mode must be Fade or PostProcess";
			return false;
		}

		materialManager->m_PostProcessProfile = composed;
		restore = [adapterState, materialManager, key]
		{
			const auto found = adapterState->postProcessBase.find(key);
			if (found == adapterState->postProcessBase.end()) return;
			materialManager->m_PostProcessProfile = found->second;
			materialManager->m_PostProcessProfile.m_IsDirty = true;
			adapterState->postProcessBase.erase(found);
		};
		return true;
	};

	adapters.light = [world](
		const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineLightOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		const Vans::VansComponentHandle component = ResolveLightComponent(world, target);
		auto* runtime = GetRuntimePayload<Vans::VansRuntimeLightComponent>(world, component, component.typeId);
		if (!runtime || !runtime->lightManager || runtime->lightIndex < 0)
		{
			error = "Light track requires a valid runtime Light component";
			return false;
		}
		VansLightManager* manager = runtime->lightManager;
		const std::string& property = output.property;
		const int index = runtime->lightIndex;
		auto blendNumber = [&](double current)
		{
			const double sampled = NumberValue(output.value, current);
			if (context.blendMode == Vans::VansTimelineBlendMode::Additive ||
				context.blendMode == Vans::VansTimelineBlendMode::Relative) return current + sampled;
			if (context.blendMode == Vans::VansTimelineBlendMode::Multiply) return current * sampled;
			return sampled;
		};
		auto blendColor = [&](const glm::vec3& current)
		{
			const glm::vec3 sampled = property == "temperature"
				? TemperatureColor(NumberValue(output.value, 6500.0)) : ColorValue(output.value, current);
			if (context.blendMode == Vans::VansTimelineBlendMode::Additive ||
				context.blendMode == Vans::VansTimelineBlendMode::Relative) return current + sampled;
			if (context.blendMode == Vans::VansTimelineBlendMode::Multiply) return current * sampled;
			return sampled;
		};

		if (runtime->kind == Vans::VansRuntimeLightKind::Directional)
		{
			auto& lights = manager->GetDirectionLights();
			if (index >= static_cast<int>(lights.size())) { error = "Directional Light runtime index is invalid"; return false; }
			const VansDirectionalLight previous = lights[index];
			if (property == "color" || property == "temperature") lights[index].m_Color = blendColor(lights[index].m_Color);
			else if (property == "intensity") lights[index].m_Intensity = static_cast<float>(std::max(0.0, blendNumber(lights[index].m_Intensity)));
			else { error = "Directional Light property is not registered: " + property; return false; }
			restore = [manager, index, previous]
			{
				if (index < static_cast<int>(manager->GetDirectionLights().size())) manager->GetDirectionLights()[index] = previous;
				manager->UpdateLightCPUData();
			};
		}
		else if (runtime->kind == Vans::VansRuntimeLightKind::Point)
		{
			auto& lights = manager->GetPointLights();
			if (index >= static_cast<int>(lights.size())) { error = "Point Light runtime index is invalid"; return false; }
			const VansPointLight previous = lights[index];
			const auto previousShadow = index < static_cast<int>(manager->GetPointShadowRegistrations().size())
				? manager->GetPointShadowRegistrations()[index].settings : VansPunctualShadowSettings{};
			if (property == "color" || property == "temperature") lights[index].m_Color = blendColor(lights[index].m_Color);
			else if (property == "intensity") lights[index].m_Intensity = static_cast<float>(std::max(0.0, blendNumber(lights[index].m_Intensity)));
			else if (property == "range" || property == "radius") lights[index].m_Radius = static_cast<float>(std::max(0.0, blendNumber(lights[index].m_Radius)));
			else if (property == "shadow" || property == "castShadows")
			{
				if (index >= static_cast<int>(manager->GetPointShadowRegistrations().size())) { error = "Point Light shadow registration is unavailable"; return false; }
				manager->GetPointShadowRegistrations()[index].settings.castShadows = BoolValue(output.value);
			}
			else { error = "Point Light property is not registered: " + property; return false; }
			restore = [manager, index, previous, previousShadow]
			{
				if (index < static_cast<int>(manager->GetPointLights().size())) manager->GetPointLights()[index] = previous;
				if (index < static_cast<int>(manager->GetPointShadowRegistrations().size())) manager->GetPointShadowRegistrations()[index].settings = previousShadow;
				manager->UpdateLightCPUData();
			};
		}
		else if (runtime->kind == Vans::VansRuntimeLightKind::Spot)
		{
			auto& lights = manager->GetSpotLight();
			if (index >= static_cast<int>(lights.size())) { error = "Spot Light runtime index is invalid"; return false; }
			const VansSpotLight previous = lights[index];
			const auto previousShadow = index < static_cast<int>(manager->GetSpotShadowRegistrations().size())
				? manager->GetSpotShadowRegistrations()[index].settings : VansPunctualShadowSettings{};
			if (property == "color" || property == "temperature") lights[index].m_Color = blendColor(lights[index].m_Color);
			else if (property == "intensity") lights[index].m_Intensity = static_cast<float>(std::max(0.0, blendNumber(lights[index].m_Intensity)));
			else if (property == "range" || property == "radius") lights[index].m_Radius = static_cast<float>(std::max(0.0, blendNumber(lights[index].m_Radius)));
			else if (property == "innerCone" || property == "innerCutoff") lights[index].m_InnerCutOff = static_cast<float>(blendNumber(lights[index].m_InnerCutOff));
			else if (property == "outerCone" || property == "outerCutoff") lights[index].m_OuterCutOff = static_cast<float>(blendNumber(lights[index].m_OuterCutOff));
			else if (property == "shadow" || property == "castShadows")
			{
				if (index >= static_cast<int>(manager->GetSpotShadowRegistrations().size())) { error = "Spot Light shadow registration is unavailable"; return false; }
				manager->GetSpotShadowRegistrations()[index].settings.castShadows = BoolValue(output.value);
			}
			else { error = "Spot Light property is not registered: " + property; return false; }
			restore = [manager, index, previous, previousShadow]
			{
				if (index < static_cast<int>(manager->GetSpotLight().size())) manager->GetSpotLight()[index] = previous;
				if (index < static_cast<int>(manager->GetSpotShadowRegistrations().size())) manager->GetSpotShadowRegistrations()[index].settings = previousShadow;
				manager->UpdateLightCPUData();
			};
		}
		else
		{
			auto& lights = manager->GetRectLights();
			if (index >= static_cast<int>(lights.size())) { error = "Rect Light runtime index is invalid"; return false; }
			const VansRectLight previous = lights[index];
			const auto previousShadow = index < static_cast<int>(manager->GetRectShadowRegistrations().size())
				? manager->GetRectShadowRegistrations()[index].settings : VansPunctualShadowSettings{};
			if (property == "color" || property == "temperature") lights[index].m_Color = blendColor(lights[index].m_Color);
			else if (property == "intensity") lights[index].m_Intensity = static_cast<float>(std::max(0.0, blendNumber(lights[index].m_Intensity)));
			else if (property == "range") lights[index].m_Range = static_cast<float>(std::max(0.0, blendNumber(lights[index].m_Range)));
			else if (property == "width") lights[index].m_HalfWidth = static_cast<float>(std::max(0.0, blendNumber(lights[index].m_HalfWidth * 2.0) * 0.5));
			else if (property == "height") lights[index].m_HalfHeight = static_cast<float>(std::max(0.0, blendNumber(lights[index].m_HalfHeight * 2.0) * 0.5));
			else if (property == "shadow" || property == "castShadows")
			{
				if (index >= static_cast<int>(manager->GetRectShadowRegistrations().size())) { error = "Rect Light shadow registration is unavailable"; return false; }
				manager->GetRectShadowRegistrations()[index].settings.castShadows = BoolValue(output.value);
			}
			else { error = "Rect Light property is not registered: " + property; return false; }
			restore = [manager, index, previous, previousShadow]
			{
				if (index < static_cast<int>(manager->GetRectLights().size())) manager->GetRectLights()[index] = previous;
				if (index < static_cast<int>(manager->GetRectShadowRegistrations().size())) manager->GetRectShadowRegistrations()[index].settings = previousShadow;
				manager->UpdateLightCPUData();
			};
		}
		manager->UpdateLightCPUData();
		return true;
	};

	adapters.materialParameter = [this, world, adapterState](
		const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineMaterialParameterOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		if (output.instancePolicy != "PerEntityRuntimeInstance")
		{
			error = "Material Parameter only accepts PerEntityRuntimeInstance to protect shared assets";
			return false;
		}
		std::vector<VansRenderNode*> nodes = ResolveRenderNodes(world, target, output.materialSlotId);
		if (nodes.empty())
		{
			error = "Material Parameter track could not resolve the requested renderer material slot";
			return false;
		}
		VansMaterialManager* materialManager = GetMaterialManager();
		std::vector<std::string> bindingKeys;
		for (VansRenderNode* node : nodes)
		{
			if (!node || !node->m_Material)
			{
				error = "Material Parameter target has no source material";
				return false;
			}
			const std::string bindingKey = "entity:" + std::to_string(target.entity.index) + ":" +
				std::to_string(target.entity.generation) + ":slot:" + output.materialSlotId + ":node:" +
				std::to_string(reinterpret_cast<std::uintptr_t>(node));
			auto binding = adapterState->materialInstances.find(bindingKey);
			if (binding == adapterState->materialInstances.end())
			{
				VansMaterial* source = node->m_Material;
				VansMaterial* instance = materialManager->AcquireRuntimeMaterialInstance(
					bindingKey, *source, GetGlobalDescriptorSet());
				if (!instance)
				{
					error = "Runtime material instance pool is exhausted or the source material type is unsupported";
					return false;
				}
				TimelineSceneAdapterState::MaterialInstanceBinding created;
				created.node = node;
				created.source = source;
				created.instance = instance;
				binding = adapterState->materialInstances.emplace(bindingKey, std::move(created)).first;
				node->m_Material = instance;
				node->RecreateDescriptorSets(GetCamera(), *GetLightManager(), *materialManager);
			}
			binding->second.writers[context.writerId] = true;
			if (!materialManager->ApplyMaterialParameter(
				*binding->second.instance, output.parameterName, ToMaterialValue(output.value)))
			{
				error = "Material parameter is not registered for the runtime material type: " + output.parameterName;
				return false;
			}
			bindingKeys.push_back(bindingKey);
		}
		restore = [this, adapterState, materialManager, writer = context.writerId,
			bindingKeys = std::move(bindingKeys)]
		{
			for (const std::string& bindingKey : bindingKeys)
			{
				auto binding = adapterState->materialInstances.find(bindingKey);
				if (binding == adapterState->materialInstances.end()) continue;
				binding->second.writers.erase(writer);
				if (!binding->second.writers.empty()) continue;
				if (binding->second.node && binding->second.node->m_Material == binding->second.instance)
				{
					binding->second.node->m_Material = binding->second.source;
					binding->second.node->RecreateDescriptorSets(GetCamera(), *GetLightManager(), *materialManager);
				}
				materialManager->ReleaseRuntimeMaterialInstance(bindingKey);
				adapterState->materialInstances.erase(binding);
			}
		};
		return true;
	};

	adapters.materialSwitch = [this, world](
		const Vans::VansTimelineApplyContext&,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineMaterialSwitchOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		if (output.materialGuid.empty())
		{
			error = "Material Switch requires an indexed material GUID";
			return false;
		}
		VansMaterial* material = dynamic_cast<VansMaterial*>(FindMaterialAsset(output.materialGuid));
		if (!material)
		{
			error = "Material Switch dependency is not loaded in the scene resource closure: " + output.materialGuid;
			return false;
		}
		std::vector<VansRenderNode*> nodes = ResolveRenderNodes(world, target, output.materialSlotId);
		if (nodes.empty())
		{
			error = "Material Switch could not resolve the requested renderer material slot";
			return false;
		}
		struct Previous
		{
			VansRenderNode* node = nullptr;
			VansMaterial* material = nullptr;
			RenderNodeType type = NONE_NODE;
			bool rayTracing = false;
		};
		std::vector<Previous> previous;
		previous.reserve(nodes.size());
		for (VansRenderNode* node : nodes)
		{
			const RenderNodeType targetType = RuntimeNodeTypeForMaterial(*material, node->GetNodeType());
			if (!RuntimeNodeClassCompatible(*node, targetType))
			{
				error = "Material Switch crosses an opaque/transparent/decal node class and requires a renderer rebuild";
				return false;
			}
			previous.push_back({ node, node->m_Material, node->GetNodeType(), node->m_RayTracingEnabled });
			if (node->GetNodeType() != targetType)
			{
				RemoveRenderNodeFromVector(node);
				node->SetNodeType(targetType);
				RegistRenderNode(node, targetType);
			}
			node->m_Material = material;
			node->m_RayTracingEnabled = material->m_MaterialType != VAN_TRANSPARENT &&
				material->m_MaterialType != VAN_PBR_TRANSMISSION;
			node->RecreateDescriptorSets(GetCamera(), *GetLightManager(), *GetMaterialManager());
		}
		restore = [this, previous = std::move(previous)]
		{
			for (const Previous& item : previous)
			{
				if (!item.node) continue;
				if (item.node->GetNodeType() != item.type)
				{
					RemoveRenderNodeFromVector(item.node);
					item.node->SetNodeType(item.type);
					RegistRenderNode(item.node, item.type);
				}
				item.node->m_Material = item.material;
				item.node->m_RayTracingEnabled = item.rayTracing;
				item.node->RecreateDescriptorSets(GetCamera(), *GetLightManager(), *GetMaterialManager());
			}
		};
		return true;
	};

	adapters.ui = [](const Vans::VansTimelineApplyContext&,
		const Vans::VansResolvedTimelineTarget&,
		const Vans::VansTimelineUIOutput& output,
		Vans::VansTimelineRestoreCallback& restore,
		std::string& error)
	{
		VansRuntime::VansUIScreenManager& manager = VansRuntime::VansUISystem::Get().GetScreenManager();
		const std::shared_ptr<VansRuntime::VansUIScreen> screen = manager.GetScreenByName(output.config.screen);
		if (!screen)
		{
			error = "UI State track could not resolve an open Screen by stable name or GUID";
			return false;
		}
		if (output.config.targetKind == "Action")
		{
			if (!output.entered) return true;
			if (output.config.setterId != 200 || output.config.action.empty())
			{
				error = "UI Action requires registered setterId 200 and an action name";
				return false;
			}
			VansRuntime::VansUIVariantMap params;
			params.emplace("value", ToUIVariant(output.value));
			VansRuntime::VansUIActionBus::Get().Dispatch({ output.config.action, std::move(params), screen->GetHandleId(), output.config.element });
			return true;
		}
		if (output.config.targetKind == "Screen")
		{
			if (output.config.setterId != 300 || output.config.descriptorId != "Screen.Visible")
			{
				error = "UI Screen property is not registered";
				return false;
			}
			const bool previous = screen->IsVisible();
			BoolValue(output.value, true) ? screen->Show() : screen->Hide();
			restore = [screen, previous] { previous ? screen->Show() : screen->Hide(); };
			return true;
		}
		if (output.config.targetKind == "ViewModel")
		{
			const std::shared_ptr<VansRuntime::VansUIViewModel> viewModel = screen->GetViewModel();
			if (output.config.setterId != 100 || output.config.descriptorId.empty() || !viewModel)
			{
				error = "UI ViewModel property requires registered setterId 100, a property name and a ViewModel";
				return false;
			}
			const VansRuntime::VansUIVariant* oldValue = viewModel->GetValue(output.config.descriptorId);
			const bool hadValue = oldValue != nullptr;
			const VansRuntime::VansUIVariant previous = oldValue ? *oldValue : VansRuntime::VansUIVariant{};
			viewModel->SetValue(output.config.descriptorId, ToUIVariant(output.value));
			restore = [viewModel, name = output.config.descriptorId, previous, hadValue]
			{
				if (hadValue) viewModel->SetValue(name, previous);
				else viewModel->RemoveValue(name);
			};
			return true;
		}

		const char* property = UIElementProperty(output.config.setterId, output.config.descriptorId);
		VansRuntime::VansUIElementHandle element = screen->FindElement(output.config.element);
		std::string previous;
		if (!property || !element.IsValid() || !element.TryGetProperty(property, previous))
		{
			error = "UI Element property is not registered, readable, or the target element is missing";
			return false;
		}
		element.SetProperty(property, UIStringValue(output.value));
		restore = [screen, elementName = output.config.element, property = std::string(property), previous]
		{
			VansRuntime::VansUIElementHandle current = screen->FindElement(elementName);
			if (current.IsValid()) current.SetProperty(property, previous);
		};
		return true;
	};

	adapters.event = [](const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target,
		const Vans::VansTimelineEventOutput& output,
		Vans::VansTimelineRestoreCallback&,
		std::string& error)
	{
		if (output.config.signalId.empty())
		{
			error = "Event track requires a stable signalId";
			return false;
		}
		Vans::VansTimelineSignalEvent event;
		event.signalId = output.config.signalId;
		event.displayName = output.config.displayName;
		event.sourceTrackId = context.propertyKey;
		event.writerId = context.writerId;
		event.targetEntity = target.entity;
		event.payload = output.payload;
		event.tick = output.tick;
		Vans::VansEventBus::Get().Enqueue(std::move(event), EventLane(output.config.eventLane));
		return true;
	};

	m_TimelineRuntime->SetAdapters(std::move(adapters));
	if (m_LoadMode == VansSceneLoadMode::Runtime)
		m_TimelineRuntime->SyncTimelineComponents();
}

void VansGraphics::VansScene::UpdateTimelinesPostScript(double deltaSeconds)
{
	if (!m_TimelineRuntime)
		return;
	m_TimelineRuntime->UpdateRuntimePostScript(deltaSeconds);
	if (m_RuntimeWorld)
		m_RuntimeWorld->FlushCommands();
}

void VansGraphics::VansScene::UpdateTimelinesCamera(double deltaSeconds)
{
	if (m_TimelineRuntime)
		m_TimelineRuntime->UpdateRuntimeCamera(deltaSeconds);
}

void VansGraphics::VansScene::UpdateTimelinePreviewsPostScript(double deltaSeconds)
{
	if (!m_TimelineRuntime)
		return;
	m_TimelineRuntime->UpdatePreviewsPostScript(deltaSeconds);
	if (m_RuntimeWorld)
		m_RuntimeWorld->FlushCommands();
}

void VansGraphics::VansScene::UpdateTimelinePreviewsCamera(double deltaSeconds)
{
	if (m_TimelineRuntime)
		m_TimelineRuntime->UpdatePreviewsCamera(deltaSeconds);
}

bool VansGraphics::VansScene::PlayRuntimeTimeline(const std::string& componentGuid, bool restart)
{
	if (!m_RuntimeWorld || !m_TimelineRuntime || componentGuid.empty())
	{
		VANS_LOG_ERROR("[Timeline] Manual play failed because the runtime scene is unavailable or the component GUID is empty");
		return false;
	}
	const Vans::VansComponentHandle component = m_RuntimeWorld->FindComponentByGuid(
		componentGuid, Vans::VansRuntimeComponentType_Timeline);
	if (!component.IsValid())
	{
		VANS_LOG_ERROR("[Timeline] Manual play could not find Timeline component guid='" << componentGuid << "'");
		return false;
	}
	if (m_TimelineRuntime->PlayComponent(component, restart)) return true;
	std::string reason = "Timeline player is unavailable";
	for (auto diagnostic = m_TimelineRuntime->Diagnostics().rbegin();
		diagnostic != m_TimelineRuntime->Diagnostics().rend(); ++diagnostic)
	{
		if (diagnostic->objectId != componentGuid) continue;
		reason = diagnostic->message;
		break;
	}
	VANS_LOG_ERROR("[Timeline] Manual play failed component='" << componentGuid << "': " << reason);
	return false;
}

bool VansGraphics::VansScene::PauseRuntimeTimeline(const std::string& componentGuid)
{
	if (!m_RuntimeWorld || !m_TimelineRuntime || componentGuid.empty()) return false;
	const Vans::VansComponentHandle component = m_RuntimeWorld->FindComponentByGuid(
		componentGuid, Vans::VansRuntimeComponentType_Timeline);
	return component.IsValid() && m_TimelineRuntime->PauseComponent(component);
}

bool VansGraphics::VansScene::ResumeRuntimeTimeline(const std::string& componentGuid)
{
	if (!m_RuntimeWorld || !m_TimelineRuntime || componentGuid.empty()) return false;
	const Vans::VansComponentHandle component = m_RuntimeWorld->FindComponentByGuid(
		componentGuid, Vans::VansRuntimeComponentType_Timeline);
	return component.IsValid() && m_TimelineRuntime->ResumeComponent(component);
}

bool VansGraphics::VansScene::StopRuntimeTimeline(const std::string& componentGuid)
{
	if (!m_RuntimeWorld || !m_TimelineRuntime || componentGuid.empty()) return false;
	const Vans::VansComponentHandle component = m_RuntimeWorld->FindComponentByGuid(
		componentGuid, Vans::VansRuntimeComponentType_Timeline);
	return component.IsValid() && m_TimelineRuntime->StopComponent(component);
}

bool VansGraphics::VansScene::GetRuntimeTimelineState(
	const std::string& componentGuid,
	std::string& state,
	std::int64_t& tick) const
{
	if (!m_RuntimeWorld || !m_TimelineRuntime || componentGuid.empty()) return false;
	const Vans::VansComponentHandle component = m_RuntimeWorld->FindComponentByGuid(
		componentGuid, Vans::VansRuntimeComponentType_Timeline);
	Vans::VansTimelinePlayerState playerState{};
	if (!component.IsValid() || !m_TimelineRuntime->GetComponentState(component, playerState, tick))
		return false;
	switch (playerState)
	{
	case Vans::VansTimelinePlayerState::Unloaded: state = "Unloaded"; break;
	case Vans::VansTimelinePlayerState::Stopped: state = "Stopped"; break;
	case Vans::VansTimelinePlayerState::Playing: state = "Playing"; break;
	case Vans::VansTimelinePlayerState::Paused: state = "Paused"; break;
	case Vans::VansTimelinePlayerState::Completed: state = "Completed"; break;
	case Vans::VansTimelinePlayerState::Error: state = "Error"; break;
	}
	return true;
}

std::string VansGraphics::VansScene::FindTimelineInstanceOwnerGuid(const std::string& assetGuid) const
{
	if (!m_RuntimeWorld || assetGuid.empty()) return {};
	const auto* storage = static_cast<const Vans::VansComponentStorage<Vans::VansRuntimeTimelineComponent>*>(
		m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Timeline));
	if (!storage) return {};
	const auto& components = storage->DenseData();
	const auto& headers = storage->Headers();
	for (std::size_t index = 0; index < components.size(); ++index)
	{
		if (components[index].assetGuid != assetGuid) continue;
		const Vans::VansEntityRecord* owner = m_RuntimeWorld->Entities().Get(headers[index].owner);
		if (owner && !owner->stableGuid.empty()) return owner->stableGuid;
	}
	return {};
}

bool VansGraphics::VansScene::StartTimelinePreview(
	const std::string& previewId,
	const std::string& canonicalJson,
	const std::string& ownerEntityGuid,
	bool safeEvents,
	bool includeSubTimelines,
	std::string& error)
{
	error.clear();
	if (!m_RuntimeWorld || !m_TimelineRuntime)
	{
		error = "Runtime scene is not ready for Timeline preview";
		return false;
	}
	Vans::VansTimelineAsset asset;
	try
	{
		const auto root = Vans::VansTimelineSerialization::Json::parse(canonicalJson);
		if (!Vans::VansTimelineSerialization::Decode(root, asset, error))
			return false;
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		return false;
	}

	auto records = Vans::VansProjectManager::Get().EnumerateAssetRecords();
	auto resolver = std::make_shared<Vans::VansAssetResolver>(Vans::VansAssetAccessMode::Editor, records);
	Vans::VansTimelineCompileOptions options;
	options.validation.runtimeValidation = false;
	options.validation.supportsPropertyDescriptor = [registry = m_TimelinePropertyRegistry](
		std::uint16_t componentTypeId,
		const std::string& descriptorId,
		Vans::VansTimelineChannelType valueType)
	{
		if (!registry) return false;
		const Vans::VansTimelineRuntimePropertyDescriptor* descriptor = registry->Find(descriptorId);
		return descriptor && descriptor->componentTypeId == componentTypeId &&
			descriptor->valueType == valueType;
	};
	options.dependencyLoader = [resolver](
		const Vans::VansTimelineAssetReference& reference,
		Vans::VansTimelineAsset& nested,
		std::string& identity,
		std::string& nestedError)
	{
		if (reference.assetGuid.empty())
		{
			nestedError = "Timeline preview dependencies require indexed asset GUIDs";
			return false;
		}
		const Vans::VansResolvedAsset resolved = resolver->Resolve(reference.assetGuid, Vans::VansAssetType::Timeline);
		if (!resolved.valid) { nestedError = resolved.error; return false; }
		identity = reference.assetGuid;
		return Vans::VansTimelineSerialization::Load(resolved.readPath, nested, nestedError);
	};
	Vans::VansTimelineCompileResult compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!compiled)
	{
		error = DiagnosticsText(compiled.diagnostics);
		if (error.empty()) error = "Timeline preview compilation failed";
		return false;
	}
	Vans::VansEntityHandle owner = ownerEntityGuid.empty()
		? Vans::VansEntityHandle{} : m_RuntimeWorld->Entities().FindByGuid(ownerEntityGuid);
	if (!ownerEntityGuid.empty() && !owner.IsValid())
	{
		error = "Timeline preview instance owner does not exist in RuntimeWorld";
		return false;
	}
	return m_TimelineRuntime->StartPreview(previewId, std::move(compiled.timeline), owner,
		safeEvents, includeSubTimelines, error);
}

bool VansGraphics::VansScene::PlayTimelinePreview(const std::string& previewId)
{
	return m_TimelineRuntime && m_TimelineRuntime->PlayPreview(previewId);
}

bool VansGraphics::VansScene::PauseTimelinePreview(const std::string& previewId)
{
	return m_TimelineRuntime && m_TimelineRuntime->PausePreview(previewId);
}

bool VansGraphics::VansScene::ConfigureTimelinePreviewPlayback(
	const std::string& previewId,
	double playRate,
	int direction,
	bool loopPlaybackRange)
{
	return m_TimelineRuntime && m_TimelineRuntime->ConfigurePreview(
		previewId, playRate, direction, loopPlaybackRange);
}

bool VansGraphics::VansScene::SeekTimelinePreview(
	const std::string& previewId,
	std::int64_t tick,
	bool safeEdges)
{
	return m_TimelineRuntime && m_TimelineRuntime->SeekPreview(previewId, tick,
		safeEdges ? Vans::VansTimelineSeekPolicy::SafeEdges : Vans::VansTimelineSeekPolicy::ContinuousOnly);
}

bool VansGraphics::VansScene::StopTimelinePreview(const std::string& previewId)
{
	return m_TimelineRuntime && m_TimelineRuntime->StopPreview(previewId);
}

bool VansGraphics::VansScene::GetTimelinePreviewState(
	const std::string& previewId,
	int& state,
	std::int64_t& tick) const
{
	if (!m_TimelineRuntime) return false;
	Vans::VansTimelinePlayer* player = m_TimelineRuntime->FindPreview(previewId);
	if (!player) return false;
	state = static_cast<int>(player->State());
	tick = player->CurrentTick();
	return true;
}
