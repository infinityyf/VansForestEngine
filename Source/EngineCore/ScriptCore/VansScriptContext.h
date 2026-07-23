#pragma once

// Must define NOMINMAX before any header that may pull in windows.h (e.g. pybind11/Python.h)
// to prevent min/max macros from breaking GLM's std::numeric_limits usage.
#ifndef NOMINMAX
#define NOMINMAX
#endif

//创建一个python的上下文环境用于调用python的逻辑
//1. 每帧会调用VansScriptUpdate函数，这里会调用对应python上下文的函数
//2. 剩下的交给python, python里会去找梭有的python对象，调用update函数，这个update函数则是引擎暴露出去的接口

#include <vector>
#include <string>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <thread>
#include <pybind11/embed.h>
#include "../PhysicsCore/VansPhysicsEvents.h"
#include "../PhysicsCore/VansRagdollSystem.h"
#include "../../../../ForestExporter/VansPhysicsEventInfo.h"
#include "../ParticleCore/VansParticleAsset.h"
#include "../ParticleCore/VansParticleRuntime.h"
namespace py = pybind11;

class VansScriptObject;
class VanPyScriptComponent;

// Forward declarations for Component sub-classes
namespace VansGraphics { class VansRenderNode; class VansScene; class VansAnimationNode; class VansLightManager; class VansCamera; class VansVideoTexture; class VansVideoManager; class VansMaterialManager; class VansParticleRenderNode; }
namespace VansEngine  { class VansPhysicsNode; class VansClothNode; class VansPhysicsVehicle; class VansCharacterControllerNode; class VansAudioNode; class VansAudioManager; }

class VansScriptContext
{
private:
	// ---- Hot-reload infrastructure ----
	std::string m_ScriptDir;  // path to EngineExported/

	// Track last-write-time for each imported .py file (keyed by script path)
	struct PyModuleInfo {
		py::module   module;
		std::string  scriptPath;
		std::string  moduleName;   // Python module name registered in sys.modules
		std::filesystem::file_time_type lastWriteTime;
		std::filesystem::path           filePath;     // absolute file path
		std::unordered_map<std::string, py::object> classes;
		bool loadFailed = false;
	};
	std::unordered_map<std::string, PyModuleInfo> m_TrackedPyModules;  // key = canonical absolute path

	struct ScheduledScript {
		VansScriptObject* owner = nullptr;
		VanPyScriptComponent* component = nullptr;
		bool cameraScript = false;
	};
	std::vector<ScheduledScript> m_ScheduledScripts;
	std::unordered_map<uint32_t, std::vector<VanPyScriptComponent*>> m_EventSubscribers;

	std::unique_ptr<py::scoped_interpreter> m_Interpreter;
	std::thread::id m_PythonThreadId;
	std::vector<std::string> m_ProjectPythonPaths;
	std::string m_ActiveProjectRoot;

	// Accumulator so we don't stat the filesystem every single frame
	float m_FileCheckAccumulator = 0.0f;
	static constexpr float FILE_CHECK_INTERVAL = 0.5f;  // seconds

	// Internal helpers
	void CheckAndReloadPyScripts();
	void VansScriptPreUpdate();
	void UpdateScriptComponents(bool cameraScriptsOnly, bool skipCameraScripts);
	bool ReloadPyModule(PyModuleInfo& moduleInfo);
	void RebuildScriptSchedule();
	void AssertPythonThread() const;
	static std::string CanonicalScriptKey(const std::filesystem::path& path);
	static std::string MakeScriptModuleName(const std::string& canonicalPath);

	// ── 物理事件调度 ─────────────────────────────────────────────────
	void DispatchPhysicsEvents();
	void DispatchEventToObject(
		const VansEngine::PhysicsEventData& event,
		uint32_t selfTransformID, uint32_t otherTransformID,
		const std::string& otherName,
		const glm::vec3& contactPoint, const glm::vec3& contactNormal, float impulse);

	// Scene pointer for iterating VanPyScriptComponents during update
	VansGraphics::VansScene* m_Scene = nullptr;

public:
	VansScriptContext() = default;
	~VansScriptContext();
	VansScriptContext(const VansScriptContext&) = delete;
	VansScriptContext& operator=(const VansScriptContext&) = delete;

	bool ResolveScriptClass(const std::string& scriptPath,
		const std::string& className,
		const std::filesystem::path& absPath,
		std::string& moduleName,
		py::object& scriptClass);
	void ConfigureProjectPythonPaths(const std::string& projectRoot);

	void VansScriptSetup();

	void VansScriptUpdate();
	void VansScriptUpdateNonCameraScripts();
	void VansScriptUpdateCameraScripts();

	// Set the active scene so the update loop can iterate objects
	void SetScene(VansGraphics::VansScene* scene);
	void RegisterScriptComponent(VansScriptObject* owner, VanPyScriptComponent* component);
	void UnregisterScriptComponent(VanPyScriptComponent* component);

	// Read the active scene pointer (used by bridge lambdas)
	VansGraphics::VansScene* GetScene() const { return m_Scene; }

	// ── 场景切换时清空已跟踪的 Python 模块 ─────────────────────────
	// 释放 py::module 引用，清空 m_TrackedPyModules，防止跨场景累积。
	void ClearTrackedModules();
	void ShutdownPython();

	// Explicit reload called from editor UI
	void ReloadAllPyScripts();

	// Native extension modules cannot be unloaded safely from the embedded interpreter.
	// This entry point remains for the editor action and reports that a restart is required.
	void ReloadPydModule(const std::string& moduleName = "vanscomponent");

	// ── 项目 Python 虚拟环境管理 ─────────────────────────────────────
	// 将项目 requirements.txt 同步到项目私有的 .vans/python/site-packages。
	// 仅由显式编辑器操作调用，运行时启动路径不会自动执行 pip。
	void SetupProjectVenv(const std::string& projectRoot);

	// Access from editor windows
	static VansScriptContext* GetInstance() { return s_Instance; }

private:
	static VansScriptContext* s_Instance;
};

class VansScriptComponent
{
public:
	std::string m_ComponentName;
	bool m_Enabled = true;

	virtual ~VansScriptComponent() = default;

	void SetEnabled(bool enabled)
	{
		if (m_Enabled == enabled) return;
		m_Enabled = enabled;
		if (enabled)
			OnEnable();
		else
			OnDisable();
	}

	bool IsEnabled() const { return m_Enabled; }

	void Destroy()
	{
		OnDestroy();
		m_Enabled = false;
	}

protected:
	virtual void OnEnable()  {}
	virtual void OnDisable() {}
	virtual void OnDestroy() {}
};

class VansScriptObject
{
public:
	std::string m_EntityGuid;
	std::string m_ObjectName;

	// All components owned by this object (polymorphic pointers).
	// The object owns the Component memory, but NOT the Node pointed to by each component.
	std::vector<VansScriptComponent*> m_Components;

	// Transform ID shared across all components of this object.
	// Typically taken from the render component's RenderNode.
	uint32_t m_TransformID = 0;
	bool m_OwnsTransform = false;

	// ── Entity level enable/disable (like Unity GameObject.SetActive) ──
	bool m_Active = true;

	void SetActive(bool active)
	{
		if (m_Active == active) return;
		m_Active = active;
		for (auto* comp : m_Components)
		{
			if (comp)
				comp->SetEnabled(active);
		}
	}

	bool IsActive() const { return m_Active; }

	// ── Query helpers ────────────────────────────────────────────────
	template<typename T>
	T* GetComponent() const
	{
		for (auto* comp : m_Components)
		{
			T* casted = dynamic_cast<T*>(comp);
			if (casted) return casted;
		}
		return nullptr;
	}

	void AddComponent(VansScriptComponent* comp)
	{
		m_Components.push_back(comp);
	}

	~VansScriptObject();
};


class VansScriptTransform : public VansScriptComponent
{
public:
	
	int m_TransformID;
};

// ── Render Component ────────────────────────────────────────────────────────
// Holds a non-owning pointer to a VansRenderNode managed by VansScene.
class VansScriptRenderComponent : public VansScriptComponent
{
public:
	VansGraphics::VansRenderNode* m_RenderNode = nullptr;

protected:
	void OnEnable()  override;
	void OnDisable() override;
};

// ── Physics Component ───────────────────────────────────────────────────────
// Holds a non-owning pointer to a VansPhysicsNode managed by VansScene.
class VansScriptPhysicsComponent : public VansScriptComponent
{
public:
	VansEngine::VansPhysicsNode* m_PhysicsNode = nullptr;

protected:
	void OnEnable()  override;
	void OnDisable() override;
};

// ── Cloth Component ─────────────────────────────────────────────────────────
// Holds a non-owning pointer to a VansClothNode managed by VansScene.
class VansScriptClothComponent : public VansScriptComponent
{
public:
	VansEngine::VansClothNode* m_ClothNode = nullptr;
	// 关联的 .clothprofile 文件路径（相对路径）；内联配置时为空
	std::string m_ProfilePath;

protected:
	void OnEnable()  override;
	void OnDisable() override;
};

// ── Vehicle Component ───────────────────────────────────────────────────────
// Holds a non-owning pointer to a VansPhysicsVehicle managed by VansScene.
class VansScriptVehicleComponent : public VansScriptComponent
{
public:
	VansEngine::VansPhysicsVehicle* m_Vehicle = nullptr;

protected:
	void OnEnable()  override {}
	void OnDisable() override {}
};

// ── Animation Component ─────────────────────────────────────────────────────
// 持有对 VansScene 管理的 VansAnimationNode 的非拥有指针。
// 通过此组件可以在 Python 中控制 AnimationController 的状态。
class VansScriptAnimationComponent : public VansScriptComponent
{
public:
	// 非拥有指针，生命周期由 VansScene 管理
	VansGraphics::VansAnimationNode* m_AnimNode = nullptr;

protected:
	void OnEnable()  override;
	void OnDisable() override;
};

// ── Ragdoll Component ───────────────────────────────────────────────────────
// 持有对 VansScene 管理的 VansAnimationNode 的非拥有指针。
// 通过此组件可以在 Python/C++ 中切换动画驱动、物理驱动和混合驱动。
class VansScriptRagdollComponent : public VansScriptComponent
{
public:
	VansScriptRagdollComponent()
	{
		m_ComponentName = "Ragdoll";
	}

	VansGraphics::VansAnimationNode* m_AnimNode = nullptr;
	VansEngine::RagdollDriveMode m_InitialDriveMode = VansEngine::RagdollDriveMode::Animation;
	std::string m_ProfilePath;
	std::string m_ProfileName;
	int m_ConfiguredBodyCount = 0;
	int m_ConfiguredJointCount = 0;

protected:
	void OnEnable()  override;
	void OnDisable() override;

public:
	void SetDriveMode(int mode);
	void SetDriveModeWithVelocity(int mode, float vx, float vy, float vz);
	int GetDriveMode() const;
	void SetBlendWeight(float weight);
	float GetBlendWeight() const;
	bool HasRuntimeRagdoll() const;
	int GetRuntimeBodyCount() const;
	int GetRuntimeJointCount() const;
	void ApplyImpulse(const std::string& boneName, float ix, float iy, float iz);
};

// ── Character Controller Component ─────────────────────────────────────────
// 持有对 VansScene 管理的 VansCharacterControllerNode 的非拥有指针。
// 通过此组件可以在 Python/C++ 脚本中调用 QueueMove() 驱动角色运动。
class VansScriptCharacterControllerComponent : public VansScriptComponent
{
public:
	VansScriptCharacterControllerComponent()
	{
		m_ComponentName = "CharacterController";
	}

	// 非拥有指针，实际 Node 由 VansScene::m_CharControllerNodes 管理
	VansEngine::VansCharacterControllerNode* m_ControllerNode = nullptr;

protected:
	void OnEnable()  override;
	void OnDisable() override;

public:
	// ── Ragdoll 接管接口 ───────────────────────────────────
	// 将 ragdollComp 的 AnimNode 绑定到 ControllerNode，开启接管模式
	void BindFollowRagdoll(VansScriptRagdollComponent* ragdollComp,
	                       const std::string& rootBone = "pelvis");
	void ClearFollowRagdoll();
	bool IsFollowRagdollEnabled() const;
};

// ── Directional Light Component ─────────────────────────────────────────────
// 持有对 VansLightManager 中方向光的非拥有索引引用。
// 每帧由 VansScene::SyncLightTransforms 将对象旋转变换写入 m_Direction。
class VansScriptDirectionalLightComponent : public VansScriptComponent
{
public:
	VansScriptDirectionalLightComponent() { m_ComponentName = "DirectionalLight"; }

	// 非拥有指针，生命周期由 VansScene::m_LightManager 管理
	VansGraphics::VansLightManager* m_LightManager = nullptr;

	// 该灯光在 m_LightManager::m_DirectionalLights 中的索引
	int m_LightIndex = -1;

protected:
	void OnEnable()  override {}
	void OnDisable() override {}
};

// ── Point Light Component ────────────────────────────────────────────────────
// 持有对 VansLightManager 中点光源的非拥有索引引用。
// 每帧由 VansScene::SyncLightTransforms 将对象位置写入 m_Position。
class VansScriptPointLightComponent : public VansScriptComponent
{
public:
	VansScriptPointLightComponent() { m_ComponentName = "PointLight"; }

	// 非拥有指针，生命周期由 VansScene::m_LightManager 管理
	VansGraphics::VansLightManager* m_LightManager = nullptr;

	// 该灯光在 m_LightManager::m_PointLights 中的索引
	int m_LightIndex = -1;

protected:
	void OnEnable()  override {}
	void OnDisable() override {}
};

// ── Spot Light Component ─────────────────────────────────────────────────────
// 持有对 VansLightManager 中聚光灯的非拥有索引引用。
// 每帧由 VansScene::SyncLightTransforms 将对象位置和旋转写入 m_Position/m_Direction。
class VansScriptSpotLightComponent : public VansScriptComponent
{
public:
	VansScriptSpotLightComponent() { m_ComponentName = "SpotLight"; }

	// 非拥有指针，生命周期由 VansScene::m_LightManager 管理
	VansGraphics::VansLightManager* m_LightManager = nullptr;

	// 该灯光在 m_LightManager::m_SpotLights 中的索引
	int m_LightIndex = -1;

protected:
	void OnEnable()  override {}
	void OnDisable() override {}
};
// ── Video Component ─────────────────────────────────────────────────────────
// 持有对 VansVideoManager 中视频纹理的非拥有指针。
// 通过此组件可以统一控制视频的播放、暂停。
// Emission 材质和面光源均通过此组件获取 GPU 纹理数据，不直接持有视频路径参数。
class VansScriptVideoComponent : public VansScriptComponent
{
public:
	VansScriptVideoComponent() { m_ComponentName = "Video"; }

	// Video runtime name resolved from an asset reference.
	std::string m_VideoName;

	// 非拥有指针，生命周期由 VansScene::m_VideoManager 管理
	VansGraphics::VansVideoTexture* m_VideoTex = nullptr;

	// 反向引用，场景加载时由 VansSceneLoader 写入，不拥有生命周期
	VansGraphics::VansVideoManager* m_VideoManager = nullptr;

	// Bindless GPU 纹理槽偏移：该组件对应的材质在全局 Bindless 纹理数组中的起始索引。
	// 值为 materialIndex * kSlotsPerMat（对 EmissiveMaterial 为 5 槽）。
	// -1 表示该组件不绑定 Bindless 槽位（如 RectLight 单独使用 emissive 数组层）。
	// 由 LoadSceneForRendering 在 PreparePBRMaterialData 之后写入。
	int m_BindlessFirstSlot = -1;

	// 材质管理器引用（非拥有指针）：用于在切换视频源时更新 Bindless 描述符集。
	// 由 LoadSceneForRendering 在 PreparePBRMaterialData 之后写入。
	VansGraphics::VansMaterialManager* m_MaterialManagerRef = nullptr;

	// 运行时切换视频资源（仅限已加载资源）。
	// 切换后，EmissiveMaterial / RectLight 下一帧自动使用新纹理。
	// 返回 true 表示切换成功，false 表示资源名未找到（保持原状）。
	bool SwitchSource(const std::string& name);

protected:
	void OnEnable()  override {}
	void OnDisable() override {}
};

// ── Rect Light Component (area light, evaluated via LTC) ─────────────────────
// 持有对 VansLightManager 中面光源的非拥有索引引用。
// 每帧由 VansScene::SyncLightTransforms 将对象 Position/Right/Up/Normal 写入对应字段。
class VansScriptRectLightComponent : public VansScriptComponent
{
public:
	VansScriptRectLightComponent() { m_ComponentName = "RectLight"; }

	// 非拥有指针，生命周期由 VansScene::m_LightManager 管理
	VansGraphics::VansLightManager* m_LightManager = nullptr;

	// 该灯光在 m_LightManager::m_RectLights 中的索引
	int m_LightIndex = -1;

	// 发光贴图路径（仅 CPU 端，不上传 GPU）。空字符串表示无贴图。
	std::string m_EmissiveTexturePath;

	// 视频组件引用（非拥有指针）。非空时每帧从此组件获取视频帧写入发光数组层。
	// 播放参数控制统一由 VansScriptVideoComponent 负责，面光源只读取 GPU 数据。
	VansScriptVideoComponent* m_VideoComponent = nullptr;

protected:
	void OnEnable()  override {}
	void OnDisable() override {}
};// ── Camera Component ────────────────────────────────────────────────────────────────────
// 持有对 VansCamera 的非拥有指针；VansCamera 由 VansScene 生命周期管理。
// Transform 的 position/rotation(pitch/yaw) 每帧由 VansCamera::SyncFromTransform 同步。
class VansScriptCameraComponent : public VansScriptComponent
{
public:
	VansScriptCameraComponent() { m_ComponentName = "camera"; }

	// 非拥有指针，生命周期由 VansScene::m_Camera 管理
	VansGraphics::VansCamera* m_Camera = nullptr;

protected:
	void OnEnable()  override;
	void OnDisable() override;
};
// ── Audio Component ─────────────────────────────────────────────────────────
// 持有对 VansAudioManager 中音频节点的非拥有指针。
// 通过此组件可以在 Python/C++ 中控制音频的播放、暂停、停止及参数调整。
class VansScriptAudioComponent : public VansScriptComponent
{
public:
	VansScriptAudioComponent() { m_ComponentName = "Audio"; }

	// 非拥有指针，生命周期由 VansScene::m_AudioManager 管理
	VansEngine::VansAudioNode* m_AudioNode = nullptr;

	// 反向引用，场景加载时由 VansSceneLoader 写入，不拥有生命周期
	VansEngine::VansAudioManager* m_AudioManager = nullptr;

protected:
	void OnEnable()  override;
	void OnDisable() override;

public:
	// 运行时切换音频资源（仅限已加载资源）。
	// 不自动播放，由调用方决定是否立即调用 Play()。
	// 返回 true 表示切换成功，false 表示资源名未找到（保持原状）。
	bool SwitchSource(const std::string& name);
};

// ── Particle Component ───────────────────────────────────────────────────────
// 持有对 VansParticleRuntime 的所有权以及对 VansParticleRenderNode 的非拥有引用。
// 通过此组件可以控制粒子系统的播放、暂停、停止，以及运行时切换粒子资产。
class VansScriptParticleComponent : public VansScriptComponent
{
public:
	VansScriptParticleComponent() { m_ComponentName = "Particle"; }

	// ── 资产引用 ─────────────────────────────────────────────────────────
	// .particle 文件相对路径（供序列化 / Inspector 显示）
	std::string m_ParticleAssetPath;

	// 运行时加载后的资产（拥有所有权）
	std::unique_ptr<VansGraphics::VansParticleAsset> m_ParticleAsset;

	// 运行时实例（双缓冲粒子状态，拥有所有权）
	std::unique_ptr<VansGraphics::VansParticleRuntime> m_Runtime;

	// 对应的渲染节点（非拥有指针，生命周期由 VansScene 管理）
	VansGraphics::VansParticleRenderNode* m_RenderNode = nullptr;

protected:
	void OnEnable()  override { Play(); }
	void OnDisable() override { Pause(); }

public:
	// ── 运行时状态 ───────────────────────────────────────────────────────
	bool  m_PlayOnAwake = true;
	bool  m_IsPlaying   = false;
	float m_PlayTime    = 0.f;
	bool      m_HasWorldPositionOverride = false;
	glm::vec3 m_WorldPositionOverride    = glm::vec3(0.f);

	// ── 播放控制接口 ─────────────────────────────────────────────────────
	void Play();
	void Stop();
	void Pause();
	void Restart();
	void SetWorldPosition(float x, float y, float z);
	void ClearWorldPositionOverride();

	// 加载并切换粒子资产（仅限 .particle 文件路径），返回 true 表示成功
	bool LoadAsset(const std::string& path);

	// 每帧由 VansScriptContext 调用（主线程）
	void OnUpdate(float deltaTime);
};

// ── Python Script Component ─────────────────────────────────────────────────
// Holds a reference to a Python script instance bound to this object.
enum class VansPythonScriptState : uint8_t
{
	Unloaded,
	Loading,
	Active,
	Disabled,
	Faulted,
	Destroyed
};

class VanPyScriptComponent : public VansScriptComponent
{
public:
	VanPyScriptComponent() { m_Enabled = false; }  // Python 脚本默认 disabled
	~VanPyScriptComponent() override;

	// Path to the .py file (relative to project root), e.g. "Scripts/my_rotator.py"
	std::string m_ScriptPath;
	// The class name inside the script, e.g. "MyRotator"
	std::string m_ScriptClassName;
	// Derived module name (computed from m_ScriptPath during Instantiate)
	std::string m_ScriptModuleName;

	// Python objects use empty handles until the interpreter is running. This
	// avoids creating py::none() during static engine-object construction.
	py::object m_PyInstance;
	py::object m_OnEnableCallback;
	py::object m_OnDisableCallback;
	py::object m_UpdateCallback;
	py::object m_CollisionEnterCallback;
	py::object m_CollisionExitCallback;
	py::object m_TriggerEnterCallback;
	py::object m_TriggerExitCallback;

	// Back-pointer to the owning VansScriptObject (non-owning, set on AddComponent).
	VansScriptObject* m_OwnerObject = nullptr;

	// Runtime state. m_IsValid is kept as the bridge-facing validity flag while
	// the enum is the authoritative lifecycle state.
	bool m_IsValid = false;
	bool m_EnableRequested = false;
	VansPythonScriptState m_State = VansPythonScriptState::Unloaded;

	// ── Lifecycle helpers (called from VansScriptContext) ─────────────
	bool Instantiate();   // resolve cached module/class, create instance, bind owner
	void Enable();        // → OnEnable  → cached Python on_enable()
	void CallUpdate();    // call Python update()
	void Disable();       // → OnDisable → cached Python on_disable()
	void Teardown();      // release py::object, reset state
	VansPythonScriptState GetState() const { return m_State; }

protected:
	void OnEnable()  override;  // calls Python on_enable()
	void OnDisable() override;  // calls Python on_disable()

	public:
	// ── Physics event callbacks (called from VansScriptContext) ───────
	void CallOnCollisionEnter(const PhysicsEventInfo& info);
	void CallOnCollisionExit(const PhysicsEventInfo& info);
	void CallOnTriggerEnter(const PhysicsEventInfo& info);
	void CallOnTriggerExit(const PhysicsEventInfo& info);

private:
	friend class VansScriptContext;
	bool BuildPythonInstance(const py::object& scriptClass, py::object& instance, std::string& error) const;
	void CommitPythonInstance(py::object instance);
	void CacheCallbacks();
	void ResetPythonObjects();
	void EnterFaultedState(const char* phase, const std::string& error);
};
