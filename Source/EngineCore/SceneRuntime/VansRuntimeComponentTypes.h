#pragma once
#include "../SceneCore/VansComponentTypeCatalog.h"

#include "VansRuntimeHandle.h"

#include "../AICore/VansAIRuntimeComponents.h"
#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AudioCore/VansAudioDirectionality.h"
#include "../AudioCore/VansAudioOcclusion.h"
#include "../AudioCore/VansAudioReverbPreset.h"
#include "../TimelineRuntime/VansTimelineComponent.h"

#include <cstdint>
#include <memory>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
class VansAnimationNode;
class VansCamera;
class VansLightManager;
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
class VansActionHost;

bool VansRuntimeComponentTypeMatches(
	std::uint16_t typeId,
	const std::type_info& valueType);

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
	std::string profileAssetGuid;
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
	std::uint64_t skeletonInstanceId = 0;
	std::uint32_t skeletonInstanceGeneration = 0;
};

struct VansRuntimeRagdollComponent
{
	VansGraphics::VansAnimationNode* animationNode = nullptr;
	std::uint8_t initialDriveMode = 0;
	std::string profileAssetGuid;
	std::string profileName;
	int configuredBodyCount = 0;
	int configuredJointCount = 0;
};

struct VansRuntimeAudioComponent
{
	VansEngine::VansAudioNode* audioNode = nullptr;
	VansEngine::VansAudioSourceBinding* sourceBinding = nullptr;
	std::string assetGuid;
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

enum class VansRuntimeAudioEnvironmentKind : std::uint8_t
{
	ReverbZone,
	Volume
};

struct VansRuntimeUIComponent
{
	std::vector<std::string> autoOpenScreenAssetGuids;
	std::vector<std::string> preloadScreenAssetGuids;
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
	std::string assetGuid;
	VansGraphics::VansVideoTexture* videoTexture = nullptr;
	VansGraphics::VansVideoManager* videoManager = nullptr;
	int bindlessFirstSlot = -1;
};

struct VansRuntimeParticleComponent
{
	std::string assetGuid;
	VansGenerationHandle instance;
	bool playOnAwake = false;
	bool hasWorldPositionOverride = false;
	float worldPositionOverrideX = 0.0f;
	float worldPositionOverrideY = 0.0f;
	float worldPositionOverrideZ = 0.0f;
};

struct VansRuntimeCameraComponent
{
	VansGraphics::VansCamera* camera = nullptr;
};

struct VansRuntimeActionHostComponent
{
	std::shared_ptr<VansActionHost> host;
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

}
