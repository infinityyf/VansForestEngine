#include "VansScriptContext.h"
#include "VansTransform.h"
#include "../RenderCore/VansRenderNode.h"
#include "../../../ForestExporter/VansEngineBridge.h"

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
}

// ---------------------------------------------------------------------------
//  Get the bridge pointer (used by VansScriptContext to pass to the .pyd)
// ---------------------------------------------------------------------------
VansEngineBridge* VansGetEngineBridgePtr()
{
	return &s_EngineBridge;
}
