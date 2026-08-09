#pragma once

#include "VansRuntimeHandle.h"

#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AudioCore/VansAudioDirectionality.h"
#include "../AudioCore/VansAudioOcclusion.h"
#include "../AudioCore/VansAudioReverbPreset.h"
#include "../TimelineRuntime/VansTimelineComponent.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
class VansAnimationNode;
class VansCamera;
class VansLightManager;
class VansParticleRenderNode;
class VansParticleRuntime;
class VansRenderNode;
class VansVideoManager;
class VansVideoTexture;
}

namespace VansEngine
{
class VansCharacterControllerNode;
class VansClothNode;
class VansAudioNode;
class VansAudioSourceBinding;
class VansPhysicsNode;
class VansPhysicsVehicle;
}

namespace Vans
{
enum VansRuntimeComponentTypeId : std::uint16_t
{
	VansRuntimeComponentType_Render = 1,
	VansRuntimeComponentType_Physics = 2,
	VansRuntimeComponentType_Cloth = 3,
	VansRuntimeComponentType_CharacterController = 4,
	VansRuntimeComponentType_DirectionalLight = 5,
	VansRuntimeComponentType_PointLight = 6,
	VansRuntimeComponentType_SpotLight = 7,
	VansRuntimeComponentType_RectLight = 8,
	VansRuntimeComponentType_Camera = 9,
	VansRuntimeComponentType_Audio = 10,
	VansRuntimeComponentType_AudioReverbZone = 11,
	VansRuntimeComponentType_AudioVolume = 12,
	VansRuntimeComponentType_Video = 13,
	VansRuntimeComponentType_Particle = 14,
	VansRuntimeComponentType_Animation = 15,
	VansRuntimeComponentType_Ragdoll = 16,
	VansRuntimeComponentType_Vehicle = 17,
	VansRuntimeComponentType_UI = 18,
	VansRuntimeComponentType_Script = 19,
	VansRuntimeComponentType_Transform = 20,
	VansRuntimeComponentType_Timeline = 21,
};

struct VansRuntimeTransformComponent
{
	std::uint32_t transformStoreId = UINT32_MAX;
};

struct VansRuntimeRenderComponent
{
	VansGraphics::VansRenderNode* renderNode = nullptr;
	std::vector<VansGraphics::VansRenderNode*> renderNodes;
};

struct VansRuntimePhysicsComponent
{
	VansEngine::VansPhysicsNode* physicsNode = nullptr;
};

struct VansRuntimeClothComponent
{
	VansEngine::VansClothNode* clothNode = nullptr;
	std::string profilePath;
};

struct VansRuntimeCharacterControllerComponent
{
	VansEngine::VansCharacterControllerNode* controllerNode = nullptr;
};

struct VansRuntimeVehicleComponent
{
	VansEngine::VansPhysicsVehicle* vehicle = nullptr;
};

struct VansRuntimeAnimationComponent
{
	VansGraphics::VansAnimationNode* animationNode = nullptr;
};

struct VansRuntimeRagdollComponent
{
	VansGraphics::VansAnimationNode* animationNode = nullptr;
	std::uint8_t initialDriveMode = 0;
	std::string profilePath;
	std::string profileName;
	int configuredBodyCount = 0;
	int configuredJointCount = 0;
};

struct VansRuntimeAudioComponent
{
	VansEngine::VansAudioNode* audioNode = nullptr;
	VansEngine::VansAudioSourceBinding* sourceBinding = nullptr;
	std::string sourceName;
	VansEngine::AudioConeSettings coneSettings;
	bool dopplerEnabled = false;
	bool hasLastAudioPosition = false;
	float lastAudioPositionX = 0.0f;
	float lastAudioPositionY = 0.0f;
	float lastAudioPositionZ = 0.0f;
	VansEngine::AudioOcclusionSettings occlusionSettings;
	VansEngine::AudioOcclusionState occlusionState;
};

struct VansRuntimeAudioReverbZoneComponent
{
	std::string shape;
	std::string preset;
	std::string presetAssetGuid;
	VansEngine::AudioReverbPresetParameters presetParameters;
	bool overridePresetParameters = false;
	float radius = 8.0f;
	float halfExtentX = 4.0f;
	float halfExtentY = 4.0f;
	float halfExtentZ = 4.0f;
	float fadeDistance = 2.0f;
	float wetGain = 0.6f;
	int priority = 0;
};

struct VansRuntimeUIComponent
{
	std::vector<std::string> autoOpenScreens;
	std::vector<std::string> preloadScreens;
	std::vector<std::uint64_t> openScreens;
};

enum class VansRuntimeScriptFieldType : std::uint8_t
{
	Null,
	Bool,
	Int,
	Float,
	String,
	ObjectReference
};

enum class VansRuntimeScriptState : std::uint8_t
{
	Unloaded,
	Loading,
	Active,
	Disabled,
	Faulted,
	Destroyed
};

struct VansRuntimeScriptFieldValue
{
	VansRuntimeScriptFieldType type = VansRuntimeScriptFieldType::Null;
	bool boolValue = false;
	std::int64_t intValue = 0;
	double floatValue = 0.0;
	std::string stringValue;
	Vans::SerializedObjectReferenceValue objectReference;
};

struct VansRuntimeScriptComponent
{
	std::string scriptPath;
	std::string entryName;
	std::unordered_map<std::string, VansRuntimeScriptFieldValue> serializedFields;
	bool enableRequested = true;
	VansRuntimeScriptState state = VansRuntimeScriptState::Unloaded;
	bool isValid = false;
	bool hasStarted = false;
};

struct VansRuntimeVideoComponent
{
	VansGraphics::VansVideoTexture* videoTexture = nullptr;
	VansGraphics::VansVideoManager* videoManager = nullptr;
	int bindlessFirstSlot = -1;
};

struct VansRuntimeParticleComponent
{
	VansGraphics::VansParticleRuntime* runtime = nullptr;
	VansGraphics::VansParticleRenderNode* renderNode = nullptr;
	bool playOnAwake = false;
	bool isPlaying = false;
	float playTime = 0.0f;
	bool hasWorldPositionOverride = false;
	float worldPositionOverrideX = 0.0f;
	float worldPositionOverrideY = 0.0f;
	float worldPositionOverrideZ = 0.0f;
};

struct VansRuntimeCameraComponent
{
	VansGraphics::VansCamera* camera = nullptr;
};

enum class VansRuntimeLightKind : std::uint8_t
{
	Directional,
	Point,
	Spot,
	Rect
};

struct VansRuntimeLightComponent
{
	VansGraphics::VansLightManager* lightManager = nullptr;
	int lightIndex = -1;
	VansRuntimeLightKind kind = VansRuntimeLightKind::Directional;
};

inline std::uint16_t VansRuntimeComponentTypeIdForKey(const std::string& key)
{
	if (key == "render") return VansRuntimeComponentType_Render;
	if (key == "physics") return VansRuntimeComponentType_Physics;
	if (key == "cloth") return VansRuntimeComponentType_Cloth;
	if (key == "charController") return VansRuntimeComponentType_CharacterController;
	if (key == "directional_light") return VansRuntimeComponentType_DirectionalLight;
	if (key == "point_light") return VansRuntimeComponentType_PointLight;
	if (key == "spot_light") return VansRuntimeComponentType_SpotLight;
	if (key == "rect_light") return VansRuntimeComponentType_RectLight;
	if (key == "camera") return VansRuntimeComponentType_Camera;
	if (key == "audio") return VansRuntimeComponentType_Audio;
	if (key == "audio_reverb_zone") return VansRuntimeComponentType_AudioReverbZone;
	if (key == "audio_volume") return VansRuntimeComponentType_AudioVolume;
	if (key == "video") return VansRuntimeComponentType_Video;
	if (key == "particle") return VansRuntimeComponentType_Particle;
	if (key == "animation") return VansRuntimeComponentType_Animation;
	if (key == "ragdoll") return VansRuntimeComponentType_Ragdoll;
	if (key == "vehicle") return VansRuntimeComponentType_Vehicle;
	if (key == "uicontroller") return VansRuntimeComponentType_UI;
	if (key == "ui") return VansRuntimeComponentType_UI;
	if (key == "luascript") return VansRuntimeComponentType_Script;
	if (key == "script") return VansRuntimeComponentType_Script;
	if (key == "transform") return VansRuntimeComponentType_Transform;
	if (key == "timeline") return VansRuntimeComponentType_Timeline;
	return VansInvalidComponentTypeId;
}
}
