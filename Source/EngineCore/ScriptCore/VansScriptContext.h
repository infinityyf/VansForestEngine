#pragma once
#include "../AudioCore/VansAudioOcclusion.h"
#include "../AudioCore/VansAudioReverbPreset.h"
#include "../AudioCore/VansAudioSourceBinding.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "VansScriptTypes.h"

#include "../EventCore/VansEventConnection.h"
#include "../PhysicsCore/VansPhysicsEvents.h"
#include "../PhysicsCore/VansRagdollSystem.h"
#include "../ParticleCore/VansParticleAsset.h"
#include "../ParticleCore/VansParticleRuntime.h"
#include "../RuntimeUI/Public/VansUIRuntimeHandles.h"
#include "../SceneRuntime/VansRuntimeHandle.h"
#include "../SceneCore/VansSceneLocalVolumetricFogComponentConfig.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
}

class VansScriptObject;
class VansLuaScriptComponent;

void VansInstallLuaProjectSearchPath(
	lua_State* luaState,
	const std::filesystem::path& projectRoot);

namespace VansGraphics
{
class VansAnimationNode;
class VansCamera;
class VansLightManager;
class VansMaterialManager;
class VansParticleRenderNode;
class VansRenderNode;
class VansScene;
class VansVideoManager;
class VansVideoTexture;
}

namespace VansEngine
{
class VansAudioManager;
class VansAudioNode;
class VansCharacterControllerNode;
class VansClothNode;
class VansPhysicsNode;
class VansPhysicsVehicle;
}

enum class VansScriptLightIndexKind : std::uint8_t
{
	Directional,
	Point,
	Spot,
	Rect
};

struct VansScriptPhysicsEventInfo
{
	std::string otherName;
	std::uint32_t otherTransformID = 0;
	float contactPoint[3] = { 0.0f, 0.0f, 0.0f };
	float contactNormal[3] = { 0.0f, 0.0f, 0.0f };
	float impulse = 0.0f;
};

enum class VansLuaScriptState : std::uint8_t
{
	Unloaded,
	Loading,
	Active,
	Disabled,
	Faulted,
	Destroyed
};

class VansScriptComponent
{
public:
	std::string m_ComponentName;
	std::string m_ComponentGuid;
	bool m_Enabled = true;

	virtual ~VansScriptComponent() = default;

	void SetEnabled(bool enabled);
	bool IsEnabled() const { return m_Enabled; }
	bool IsEffectivelyEnabled() const { return m_EffectiveEnabled; }
	virtual void MirrorRuntimeEnabledState(bool selfEnabled, bool effectiveEnabled);
	virtual void MirrorRuntimeOpenScreens(const std::vector<std::uint64_t>& openScreens) {}
	void ApplyOwnerActive(bool active);
	void Destroy();
	virtual void RebindSceneLightIndex(
		VansScriptLightIndexKind kind,
		int oldIndex,
		int newIndex) {}

protected:
	virtual void OnEnable() {}
	virtual void OnDisable() {}
	virtual void OnDestroy() {}

private:
	void RefreshEffectiveEnabled();

	bool m_OwnerActive = true;
	bool m_EffectiveEnabled = true;
	bool m_Destroyed = false;
};

class VansScriptObject
{
public:
	std::string m_EntityGuid;
	std::string m_ObjectName;
	std::vector<VansScriptComponent*> m_Components;
	std::uint32_t m_TransformID = 0;
	bool m_OwnsTransform = false;
	bool m_Active = true;

	void SetActive(bool active);
	bool IsActive() const { return m_Active; }
	void AddComponent(VansScriptComponent* comp);
	std::uint32_t ReleaseOwnedTransform();

	template<typename T>
	T* GetComponent() const
	{
		for (auto* comp : m_Components)
		{
			if (auto* casted = dynamic_cast<T*>(comp))
				return casted;
		}
		return nullptr;
	}

	~VansScriptObject();
};

class VansScriptRenderComponent : public VansScriptComponent
{
public:
	VansGraphics::VansRenderNode* m_RenderNode = nullptr;
	std::vector<VansGraphics::VansRenderNode*> m_RenderNodes;
protected:
	void OnEnable() override;
	void OnDisable() override;
};

class VansScriptPhysicsComponent : public VansScriptComponent
{
public:
	VansEngine::VansPhysicsNode* m_PhysicsNode = nullptr;
protected:
	void OnEnable() override;
	void OnDisable() override;
};

class VansScriptClothComponent : public VansScriptComponent
{
public:
	VansEngine::VansClothNode* m_ClothNode = nullptr;
	std::string m_ProfilePath;
protected:
	void OnEnable() override;
	void OnDisable() override;
};

class VansScriptVehicleComponent : public VansScriptComponent
{
public:
	VansEngine::VansPhysicsVehicle* m_Vehicle = nullptr;
};

class VansScriptAnimationComponent : public VansScriptComponent
{
public:
	VansGraphics::VansAnimationNode* m_AnimNode = nullptr;
protected:
	void OnEnable() override;
	void OnDisable() override;
};

class VansScriptRagdollComponent : public VansScriptComponent
{
public:
	VansScriptRagdollComponent();
	VansGraphics::VansAnimationNode* m_AnimNode = nullptr;
	VansEngine::RagdollDriveMode m_InitialDriveMode = VansEngine::RagdollDriveMode::Animation;
	std::string m_ProfilePath;
	std::string m_ProfileName;
	int m_ConfiguredBodyCount = 0;
	int m_ConfiguredJointCount = 0;

	void SetDriveMode(int mode);
	void SetDriveModeWithVelocity(int mode, float vx, float vy, float vz);
	int GetDriveMode() const;
	void SetBlendWeight(float weight);
	float GetBlendWeight() const;
	bool HasRuntimeRagdoll() const;
	int GetRuntimeBodyCount() const;
	int GetRuntimeJointCount() const;
	void ApplyImpulse(const std::string& boneName, float ix, float iy, float iz);

protected:
	void OnEnable() override;
	void OnDisable() override;
};

class VansScriptCharacterControllerComponent : public VansScriptComponent
{
public:
	VansScriptCharacterControllerComponent();
	VansEngine::VansCharacterControllerNode* m_ControllerNode = nullptr;
	void BindFollowRagdoll(VansScriptRagdollComponent* ragdollComp, const std::string& rootBone = "pelvis");
	void ClearFollowRagdoll();
	bool IsFollowRagdollEnabled() const;
protected:
	void OnEnable() override;
	void OnDisable() override;
};

class VansScriptDirectionalLightComponent : public VansScriptComponent
{
public:
	VansScriptDirectionalLightComponent() { m_ComponentName = "DirectionalLight"; }
	VansGraphics::VansLightManager* m_LightManager = nullptr;
	int m_LightIndex = -1;
	void RebindSceneLightIndex(
		VansScriptLightIndexKind kind,
		int oldIndex,
		int newIndex) override;
};

class VansScriptPointLightComponent : public VansScriptComponent
{
public:
	VansScriptPointLightComponent() { m_ComponentName = "PointLight"; }
	VansGraphics::VansLightManager* m_LightManager = nullptr;
	int m_LightIndex = -1;
	void RebindSceneLightIndex(
		VansScriptLightIndexKind kind,
		int oldIndex,
		int newIndex) override;
};

class VansScriptSpotLightComponent : public VansScriptComponent
{
public:
	VansScriptSpotLightComponent() { m_ComponentName = "SpotLight"; }
	VansGraphics::VansLightManager* m_LightManager = nullptr;
	int m_LightIndex = -1;
	void RebindSceneLightIndex(
		VansScriptLightIndexKind kind,
		int oldIndex,
		int newIndex) override;
};

class VansScriptVideoComponent;

class VansScriptRectLightComponent : public VansScriptComponent
{
public:
	VansScriptRectLightComponent() { m_ComponentName = "RectLight"; }
	VansGraphics::VansLightManager* m_LightManager = nullptr;
	int m_LightIndex = -1;
	std::string m_EmissiveTexturePath;
	VansScriptVideoComponent* m_VideoComponent = nullptr;
	void RebindSceneLightIndex(
		VansScriptLightIndexKind kind,
		int oldIndex,
		int newIndex) override;
};

class VansScriptCameraComponent : public VansScriptComponent
{
public:
	VansScriptCameraComponent() { m_ComponentName = "camera"; }
	VansGraphics::VansCamera* m_Camera = nullptr;
protected:
	void OnEnable() override;
	void OnDisable() override;
	void OnDestroy() override;
};

class VansScriptAudioComponent : public VansScriptComponent
{
public:
	VansScriptAudioComponent() { m_ComponentName = "Audio"; }
	VansEngine::VansAudioSourceBinding m_Source;
	VansEngine::AudioOcclusionSettings m_OcclusionSettings;
	VansEngine::AudioOcclusionState m_OcclusionState;
	VansEngine::AudioConeSettings m_ConeSettings;
	bool m_DopplerEnabled = false;
	bool m_HasLastAudioPosition = false;
	float m_LastAudioPositionX = 0.0f;
	float m_LastAudioPositionY = 0.0f;
	float m_LastAudioPositionZ = 0.0f;
	bool SwitchSource(const std::string& name);
protected:
	void OnEnable() override;
	void OnDisable() override;
	void OnDestroy() override;
};

class VansScriptAudioReverbZoneComponent : public VansScriptComponent
{
public:
	VansScriptAudioReverbZoneComponent() { m_ComponentName = "AudioReverbZone"; }
	std::string m_Shape = "sphere";
	std::string m_Preset = "generic";
	std::string m_PresetAssetGuid;
	VansEngine::AudioReverbPresetParameters m_PresetParameters;
	bool m_OverridePresetParameters = false;
	float m_Radius = 8.0f;
	float m_HalfExtentX = 4.0f;
	float m_HalfExtentY = 4.0f;
	float m_HalfExtentZ = 4.0f;
	float m_FadeDistance = 2.0f;
	float m_WetGain = 0.6f;
	int m_Priority = 0;
};

class VansScriptLocalVolumetricFogComponent : public VansScriptComponent
{
public:
	VansScriptLocalVolumetricFogComponent()
	{
		m_ComponentName = "LocalVolumetricFog";
	}

	Vans::VansSceneLocalVolumetricFogComponentConfig m_Settings;
};

class VansScriptVideoComponent : public VansScriptComponent
{
public:
	VansScriptVideoComponent() { m_ComponentName = "Video"; }
	std::string m_VideoName;
	VansGraphics::VansVideoTexture* m_VideoTex = nullptr;
	VansGraphics::VansVideoManager* m_VideoManager = nullptr;
	int m_BindlessFirstSlot = -1;
	VansGraphics::VansMaterialManager* m_MaterialManagerRef = nullptr;
	bool SwitchSource(const std::string& name);

protected:
	void OnDestroy() override;
};

class VansScriptParticleComponent : public VansScriptComponent
{
public:
	VansScriptParticleComponent() { m_ComponentName = "Particle"; }
	std::string m_ParticleAssetPath;
	std::unique_ptr<VansGraphics::VansParticleAsset> m_ParticleAsset;
	std::unique_ptr<VansGraphics::VansParticleRuntime> m_Runtime;
	VansGraphics::VansParticleRenderNode* m_RenderNode = nullptr;
	bool m_PlayOnAwake = true;
	bool m_IsPlaying = false;
	float m_PlayTime = 0.0f;
	bool m_HasWorldPositionOverride = false;
	glm::vec3 m_WorldPositionOverride = glm::vec3(0.0f);

	void Play();
	void Stop();
	void Pause();
	void Restart();
	void SetWorldPosition(float x, float y, float z);
	void ClearWorldPositionOverride();
	bool LoadAsset(const std::string& path);
	void OnUpdate(float deltaTime);
	void MirrorRuntimeEnabledState(bool selfEnabled, bool effectiveEnabled) override;

protected:
	void OnEnable() override;
	void OnDisable() override;
	void OnDestroy() override;
};

class VansLuaScriptComponent : public VansScriptComponent
{
public:
	VansLuaScriptComponent() { m_Enabled = false; }
	~VansLuaScriptComponent() override;

	std::string m_ScriptPath;
	std::string m_EntryName;
	std::unordered_map<std::string, VansScriptSerializedFieldValue> m_SerializedFields;
	VansScriptObject* m_OwnerObject = nullptr;
	bool m_IsValid = false;
	bool m_EnableRequested = true;
	VansLuaScriptState m_State = VansLuaScriptState::Unloaded;

	int m_ModuleRef = LUA_NOREF;
	int m_InstanceRef = LUA_NOREF;
	int m_OnStartRef = LUA_NOREF;
	int m_OnEnableRef = LUA_NOREF;
	int m_OnDisableRef = LUA_NOREF;
	int m_UpdateRef = LUA_NOREF;
	int m_CollisionEnterRef = LUA_NOREF;
	int m_CollisionExitRef = LUA_NOREF;
	int m_TriggerEnterRef = LUA_NOREF;
	int m_TriggerExitRef = LUA_NOREF;
	bool m_HasStarted = false;

	bool Instantiate();
	void Enable();
	void Disable();
	void Teardown();
	void CallUpdate(float deltaTime);
	void CallOnCollisionEnter(const VansScriptPhysicsEventInfo& info);
	void CallOnCollisionExit(const VansScriptPhysicsEventInfo& info);
	void CallOnTriggerEnter(const VansScriptPhysicsEventInfo& info);
	void CallOnTriggerExit(const VansScriptPhysicsEventInfo& info);

protected:
	void OnEnable() override;
	void OnDisable() override;
	void OnDestroy() override;

private:
	friend class VansScriptContext;
	void CacheCallback(lua_State* luaState, const char* name, int& ref);
	void ReleaseLuaRefs(lua_State* luaState);
	void EnterFaultedState(const char* phase, const std::string& error);
};

class VansScriptUIComponent : public VansScriptComponent
{
public:
	VansScriptUIComponent() { m_ComponentName = "UIController"; }
	~VansScriptUIComponent() override;

	std::vector<std::string> m_AutoOpenScreens;
	std::vector<std::string> m_PreloadScreens;
	std::vector<VansRuntime::VansUIHandleId> m_OpenScreens;

	void Preload();
	void ReleasePreloaded();
	void OpenConfiguredScreens();
	void CloseOpenedScreens();
	void MirrorRuntimeOpenScreens(const std::vector<std::uint64_t>& openScreens) override;

protected:
	void OnEnable() override;
	void OnDisable() override;
};

class VansScriptContext
{
public:
	VansScriptContext() = default;
	~VansScriptContext();
	VansScriptContext(const VansScriptContext&) = delete;
	VansScriptContext& operator=(const VansScriptContext&) = delete;

	void VansScriptSetup();
	void VansScriptUpdate();
	void VansScriptUpdateNonCameraScripts();
	void VansScriptUpdateCameraScripts();

	void SetScene(VansGraphics::VansScene* scene);
	void AttachSceneWithoutRebuild(VansGraphics::VansScene* scene);
	void RegisterScriptComponent(VansScriptObject* owner, VansLuaScriptComponent* component);
	void UnregisterScriptComponent(VansLuaScriptComponent* component);
	VansGraphics::VansScene* GetScene() const { return m_Scene; }

	void ClearTrackedModules();
	void ShutdownLua();
	void ReloadAllLuaScripts();
	lua_State* GetLuaState() const { return m_LuaState; }
	const std::string& GetActiveProjectRoot() const { return m_ActiveProjectRoot; }
	void SetActiveProjectRoot(const std::string& projectRoot);

	static VansScriptContext* GetInstance() { return s_Instance; }

private:
	struct ScheduledScript
	{
		VansScriptObject* owner = nullptr;
		VansLuaScriptComponent* component = nullptr;
		std::string entityGuid;
		std::string componentGuid;
		Vans::VansEntityHandle runtimeEntity;
		Vans::VansComponentHandle runtimeComponent;
		bool cameraScript = false;
	};

	struct ScriptEventSubscriber
	{
		VansLuaScriptComponent* component = nullptr;
		std::string entityGuid;
		std::string componentGuid;
		Vans::VansEntityHandle runtimeEntity;
		Vans::VansComponentHandle runtimeComponent;
	};

	lua_State* m_LuaState = nullptr;
	std::thread::id m_LuaThreadId;
	VansGraphics::VansScene* m_Scene = nullptr;
	std::string m_ActiveProjectRoot;
	std::vector<ScheduledScript> m_ScheduledScripts;
	std::unordered_map<std::uint32_t, std::vector<ScriptEventSubscriber>> m_EventSubscribers;
	Vans::VansScopedEventConnections m_EventConnections;

	void VansScriptPreUpdate();
	void UpdateScriptComponents(bool cameraScriptsOnly, bool skipCameraScripts);
	void RebuildScriptSchedule();
	bool ResolveRuntimeHandles(ScheduledScript& scheduled) const;
	bool ResolveRuntimeHandles(ScriptEventSubscriber& subscriber) const;
	void HandlePhysicsContactEvent(const VansEngine::VansPhysicsContactEvent& event);
	void DispatchEventToObject(
		const VansEngine::VansPhysicsContactEvent& event,
		std::uint32_t selfTransformID,
		std::uint32_t otherTransformID,
		const std::string& otherName,
		const glm::vec3& contactPoint,
		const glm::vec3& contactNormal,
		float impulse);
	void AssertLuaThread() const;
	void RegisterLuaBindings();
	void InstallLuaSearchPath();
	void RefreshActiveProjectRoot();

	static VansScriptContext* s_Instance;
};
