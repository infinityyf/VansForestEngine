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
namespace VansGraphics { class VansRenderNode; class VansScene; }
namespace VansEngine  { class VansPhysicsNode; class VansClothNode; class VansPhysicsVehicle; }

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