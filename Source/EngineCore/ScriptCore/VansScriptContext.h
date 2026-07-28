#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "VansScriptTypes.h"

#include "../PhysicsCore/VansPhysicsEvents.h"
#include "../PhysicsCore/VansRagdollSystem.h"
#include "../ParticleCore/VansParticleAsset.h"
#include "../ParticleCore/VansParticleRuntime.h"

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
	void Destroy();

protected:
	virtual void OnEnable() {}
	virtual void OnDisable() {}
	virtual void OnDestroy() {}
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

class VansScriptTransform : public VansScriptComponent
{
public:
	int m_TransformID = 0;
};

class VansScriptRenderComponent : public VansScriptComponent
{
public:
	VansGraphics::VansRenderNode* m_RenderNode = nullptr;
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
};

class VansScriptPointLightComponent : public VansScriptComponent
{
public:
	VansScriptPointLightComponent() { m_ComponentName = "PointLight"; }
	VansGraphics::VansLightManager* m_LightManager = nullptr;
	int m_LightIndex = -1;
};

class VansScriptSpotLightComponent : public VansScriptComponent
{
public:
	VansScriptSpotLightComponent() { m_ComponentName = "SpotLight"; }
	VansGraphics::VansLightManager* m_LightManager = nullptr;
	int m_LightIndex = -1;
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
};

class VansScriptCameraComponent : public VansScriptComponent
{
public:
	VansScriptCameraComponent() { m_ComponentName = "camera"; }
	VansGraphics::VansCamera* m_Camera = nullptr;
protected:
	void OnEnable() override;
	void OnDisable() override;
};

class VansScriptAudioComponent : public VansScriptComponent
{
public:
	VansScriptAudioComponent() { m_ComponentName = "Audio"; }
	VansEngine::VansAudioNode* m_AudioNode = nullptr;
	VansEngine::VansAudioManager* m_AudioManager = nullptr;
	bool SwitchSource(const std::string& name);
protected:
	void OnEnable() override;
	void OnDisable() override;
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

protected:
	void OnEnable() override;
	void OnDisable() override;
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

private:
	friend class VansScriptContext;
	void CacheCallback(lua_State* luaState, const char* name, int& ref);
	void ReleaseLuaRefs(lua_State* luaState);
	void EnterFaultedState(const char* phase, const std::string& error);
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

	static VansScriptContext* GetInstance() { return s_Instance; }

private:
	struct ScheduledScript
	{
		VansScriptObject* owner = nullptr;
		VansLuaScriptComponent* component = nullptr;
		bool cameraScript = false;
	};

	lua_State* m_LuaState = nullptr;
	std::thread::id m_LuaThreadId;
	VansGraphics::VansScene* m_Scene = nullptr;
	std::string m_ActiveProjectRoot;
	std::vector<ScheduledScript> m_ScheduledScripts;
	std::unordered_map<std::uint32_t, std::vector<VansLuaScriptComponent*>> m_EventSubscribers;

	void VansScriptPreUpdate();
	void UpdateScriptComponents(bool cameraScriptsOnly, bool skipCameraScripts);
	void RebuildScriptSchedule();
	void DispatchPhysicsEvents();
	void DispatchEventToObject(
		const VansEngine::PhysicsEventData& event,
		std::uint32_t selfTransformID,
		std::uint32_t otherTransformID,
		const std::string& otherName,
		const glm::vec3& contactPoint,
		const glm::vec3& contactNormal,
		float impulse);
	void AssertLuaThread() const;
	void RegisterLuaBindings();
	void InstallLuaSearchPath();

	static VansScriptContext* s_Instance;
};
