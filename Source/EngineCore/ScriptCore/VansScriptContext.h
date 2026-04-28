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
#include <pybind11/embed.h>
#include "../PhysicsCore/VansPhysicsEvents.h"
#include "../../../../ForestExporter/VansPhysicsEventInfo.h"
namespace py = pybind11;

// Forward declarations for Component sub-classes
namespace VansGraphics { class VansRenderNode; class VansScene; class VansAnimationNode; class VansLightManager; class VansCamera; }
namespace VansEngine  { class VansPhysicsNode; class VansClothNode; class VansPhysicsVehicle; class VansCharacterControllerNode; }

class VansScriptContext
{
private:
	// ---- Hot-reload infrastructure ----
	std::string m_ScriptDir;  // path to EngineExported/

	// Track last-write-time for each imported .py file (keyed by script path)
	struct PyModuleInfo {
		py::module   module;
		std::string  moduleName;   // Python module name registered in sys.modules
		std::filesystem::file_time_type lastWriteTime;
		std::filesystem::path           filePath;     // absolute file path
	};
	std::unordered_map<std::string, PyModuleInfo> m_TrackedPyModules;  // key = script path

	// Counter for unique temp .pyd file names during copy-based reload
	int m_PydReloadCounter = 0;

	// Accumulator so we don't stat the filesystem every single frame
	float m_FileCheckAccumulator = 0.0f;
	static constexpr float FILE_CHECK_INTERVAL = 0.5f;  // seconds

	// Internal helpers
	void CheckAndReloadPyScripts();

	// Called after a .py module is hot-reloaded to re-instantiate script components
	void OnPyModuleReloaded(const std::string& scriptPath);

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

	// Register a Python module for hot-reload file-watching.
	// Called from VanPyScriptComponent::Instantiate() and internally.
	void TrackPyModule(const std::string& scriptPath, const std::string& moduleName, py::module mod, const std::filesystem::path& absPath);

	void VansScriptSetup();

	void VansScriptUpdate();

	// Set the active scene so the update loop can iterate objects
	void SetScene(VansGraphics::VansScene* scene) { m_Scene = scene; }

	// ── 场景切换时清空已跟踪的 Python 模块 ─────────────────────────
	// 释放 py::module 引用，清空 m_TrackedPyModules，防止跨场景累积。
	void ClearTrackedModules();

	// Explicit reload called from editor UI
	void ReloadAllPyScripts();

	// Reload the .pyd C++ extension module (copy-based hot-reload on Windows)
	void ReloadPydModule(const std::string& moduleName = "vanscomponent");

	// ── 项目 Python 虚拟环境管理 ─────────────────────────────────────
	// 为指定项目目录创建/更新 .venv 并安装 requirements.txt 中的依赖。
	// 在解释器启动后调用（VansScriptSetup 内部或项目打开后由编辑器调用）。
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

	virtual ~VansScriptComponent() = default;
};

class VansScriptObject
{
public:
	std::string m_ObjectName;

	// All components owned by this object (polymorphic pointers).
	// The object owns the Component memory, but NOT the Node pointed to by each component.
	std::vector<VansScriptComponent*> m_Components;

	// Transform ID shared across all components of this object.
	// Typically taken from the render component's RenderNode.
	uint32_t m_TransformID = 0;

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

	~VansScriptObject()
	{
		for (auto* comp : m_Components)
			delete comp;
		m_Components.clear();
	}
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
};

// ── Physics Component ───────────────────────────────────────────────────────
// Holds a non-owning pointer to a VansPhysicsNode managed by VansScene.
class VansScriptPhysicsComponent : public VansScriptComponent
{
public:
	VansEngine::VansPhysicsNode* m_PhysicsNode = nullptr;
};

// ── Cloth Component ─────────────────────────────────────────────────────────
// Holds a non-owning pointer to a VansClothNode managed by VansScene.
class VansScriptClothComponent : public VansScriptComponent
{
public:
	VansEngine::VansClothNode* m_ClothNode = nullptr;
};

// ── Vehicle Component ───────────────────────────────────────────────────────
// Holds a non-owning pointer to a VansPhysicsVehicle managed by VansScene.
class VansScriptVehicleComponent : public VansScriptComponent
{
public:
	VansEngine::VansPhysicsVehicle* m_Vehicle = nullptr;
};

// ── Animation Component ─────────────────────────────────────────────────────
// 持有对 VansScene 管理的 VansAnimationNode 的非拥有指针。
// 通过此组件可以在 Python 中控制 AnimationController 的状态。
class VansScriptAnimationComponent : public VansScriptComponent
{
public:
	// 非拥有指针，生命周期由 VansScene 管理
	VansGraphics::VansAnimationNode* m_AnimNode = nullptr;
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
};// ── Camera Component ────────────────────────────────────────────────────────────────────
// 持有对 VansCamera 的非拥有指针；VansCamera 由 VansScene 生命周期管理。
// Transform 的 position/rotation(pitch/yaw) 每帧由 VansCamera::SyncFromTransform 同步。
class VansScriptCameraComponent : public VansScriptComponent
{
public:
	VansScriptCameraComponent() { m_ComponentName = "camera"; }

	// 非拥有指针，生命周期由 VansScene::m_Camera 管理
	VansGraphics::VansCamera* m_Camera = nullptr;
};
// ── Python Script Component ─────────────────────────────────────────────────
// Holds a reference to a Python script instance bound to this object.
class VanPyScriptComponent : public VansScriptComponent
{
public:
	// Path to the .py file (relative to project root), e.g. "Scripts/my_rotator.py"
	std::string m_ScriptPath;
	// The class name inside the script, e.g. "MyRotator"
	std::string m_ScriptClassName;
	// Derived module name (computed from m_ScriptPath during Instantiate)
	std::string m_ScriptModuleName;

	// The live Python instance (py::object wrapping a vanspyscript subclass).
	// When no script is assigned this is py::none().
	py::object  m_PyInstance = py::none();

	// Back-pointer to the owning VansScriptObject (non-owning, set on AddComponent).
	VansScriptObject* m_OwnerObject = nullptr;

	// Runtime state
	bool m_IsEnabled  = false;
	bool m_IsValid    = false;   // true after successful instantiation

	// ── Lifecycle helpers (called from VansScriptContext) ─────────────
	void Instantiate();   // import module, create class instance, bind owner
	void Enable();        // call Python on_enable()
	void CallUpdate();    // call Python update()
	void Disable();       // call Python on_disable()
	void Teardown();      // release py::object, reset state

	// ── Physics event callbacks (called from VansScriptContext) ───────
	void CallOnCollisionEnter(const PhysicsEventInfo& info);
	void CallOnCollisionExit(const PhysicsEventInfo& info);
	void CallOnTriggerEnter(const PhysicsEventInfo& info);
	void CallOnTriggerExit(const PhysicsEventInfo& info);
};