#include "VansScriptContext.h"
#include "VansTransform.h"
#include "../RenderCore/VansRenderNode.h"
#include "../Util/VansInputManager.h"
#include "../../../../ForestExporter/VansEngineBridge.h"
#include "../../../../ForestExporter/VansInputBridge.h"

// ═══════════════════════════════════════════════════════════════════════════
//  VansScriptBridge.cpp — Fills the VansEngineBridge function-pointer table
//  so the .pyd module can call engine code without any engine headers.
// ═══════════════════════════════════════════════════════════════════════════

static VansEngineBridge s_EngineBridge;

// ---------------------------------------------------------------------------
//  Helper: cast opaque void* back to engine types
// ---------------------------------------------------------------------------
static inline VansGraphics::VansRenderNode* AsRenderNode(void* p)
{
	return static_cast<VansGraphics::VansRenderNode*>(p);
}

static inline VansScriptObject* AsScriptObject(void* p)
{
	return static_cast<VansScriptObject*>(p);
}

static inline VansScriptComponent* AsScriptComponent(void* p)
{
	return static_cast<VansScriptComponent*>(p);
}

// ---------------------------------------------------------------------------
//  Populate the bridge
// ---------------------------------------------------------------------------
void VansInitEngineBridge()
{
	// ── Transform ────────────────────────────────────────────────────────
	s_EngineBridge.transformGetPosition = [](uint32_t id, float& x, float& y, float& z)
	{
		auto& t = VansGraphics::VansTransformStore::GetTransform(id);
		x = t.m_Position.x; y = t.m_Position.y; z = t.m_Position.z;
	};

	s_EngineBridge.transformSetPosition = [](uint32_t id, float x, float y, float z)
	{
		auto& t = VansGraphics::VansTransformStore::GetTransform(id);
		t.m_Position = glm::vec3(x, y, z);
	};

	s_EngineBridge.transformGetRotation = [](uint32_t id, float& x, float& y, float& z)
	{
		auto& t = VansGraphics::VansTransformStore::GetTransform(id);
		x = t.m_Rotation.x; y = t.m_Rotation.y; z = t.m_Rotation.z;
	};

	s_EngineBridge.transformSetRotation = [](uint32_t id, float x, float y, float z)
	{
		auto& t = VansGraphics::VansTransformStore::GetTransform(id);
		t.m_Rotation = glm::vec3(x, y, z);
	};

	s_EngineBridge.transformGetScale = [](uint32_t id, float& x, float& y, float& z)
	{
		auto& t = VansGraphics::VansTransformStore::GetTransform(id);
		x = t.m_Scale.x; y = t.m_Scale.y; z = t.m_Scale.z;
	};

	s_EngineBridge.transformSetScale = [](uint32_t id, float x, float y, float z)
	{
		auto& t = VansGraphics::VansTransformStore::GetTransform(id);
		t.m_Scale = glm::vec3(x, y, z);
	};

	s_EngineBridge.transformTranslate = [](uint32_t id, float dx, float dy, float dz)
	{
		auto& t = VansGraphics::VansTransformStore::GetTransform(id);
		t.m_Position += glm::vec3(dx, dy, dz);
	};

	// ── RenderNode ───────────────────────────────────────────────────────
	s_EngineBridge.renderNodeGetName = [](void* node, char* buf, int bufSize)
	{
		auto* n = AsRenderNode(node);
		if (n && bufSize > 0)
		{
			strncpy_s(buf, bufSize, n->m_NodeName.c_str(), _TRUNCATE);
		}
	};

	s_EngineBridge.renderNodeSetName = [](void* node, const char* name)
	{
		auto* n = AsRenderNode(node);
		if (n) n->SetName(name);
	};

	s_EngineBridge.renderNodeGetPosition = [](void* node, float& x, float& y, float& z)
	{
		auto* n = AsRenderNode(node);
		if (n) { auto p = n->GetTransformPosition(); x = p.x; y = p.y; z = p.z; }
	};

	s_EngineBridge.renderNodeSetPosition = [](void* node, float x, float y, float z)
	{
		auto* n = AsRenderNode(node);
		if (n) n->SetTransformData(glm::vec3(x, y, z), n->GetTransformRotation(), n->GetTransformScale());
	};

	s_EngineBridge.renderNodeGetRotation = [](void* node, float& x, float& y, float& z)
	{
		auto* n = AsRenderNode(node);
		if (n) { auto r = n->GetTransformRotation(); x = r.x; y = r.y; z = r.z; }
	};

	s_EngineBridge.renderNodeSetRotation = [](void* node, float x, float y, float z)
	{
		auto* n = AsRenderNode(node);
		if (n) n->SetTransformData(n->GetTransformPosition(), glm::vec3(x, y, z), n->GetTransformScale());
	};

	s_EngineBridge.renderNodeGetScale = [](void* node, float& x, float& y, float& z)
	{
		auto* n = AsRenderNode(node);
		if (n) { auto s = n->GetTransformScale(); x = s.x; y = s.y; z = s.z; }
	};

	s_EngineBridge.renderNodeSetScale = [](void* node, float x, float y, float z)
	{
		auto* n = AsRenderNode(node);
		if (n) n->SetTransformData(n->GetTransformPosition(), n->GetTransformRotation(), glm::vec3(x, y, z));
	};

	s_EngineBridge.renderNodeSetTransform = [](void* node,
		float px, float py, float pz,
		float rx, float ry, float rz,
		float sx, float sy, float sz)
	{
		auto* n = AsRenderNode(node);
		if (n) n->SetTransformData(glm::vec3(px, py, pz), glm::vec3(rx, ry, rz), glm::vec3(sx, sy, sz));
	};

	s_EngineBridge.renderNodeGetTransformID = [](void* node) -> uint32_t
	{
		auto* n = AsRenderNode(node);
		return n ? n->m_TransformID : 0;
	};

	s_EngineBridge.renderNodeGetType = [](void* node) -> int
	{
		auto* n = AsRenderNode(node);
		return n ? static_cast<int>(n->GetNodeType()) : 0;
	};

	s_EngineBridge.renderNodeHasSkeleton = [](void* node) -> bool
	{
		auto* n = AsRenderNode(node);
		return n ? n->m_HasSkeletonBone : false;
	};

	s_EngineBridge.renderNodeIsAnimEnabled = [](void* node) -> bool
	{
		auto* n = AsRenderNode(node);
		return n ? n->m_AnimationEnabled : false;
	};

	// ── Object / Component helpers ───────────────────────────────────────
	s_EngineBridge.objectGetName = [](void* obj, char* buf, int bufSize)
	{
		auto* o = AsScriptObject(obj);
		if (o && bufSize > 0)
			strncpy_s(buf, bufSize, o->m_ObjectName.c_str(), _TRUNCATE);
	};

	s_EngineBridge.objectGetTransformID = [](void* obj) -> uint32_t
	{
		auto* o = AsScriptObject(obj);
		return o ? o->m_TransformID : 0;
	};

	s_EngineBridge.componentGetName = [](void* comp, char* buf, int bufSize)
	{
		auto* c = AsScriptComponent(comp);
		if (c && bufSize > 0)
			strncpy_s(buf, bufSize, c->m_ComponentName.c_str(), _TRUNCATE);
	};

	s_EngineBridge.componentGetTransformID = [](void* comp) -> uint32_t
	{
		auto* c = dynamic_cast<VansScriptTransform*>(AsScriptComponent(comp));
		return c ? static_cast<uint32_t>(c->m_TransformID) : 0;
	};

	s_EngineBridge.componentGetRenderNode = [](void* comp) -> void*
	{
		auto* c = dynamic_cast<VansScriptRenderComponent*>(AsScriptComponent(comp));
		return c ? static_cast<void*>(c->m_RenderNode) : nullptr;
	};

	// ── Object component query ───────────────────────────────────────────
	s_EngineBridge.objectGetComponentCount = [](void* obj) -> int
	{
		auto* o = AsScriptObject(obj);
		return o ? static_cast<int>(o->m_Components.size()) : 0;
	};

	s_EngineBridge.objectGetComponentByIndex = [](void* obj, int index) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (o && index >= 0 && index < static_cast<int>(o->m_Components.size()))
			return o->m_Components[index];
		return nullptr;
	};

	s_EngineBridge.objectGetRenderComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptRenderComponent>();
	};

	s_EngineBridge.objectGetTransformComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptTransform>();
	};
}

// ---------------------------------------------------------------------------
//  Get the bridge pointer (used by VansScriptContext to pass to the .pyd)
// ---------------------------------------------------------------------------
VansEngineBridge* VansGetEngineBridgePtr()
{
	return &s_EngineBridge;
}

// ===========================================================================
//  VansInputBridge — 将 VansInputManager 的接口暴露给 vaninput .pyd
// ===========================================================================

static VansInputBridge s_InputBridge;

void VansInitInputBridge()
{
	auto& input = Vans::VansInputManager::Get();

	// ── 键盘 ─────────────────────────────────────────────────────────────
	s_InputBridge.isKeyDown = [](int key) -> bool
	{
		return Vans::VansInputManager::Get().IsKeyDown(key);
	};

	s_InputBridge.isKeyPressed = [](int key) -> bool
	{
		return Vans::VansInputManager::Get().IsKeyPressed(key);
	};

	s_InputBridge.isKeyReleased = [](int key) -> bool
	{
		return Vans::VansInputManager::Get().IsKeyReleased(key);
	};

	// ── 鼠标按键 ──────────────────────────────────────────────────────────
	s_InputBridge.isMouseDown = [](int button) -> bool
	{
		return Vans::VansInputManager::Get().IsMouseButtonDown(
			static_cast<Vans::MouseButton>(button));
	};

	s_InputBridge.isMousePressed = [](int button) -> bool
	{
		return Vans::VansInputManager::Get().IsMouseButtonPressed(
			static_cast<Vans::MouseButton>(button));
	};

	s_InputBridge.isMouseReleased = [](int button) -> bool
	{
		return Vans::VansInputManager::Get().IsMouseButtonReleased(
			static_cast<Vans::MouseButton>(button));
	};

	// ── 鼠标位置 / 增量 / 滚轮 ──────────────────────────────────────────
	s_InputBridge.getMousePosition = [](double& x, double& y)
	{
		Vans::VansInputManager::Get().GetMousePosition(x, y);
	};

	s_InputBridge.getMouseDelta = [](double& dx, double& dy)
	{
		Vans::VansInputManager::Get().GetMouseDelta(dx, dy);
	};

	s_InputBridge.getScrollDelta = [](double& sx, double& sy)
	{
		Vans::VansInputManager::Get().GetScrollDelta(sx, sy);
	};

	// ── Action 系统 ───────────────────────────────────────────────────────
	s_InputBridge.registerAction = [](const char* name, int key)
	{
		Vans::VansInputManager::Get().RegisterAction(std::string(name), key);
	};

	s_InputBridge.unregisterAction = [](const char* name)
	{
		Vans::VansInputManager::Get().UnregisterAction(std::string(name));
	};

	s_InputBridge.isActionDown = [](const char* name) -> bool
	{
		return Vans::VansInputManager::Get().IsActionDown(std::string(name));
	};

	s_InputBridge.isActionPressed = [](const char* name) -> bool
	{
		return Vans::VansInputManager::Get().IsActionPressed(std::string(name));
	};

	s_InputBridge.isActionReleased = [](const char* name) -> bool
	{
		return Vans::VansInputManager::Get().IsActionReleased(std::string(name));
	};

	// ── Axis 系统 ─────────────────────────────────────────────────────────
	s_InputBridge.registerAxis = [](const char* name, int posKey, int negKey)
	{
		Vans::VansInputManager::Get().RegisterAxis(std::string(name), posKey, negKey);
	};

	s_InputBridge.unregisterAxis = [](const char* name)
	{
		Vans::VansInputManager::Get().UnregisterAxis(std::string(name));
	};

	s_InputBridge.getAxis = [](const char* name) -> float
	{
		return Vans::VansInputManager::Get().GetAxis(std::string(name));
	};
}

VansInputBridge* VansGetInputBridgePtr()
{
	return &s_InputBridge;
}
