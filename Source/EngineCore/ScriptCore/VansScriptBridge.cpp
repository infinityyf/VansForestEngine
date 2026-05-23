#include "VansScriptContext.h"
#include "../RenderCore/VansScene.h"
#include "../AudioCore/VansAudioNode.h"
#include "VansTransform.h"
#include "../RenderCore/VansCamera.h"
#include "../RenderCore/VansRenderNode.h"
#include "../RenderCore/BRDFData/VansLight.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AnimationCore/VansAnimationController.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../Util/VansInputManager.h"
#include "../RenderCore/VulkanCore/VansVideoTexture.h"
#include "../RuntimeUI/Public/VansUISystem.h"
#include "../RuntimeUI/Public/VansUIDocument.h"
#include "../RuntimeUI/Public/VansUIElementHandle.h"
#include "../RuntimeUI/Public/VansUIScreenManager.h"
#include "../../../../ForestExporter/VansEngineBridge.h"
#include "../../../../ForestExporter/VansInputBridge.h"
#include <unordered_map>
#include <memory>

// ═══════════════════════════════════════════════════════════════════════════
//  VansScriptBridge.cpp — Fills the VansEngineBridge function-pointer table
//  so the .pyd module can call engine code without any engine headers.
// ═══════════════════════════════════════════════════════════════════════════

static VansEngineBridge s_EngineBridge;

// Keeps loaded UI document shared_ptrs alive; keyed by raw pointer (used as opaque handle).
static std::unordered_map<void*, std::shared_ptr<VansRuntime::VansUIDocument>> s_UIDocRegistry;

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

static inline VansScriptAnimationComponent* AsAnimComp(void* p)
{
	return dynamic_cast<VansScriptAnimationComponent*>(static_cast<VansScriptComponent*>(p));
}

static inline VansScriptCharacterControllerComponent* AsCCTComp(void* p)
{
	return dynamic_cast<VansScriptCharacterControllerComponent*>(
		static_cast<VansScriptComponent*>(p));
}

static inline VansScriptDirectionalLightComponent* AsDirLightComp(void* p)
{
	return dynamic_cast<VansScriptDirectionalLightComponent*>(
		static_cast<VansScriptComponent*>(p));
}

static inline VansScriptPointLightComponent* AsPointLightComp(void* p)
{
	return dynamic_cast<VansScriptPointLightComponent*>(
		static_cast<VansScriptComponent*>(p));
}

static inline VansScriptSpotLightComponent* AsSpotLightComp(void* p)
{
	return dynamic_cast<VansScriptSpotLightComponent*>(
		static_cast<VansScriptComponent*>(p));
}

static inline VansScriptCameraComponent* AsCameraComp(void* p)
{
	return dynamic_cast<VansScriptCameraComponent*>(
		static_cast<VansScriptComponent*>(p));
}

static inline VansScriptRectLightComponent* AsRectLightComp(void* p)
{
	return dynamic_cast<VansScriptRectLightComponent*>(
		static_cast<VansScriptComponent*>(p));
}

static inline VansScriptAudioComponent* AsAudioComp(void* p)
{
	return dynamic_cast<VansScriptAudioComponent*>(
		static_cast<VansScriptComponent*>(p));
}

static inline VansScriptVideoComponent* AsVideoComp(void* p)
{
	return dynamic_cast<VansScriptVideoComponent*>(
		static_cast<VansScriptComponent*>(p));
}

static inline VansScriptParticleComponent* AsParticleComp(void* p)
{
	return dynamic_cast<VansScriptParticleComponent*>(
		static_cast<VansScriptComponent*>(p));
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

	s_EngineBridge.sceneFindObject = [](const char* name) -> void*
	{
		if (!name) return nullptr;
		auto* ctx = VansScriptContext::GetInstance();
		if (!ctx || !ctx->GetScene()) return nullptr;
		return ctx->GetScene()->FindObjectByName(std::string(name));
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

	s_EngineBridge.objectGetAnimComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptAnimationComponent>();
	};

	// ── AnimationComponent ───────────────────────────────────────────────
	s_EngineBridge.animPlay = [](void* comp)
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode) ac->m_AnimNode->Play();
	};

	s_EngineBridge.animPlayState = [](void* comp, const char* stateName)
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode) ac->m_AnimNode->Play(stateName);
	};

	s_EngineBridge.animPause = [](void* comp)
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode) ac->m_AnimNode->Pause();
	};

	s_EngineBridge.animResume = [](void* comp)
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode) ac->m_AnimNode->Resume();
	};

	s_EngineBridge.animStop = [](void* comp)
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode) ac->m_AnimNode->Stop();
	};

	s_EngineBridge.animSetFloat = [](void* comp, const char* name, float val)
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode && ac->m_AnimNode->GetController())
			ac->m_AnimNode->GetController()->SetFloat(name, val);
	};

	s_EngineBridge.animSetBool = [](void* comp, const char* name, bool val)
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode && ac->m_AnimNode->GetController())
			ac->m_AnimNode->GetController()->SetBool(name, val);
	};

	s_EngineBridge.animSetInt = [](void* comp, const char* name, int val)
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode && ac->m_AnimNode->GetController())
			ac->m_AnimNode->GetController()->SetInt(name, val);
	};

	s_EngineBridge.animSetTrigger = [](void* comp, const char* name)
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode && ac->m_AnimNode->GetController())
			ac->m_AnimNode->GetController()->SetTrigger(name);
	};

	s_EngineBridge.animGetFloat = [](void* comp, const char* name) -> float
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode && ac->m_AnimNode->GetController())
			return ac->m_AnimNode->GetController()->GetFloat(name);
		return 0.0f;
	};

	s_EngineBridge.animGetBool = [](void* comp, const char* name) -> bool
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode && ac->m_AnimNode->GetController())
			return ac->m_AnimNode->GetController()->GetBool(name);
		return false;
	};

	s_EngineBridge.animGetInt = [](void* comp, const char* name) -> int
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode && ac->m_AnimNode->GetController())
			return ac->m_AnimNode->GetController()->GetInt(name);
		return 0;
	};

	s_EngineBridge.animGetCurrentState = [](void* comp, char* buf, int bufSize)
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode && bufSize > 0)
		{
			std::string state = ac->m_AnimNode->GetCurrentStateName();
			strncpy_s(buf, bufSize, state.c_str(), _TRUNCATE);
		}
	};

	s_EngineBridge.animGetNormalizedTime = [](void* comp) -> float
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode) return ac->m_AnimNode->GetNormalizedTime();
		return 0.0f;
	};

	s_EngineBridge.animGetSpeed = [](void* comp) -> float
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode) return ac->m_AnimNode->GetSpeed();
		return 1.0f;
	};

	s_EngineBridge.animSetSpeed = [](void* comp, float speed)
	{
		auto* ac = AsAnimComp(comp);
		if (ac && ac->m_AnimNode && ac->m_AnimNode->GetController())
			ac->m_AnimNode->GetController()->SetSpeed(speed);
	};

	// ── CharacterController Component ────────────────────────────────────
	s_EngineBridge.objectGetCCTComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptCharacterControllerComponent>();
	};

	s_EngineBridge.cctQueueMove = [](void* comp, float dx, float dy, float dz, float dt)
	{
		auto* cc = AsCCTComp(comp);
		if (cc && cc->m_ControllerNode)
			cc->m_ControllerNode->QueueMove(glm::vec3(dx, dy, dz), dt);
	};

	s_EngineBridge.cctIsGrounded = [](void* comp) -> bool
	{
		auto* cc = AsCCTComp(comp);
		if (cc && cc->m_ControllerNode)
			return cc->m_ControllerNode->IsGrounded();
		return false;
	};

	s_EngineBridge.cctGetPosition = [](void* comp, float& x, float& y, float& z)
	{
		auto* cc = AsCCTComp(comp);
		if (cc && cc->m_ControllerNode)
		{
			glm::vec3 pos = cc->m_ControllerNode->GetPosition();
			x = pos.x; y = pos.y; z = pos.z;
		}
	};

	s_EngineBridge.cctSetPosition = [](void* comp, float x, float y, float z)
	{
		auto* cc = AsCCTComp(comp);
		if (cc && cc->m_ControllerNode)
			cc->m_ControllerNode->SetPosition(glm::vec3(x, y, z));
	};

	s_EngineBridge.cctIsEnabled = [](void* comp) -> bool
	{
		auto* cc = AsCCTComp(comp);
		if (cc && cc->m_ControllerNode)
			return cc->m_ControllerNode->IsEnabled();
		return false;
	};

	// ── Directional Light Component ───────────────────────────────────────
	s_EngineBridge.objectGetDirLightComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptDirectionalLightComponent>();
	};

	s_EngineBridge.dirLightGetColor = [](void* comp, float& r, float& g, float& b)
	{
		auto* c = AsDirLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetDirectionLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		r = lights[c->m_LightIndex].m_Color.r;
		g = lights[c->m_LightIndex].m_Color.g;
		b = lights[c->m_LightIndex].m_Color.b;
	};

	s_EngineBridge.dirLightSetColor = [](void* comp, float r, float g, float b)
	{
		auto* c = AsDirLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetDirectionLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_Color = glm::vec3(r, g, b);
	};

	s_EngineBridge.dirLightGetIntensity = [](void* comp) -> float
	{
		auto* c = AsDirLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return 0.0f;
		auto& lights = c->m_LightManager->GetDirectionLights();
		if (c->m_LightIndex >= (int)lights.size()) return 0.0f;
		return lights[c->m_LightIndex].m_Intensity;
	};

	s_EngineBridge.dirLightSetIntensity = [](void* comp, float intensity)
	{
		auto* c = AsDirLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetDirectionLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_Intensity = intensity;
	};

	s_EngineBridge.dirLightGetDirection = [](void* comp, float& x, float& y, float& z)
	{
		auto* c = AsDirLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetDirectionLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		x = lights[c->m_LightIndex].m_Direction.x;
		y = lights[c->m_LightIndex].m_Direction.y;
		z = lights[c->m_LightIndex].m_Direction.z;
	};

	// ── Point Light Component ─────────────────────────────────────────────
	s_EngineBridge.objectGetPointLightComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptPointLightComponent>();
	};

	s_EngineBridge.pointLightGetColor = [](void* comp, float& r, float& g, float& b)
	{
		auto* c = AsPointLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetPointLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		r = lights[c->m_LightIndex].m_Color.r;
		g = lights[c->m_LightIndex].m_Color.g;
		b = lights[c->m_LightIndex].m_Color.b;
	};

	s_EngineBridge.pointLightSetColor = [](void* comp, float r, float g, float b)
	{
		auto* c = AsPointLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetPointLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_Color = glm::vec3(r, g, b);
	};

	s_EngineBridge.pointLightGetIntensity = [](void* comp) -> float
	{
		auto* c = AsPointLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return 0.0f;
		auto& lights = c->m_LightManager->GetPointLights();
		if (c->m_LightIndex >= (int)lights.size()) return 0.0f;
		return lights[c->m_LightIndex].m_Intensity;
	};

	s_EngineBridge.pointLightSetIntensity = [](void* comp, float intensity)
	{
		auto* c = AsPointLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetPointLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_Intensity = intensity;
	};

	s_EngineBridge.pointLightGetRadius = [](void* comp) -> float
	{
		auto* c = AsPointLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return 0.0f;
		auto& lights = c->m_LightManager->GetPointLights();
		if (c->m_LightIndex >= (int)lights.size()) return 0.0f;
		return lights[c->m_LightIndex].m_Radius;
	};

	s_EngineBridge.pointLightSetRadius = [](void* comp, float radius)
	{
		auto* c = AsPointLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetPointLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_Radius = radius;
	};

	// ── Spot Light Component ──────────────────────────────────────────────
	s_EngineBridge.objectGetSpotLightComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptSpotLightComponent>();
	};

	s_EngineBridge.spotLightGetColor = [](void* comp, float& r, float& g, float& b)
	{
		auto* c = AsSpotLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetSpotLight();
		if (c->m_LightIndex >= (int)lights.size()) return;
		r = lights[c->m_LightIndex].m_Color.r;
		g = lights[c->m_LightIndex].m_Color.g;
		b = lights[c->m_LightIndex].m_Color.b;
	};

	s_EngineBridge.spotLightSetColor = [](void* comp, float r, float g, float b)
	{
		auto* c = AsSpotLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetSpotLight();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_Color = glm::vec3(r, g, b);
	};

	s_EngineBridge.spotLightGetIntensity = [](void* comp) -> float
	{
		auto* c = AsSpotLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return 0.0f;
		auto& lights = c->m_LightManager->GetSpotLight();
		if (c->m_LightIndex >= (int)lights.size()) return 0.0f;
		return lights[c->m_LightIndex].m_Intensity;
	};

	s_EngineBridge.spotLightSetIntensity = [](void* comp, float intensity)
	{
		auto* c = AsSpotLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetSpotLight();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_Intensity = intensity;
	};

	s_EngineBridge.spotLightGetRadius = [](void* comp) -> float
	{
		auto* c = AsSpotLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return 0.0f;
		auto& lights = c->m_LightManager->GetSpotLight();
		if (c->m_LightIndex >= (int)lights.size()) return 0.0f;
		return lights[c->m_LightIndex].m_Radius;
	};

	s_EngineBridge.spotLightSetRadius = [](void* comp, float radius)
	{
		auto* c = AsSpotLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetSpotLight();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_Radius = radius;
	};

	s_EngineBridge.spotLightGetInnerCutoff = [](void* comp) -> float
	{
		auto* c = AsSpotLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return 0.0f;
		auto& lights = c->m_LightManager->GetSpotLight();
		if (c->m_LightIndex >= (int)lights.size()) return 0.0f;
		return glm::degrees(lights[c->m_LightIndex].m_InnerCutOff);
	};

	s_EngineBridge.spotLightSetInnerCutoff = [](void* comp, float deg)
	{
		auto* c = AsSpotLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetSpotLight();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_InnerCutOff = glm::radians(deg);
	};

	s_EngineBridge.spotLightGetOuterCutoff = [](void* comp) -> float
	{
		auto* c = AsSpotLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return 0.0f;
		auto& lights = c->m_LightManager->GetSpotLight();
		if (c->m_LightIndex >= (int)lights.size()) return 0.0f;
		return glm::degrees(lights[c->m_LightIndex].m_OuterCutOff);
	};

	s_EngineBridge.spotLightSetOuterCutoff = [](void* comp, float deg)
	{
		auto* c = AsSpotLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetSpotLight();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_OuterCutOff = glm::radians(deg);
	};

	s_EngineBridge.spotLightGetDirection = [](void* comp, float& x, float& y, float& z)
	{
		auto* c = AsSpotLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetSpotLight();
		if (c->m_LightIndex >= (int)lights.size()) return;
		x = lights[c->m_LightIndex].m_Direction.x;
		y = lights[c->m_LightIndex].m_Direction.y;
		z = lights[c->m_LightIndex].m_Direction.z;
	};

	// ── Camera Component ────────────────────────────────────────────────────
	s_EngineBridge.objectGetCameraComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptCameraComponent>();
	};

	s_EngineBridge.cameraGetFov = [](void* comp) -> float
	{
		auto* c = AsCameraComp(comp);
		return (c && c->m_Camera) ? c->m_Camera->GetFov() : 60.0f;
	};

	s_EngineBridge.cameraSetFov = [](void* comp, float fov)
	{
		auto* c = AsCameraComp(comp);
		if (c && c->m_Camera) c->m_Camera->SetFov(fov);
	};

	s_EngineBridge.cameraGetNearClip = [](void* comp) -> float
	{
		auto* c = AsCameraComp(comp);
		return (c && c->m_Camera) ? c->m_Camera->GetNearClip() : 0.1f;
	};

	s_EngineBridge.cameraSetNearClip = [](void* comp, float v)
	{
		auto* c = AsCameraComp(comp);
		if (c && c->m_Camera) c->m_Camera->SetNearClip(v);
	};

	s_EngineBridge.cameraGetFarClip = [](void* comp) -> float
	{
		auto* c = AsCameraComp(comp);
		return (c && c->m_Camera) ? c->m_Camera->GetFarClip() : 1000.0f;
	};

	s_EngineBridge.cameraSetFarClip = [](void* comp, float v)
	{
		auto* c = AsCameraComp(comp);
		if (c && c->m_Camera) c->m_Camera->SetFarClip(v);
	};

	// ── Rect Light Component ─────────────────────────────────────────
	s_EngineBridge.objectGetRectLightComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptRectLightComponent>();
	};

	s_EngineBridge.rectLightGetColor = [](void* comp, float& r, float& g, float& b)
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		r = lights[c->m_LightIndex].m_Color.r;
		g = lights[c->m_LightIndex].m_Color.g;
		b = lights[c->m_LightIndex].m_Color.b;
	};

	s_EngineBridge.rectLightSetColor = [](void* comp, float r, float g, float b)
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_Color = glm::vec3(r, g, b);
	};

	s_EngineBridge.rectLightGetIntensity = [](void* comp) -> float
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return 0.0f;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return 0.0f;
		return lights[c->m_LightIndex].m_Intensity;
	};

	s_EngineBridge.rectLightSetIntensity = [](void* comp, float v)
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_Intensity = v;
	};

	// 宽度 / 高度以完整边长暴露给 Python，内部存储剧半边长
	s_EngineBridge.rectLightGetWidth = [](void* comp) -> float
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return 0.0f;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return 0.0f;
		return lights[c->m_LightIndex].m_HalfWidth * 2.0f;
	};

	s_EngineBridge.rectLightSetWidth = [](void* comp, float w)
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_HalfWidth = w * 0.5f;
	};

	s_EngineBridge.rectLightGetHeight = [](void* comp) -> float
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return 0.0f;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return 0.0f;
		return lights[c->m_LightIndex].m_HalfHeight * 2.0f;
	};

	s_EngineBridge.rectLightSetHeight = [](void* comp, float h)
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_HalfHeight = h * 0.5f;
	};

	s_EngineBridge.rectLightGetRange = [](void* comp) -> float
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return 0.0f;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return 0.0f;
		return lights[c->m_LightIndex].m_Range;
	};

	s_EngineBridge.rectLightSetRange = [](void* comp, float r)
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_Range = r;
	};

	s_EngineBridge.rectLightGetTwoSided = [](void* comp) -> bool
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return false;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return false;
		return lights[c->m_LightIndex].m_TwoSided != 0.0f;
	};

	s_EngineBridge.rectLightSetTwoSided = [](void* comp, bool v)
	{
		auto* c = AsRectLightComp(comp);
		if (!c || !c->m_LightManager || c->m_LightIndex < 0) return;
		auto& lights = c->m_LightManager->GetRectLights();
		if (c->m_LightIndex >= (int)lights.size()) return;
		lights[c->m_LightIndex].m_TwoSided = v ? 1.0f : 0.0f;
	};

	// ── Runtime UI System ─────────────────────────────────────────────────

	s_EngineBridge.uiLoadDocument = [](const char* xamlPath) -> void*
	{
		auto doc = VansRuntime::VansUISystem::Get().LoadDocument(xamlPath);
		if (!doc) return nullptr;
		void* rawPtr = doc.get();
		s_UIDocRegistry[rawPtr] = std::move(doc);
		return rawPtr;
	};

	s_EngineBridge.uiUnloadDocument = [](void* doc)
	{
		auto it = s_UIDocRegistry.find(doc);
		if (it != s_UIDocRegistry.end())
		{
			VansRuntime::VansUISystem::Get().UnloadDocument(it->second);
			s_UIDocRegistry.erase(it);
		}
	};

	s_EngineBridge.uiDocumentShow = [](void* doc)
	{
		auto it = s_UIDocRegistry.find(doc);
		if (it != s_UIDocRegistry.end()) it->second->Show();
	};

	s_EngineBridge.uiDocumentHide = [](void* doc)
	{
		auto it = s_UIDocRegistry.find(doc);
		if (it != s_UIDocRegistry.end()) it->second->Hide();
	};

	s_EngineBridge.uiDocumentIsVisible = [](void* doc) -> bool
	{
		auto it = s_UIDocRegistry.find(doc);
		if (it != s_UIDocRegistry.end()) return it->second->IsVisible();
		return false;
	};

	s_EngineBridge.uiDocumentGetPath = [](void* doc, char* buf, int bufSize)
	{
		auto it = s_UIDocRegistry.find(doc);
		if (it != s_UIDocRegistry.end() && bufSize > 0)
			strncpy_s(buf, bufSize, it->second->GetSourcePath().c_str(), _TRUNCATE);
	};

	s_EngineBridge.uiSetElementText = [](void* doc, const char* elemName, const char* text)
	{
		auto it = s_UIDocRegistry.find(doc);
		if (it == s_UIDocRegistry.end()) return;
		auto elem = it->second->FindElement(elemName);
		if (elem.IsValid()) elem.SetText(text);
	};

	s_EngineBridge.uiGetElementText = [](void* doc, const char* elemName, char* buf, int bufSize)
	{
		auto it = s_UIDocRegistry.find(doc);
		if (it == s_UIDocRegistry.end()) return;
		auto elem = it->second->FindElement(elemName);
		if (elem.IsValid() && bufSize > 0)
			strncpy_s(buf, bufSize, elem.GetText().c_str(), _TRUNCATE);
	};

	s_EngineBridge.uiSetElementVisible = [](void* doc, const char* elemName, bool visible)
	{
		auto it = s_UIDocRegistry.find(doc);
		if (it == s_UIDocRegistry.end()) return;
		auto elem = it->second->FindElement(elemName);
		if (elem.IsValid()) elem.SetVisible(visible);
	};

	s_EngineBridge.uiGetElementVisible = [](void* doc, const char* elemName) -> bool
	{
		auto it = s_UIDocRegistry.find(doc);
		if (it == s_UIDocRegistry.end()) return false;
		auto elem = it->second->FindElement(elemName);
		return elem.IsValid() ? elem.IsVisible() : false;
	};

	s_EngineBridge.uiBindElementClick = [](void* doc,
	                                       const char* elemName,
	                                       void (*cb)(void*),
	                                       void* userData)
	{
		auto it = s_UIDocRegistry.find(doc);
		if (it == s_UIDocRegistry.end()) return;
		auto elem = it->second->FindElement(elemName);
		if (elem.IsValid())
			elem.BindClick([cb, userData]() { if (cb) cb(userData); });
	};

	// ── ScreenManager shortcuts ───────────────────────────────────────────

	s_EngineBridge.uiScreenPushScreen = [](const char* xamlPath)
	{
		VansRuntime::VansUISystem::Get().GetScreenManager().PushScreen(xamlPath);
	};

	s_EngineBridge.uiScreenPopScreen = []()
	{
		VansRuntime::VansUISystem::Get().GetScreenManager().PopScreen();
	};

	s_EngineBridge.uiScreenSetHUD = [](const char* xamlPath)
	{
		VansRuntime::VansUISystem::Get().GetScreenManager().SetHUD(xamlPath);
	};

	s_EngineBridge.uiScreenShowHUD = []()
	{
		VansRuntime::VansUISystem::Get().GetScreenManager().ShowHUD();
	};

	s_EngineBridge.uiScreenHideHUD = []()
	{
		VansRuntime::VansUISystem::Get().GetScreenManager().HideHUD();
	};

	// ── Audio Component ───────────────────────────────────────────────────
	s_EngineBridge.objectGetAudioComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptAudioComponent>();
	};

	s_EngineBridge.audioPlay = [](void* comp)
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) c->m_AudioNode->Play();
	};

	s_EngineBridge.audioPause = [](void* comp)
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) c->m_AudioNode->Pause();
	};

	s_EngineBridge.audioStop = [](void* comp)
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) c->m_AudioNode->Stop();
	};

	s_EngineBridge.audioResume = [](void* comp)
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) c->m_AudioNode->Resume();
	};

	s_EngineBridge.audioGetVolume = [](void* comp) -> float
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) return c->m_AudioNode->GetVolume();
		return 1.0f;
	};

	s_EngineBridge.audioSetVolume = [](void* comp, float v)
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) c->m_AudioNode->SetVolume(v);
	};

	s_EngineBridge.audioGetPitch = [](void* comp) -> float
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) return c->m_AudioNode->GetPitch();
		return 1.0f;
	};

	s_EngineBridge.audioSetPitch = [](void* comp, float p)
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) c->m_AudioNode->SetPitch(p);
	};

	s_EngineBridge.audioGetLoop = [](void* comp) -> bool
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) return c->m_AudioNode->GetLoop();
		return false;
	};

	s_EngineBridge.audioSetLoop = [](void* comp, bool loop)
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) c->m_AudioNode->SetLoop(loop);
	};

	s_EngineBridge.audioIsPlaying = [](void* comp) -> bool
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) return c->m_AudioNode->IsPlaying();
		return false;
	};

	s_EngineBridge.audioIsPaused = [](void* comp) -> bool
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) return c->m_AudioNode->IsPaused();
		return false;
	};

	s_EngineBridge.audioSetPosition = [](void* comp, float x, float y, float z)
	{
		auto* c = AsAudioComp(comp);
		if (c && c->m_AudioNode) c->m_AudioNode->SetPosition(x, y, z);
	};

	s_EngineBridge.audioGetFilePath = [](void* comp, char* buf, int bufSize)
	{
		auto* c = AsAudioComp(comp);
		if (!c || !c->m_AudioNode || !buf || bufSize <= 0) return;
		const std::string& path = c->m_AudioNode->GetFilePath();
		strncpy_s(buf, bufSize, path.c_str(), static_cast<size_t>(bufSize - 1));
	};

	// ── Particle Component ─────────────────────────────────────────
	s_EngineBridge.objectGetParticleComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptParticleComponent>();
	};

	s_EngineBridge.particlePlay = [](void* comp)
	{
		auto* c = AsParticleComp(comp);
		if (c) c->Play();
	};

	s_EngineBridge.particleStop = [](void* comp)
	{
		auto* c = AsParticleComp(comp);
		if (c) c->Stop();
	};

	s_EngineBridge.particlePause = [](void* comp)
	{
		auto* c = AsParticleComp(comp);
		if (c) c->Pause();
	};

	s_EngineBridge.particleRestart = [](void* comp)
	{
		auto* c = AsParticleComp(comp);
		if (c) c->Restart();
	};

	s_EngineBridge.particleSetWorldPosition = [](void* comp, float x, float y, float z)
	{
		auto* c = AsParticleComp(comp);
		if (c) c->SetWorldPosition(x, y, z);
	};

	s_EngineBridge.particleClearWorldPositionOverride = [](void* comp)
	{
		auto* c = AsParticleComp(comp);
		if (c) c->ClearWorldPositionOverride();
	};

	s_EngineBridge.particleIsPlaying = [](void* comp) -> bool
	{
		auto* c = AsParticleComp(comp);
		return c ? c->m_IsPlaying : false;
	};

	s_EngineBridge.particleGetPlayTime = [](void* comp) -> float
	{
		auto* c = AsParticleComp(comp);
		return c ? c->m_PlayTime : 0.0f;
	};

	s_EngineBridge.particleGetAssetPath = [](void* comp, char* buf, int bufSize)
	{
		auto* c = AsParticleComp(comp);
		if (!c || !buf || bufSize <= 0) return;
		strncpy_s(buf, bufSize, c->m_ParticleAssetPath.c_str(), static_cast<size_t>(bufSize - 1));
	};

	// ── Video Component ───────────────────────────────────────────
	s_EngineBridge.objectGetVideoComp = [](void* obj) -> void*
	{
		auto* o = AsScriptObject(obj);
		if (!o) return nullptr;
		return o->GetComponent<VansScriptVideoComponent>();
	};

	s_EngineBridge.videoPlay = [](void* comp)
	{
		auto* c = AsVideoComp(comp);
		if (c && c->m_VideoTex) c->m_VideoTex->Play();
	};

	s_EngineBridge.videoPause = [](void* comp)
	{
		auto* c = AsVideoComp(comp);
		if (c && c->m_VideoTex) c->m_VideoTex->Pause();
	};

	s_EngineBridge.videoStop = [](void* comp)
	{
		auto* c = AsVideoComp(comp);
		if (c && c->m_VideoTex) c->m_VideoTex->Stop();
	};

	s_EngineBridge.videoIsPlaying = [](void* comp) -> bool
	{
		auto* c = AsVideoComp(comp);
		return (c && c->m_VideoTex) ? c->m_VideoTex->IsPlaying() : false;
	};

	s_EngineBridge.videoIsReady = [](void* comp) -> bool
	{
		auto* c = AsVideoComp(comp);
		return (c && c->m_VideoTex) ? c->m_VideoTex->IsReady() : false;
	};

	s_EngineBridge.videoGetName = [](void* comp, char* buf, int bufSize)
	{
		auto* c = AsVideoComp(comp);
		if (!c || !buf || bufSize <= 0) return;
		strncpy_s(buf, bufSize, c->m_VideoName.c_str(), static_cast<size_t>(bufSize - 1));
	};

	// ── Audio SwitchSource ───────────────────────────────────────────
	s_EngineBridge.audioSwitchSource = [](void* comp, const char* name) -> bool
	{
		auto* c = AsAudioComp(comp);
		if (!c || !name) return false;
		return c->SwitchSource(std::string(name));
	};

	// ── Video SwitchSource ───────────────────────────────────────────
	s_EngineBridge.videoSwitchSource = [](void* comp, const char* name) -> bool
	{
		auto* c = AsVideoComp(comp);
		if (!c || !name) return false;
		return c->SwitchSource(std::string(name));
	};

	// ── Physics Scene Query ───────────────────────────────────────────────
	s_EngineBridge.physicsRaycast = [](float ox, float oy, float oz,
	                                    float dx, float dy, float dz,
	                                    float maxDist) -> RaycastHitInfo
	{
		RaycastHitInfo result;
		auto& phys = VansEngine::VansPhysicsSystem::GetInstance();
		PxScene* scene = phys.GetScene();
		if (!scene) return result;

		std::lock_guard<std::mutex> lock(phys.GetSimulationMutex());

		const PxVec3 origin(ox, oy, oz);
		const PxVec3 unitDir = PxVec3(dx, dy, dz).getNormalized();
		PxRaycastBuffer buf;
		if (!scene->raycast(origin, unitDir, maxDist, buf) || !buf.hasBlock)
			return result;

		const PxRaycastHit& hit = buf.block;
		result.hasHit    = true;
		result.distance  = hit.distance;
		result.position  = PyVec3(hit.position.x, hit.position.y, hit.position.z);
		result.normal    = PyVec3(hit.normal.x,   hit.normal.y,   hit.normal.z);
		if (hit.actor && hit.actor->userData)
		{
			auto* node = static_cast<VansEngine::VansPhysicsNode*>(hit.actor->userData);
			strncpy_s(result.hitName, sizeof(result.hitName),
			          node->GetName().c_str(), _TRUNCATE);
			result.transformID = node->GetTransformID();
		}
		return result;
	};

	s_EngineBridge.physicsRaycastAll = [](float ox, float oy, float oz,
	                                       float dx, float dy, float dz,
	                                       float maxDist,
	                                       RaycastHitInfo* outHits, int capacity) -> int
	{
		if (!outHits || capacity <= 0) return 0;
		auto& phys = VansEngine::VansPhysicsSystem::GetInstance();
		PxScene* scene = phys.GetScene();
		if (!scene) return 0;

		std::lock_guard<std::mutex> lock(phys.GetSimulationMutex());

		const PxVec3 origin(ox, oy, oz);
		const PxVec3 unitDir = PxVec3(dx, dy, dz).getNormalized();
		std::vector<PxRaycastHit> hitBuffer(static_cast<size_t>(capacity));
		PxRaycastBuffer buf(hitBuffer.data(), static_cast<PxU32>(capacity));
		scene->raycast(origin, unitDir, maxDist, buf);

		const PxU32 count = buf.nbTouches;
		const PxU32 write = (count < static_cast<PxU32>(capacity))
		                    ? count : static_cast<PxU32>(capacity);
		for (PxU32 i = 0; i < write; ++i)
		{
			const PxRaycastHit& hit = buf.touches[i];
			RaycastHitInfo& out = outHits[i];
			out.hasHit   = true;
			out.distance = hit.distance;
			out.position = PyVec3(hit.position.x, hit.position.y, hit.position.z);
			out.normal   = PyVec3(hit.normal.x,   hit.normal.y,   hit.normal.z);
			if (hit.actor && hit.actor->userData)
			{
				auto* node = static_cast<VansEngine::VansPhysicsNode*>(hit.actor->userData);
				strncpy_s(out.hitName, sizeof(out.hitName),
				          node->GetName().c_str(), _TRUNCATE);
				out.transformID = node->GetTransformID();
			}
		}
		return static_cast<int>(write);
	};

	s_EngineBridge.physicsOverlapSphere = [](float cx, float cy, float cz, float radius,
	                                          OverlapHitInfo* outHits, int capacity) -> int
	{
		if (!outHits || capacity <= 0) return 0;
		auto& phys = VansEngine::VansPhysicsSystem::GetInstance();
		PxScene* scene = phys.GetScene();
		if (!scene) return 0;

		std::lock_guard<std::mutex> lock(phys.GetSimulationMutex());

		const PxSphereGeometry sphere(radius);
		const PxTransform pose(PxVec3(cx, cy, cz));
		std::vector<PxOverlapHit> hitBuffer(static_cast<size_t>(capacity));
		PxOverlapBuffer buf(hitBuffer.data(), static_cast<PxU32>(capacity));
		scene->overlap(sphere, pose, buf);

		const PxU32 count = buf.nbTouches;
		const PxU32 write = (count < static_cast<PxU32>(capacity))
		                    ? count : static_cast<PxU32>(capacity);
		for (PxU32 i = 0; i < write; ++i)
		{
			const PxOverlapHit& hit = buf.touches[i];
			OverlapHitInfo& out = outHits[i];
			if (hit.actor && hit.actor->userData)
			{
				auto* node = static_cast<VansEngine::VansPhysicsNode*>(hit.actor->userData);
				strncpy_s(out.hitName, sizeof(out.hitName),
				          node->GetName().c_str(), _TRUNCATE);
				out.transformID = node->GetTransformID();
			}
		}
		return static_cast<int>(write);
	};

	s_EngineBridge.physicsAddForce = [](uint32_t transformID,
	                                    float fx, float fy, float fz,
	                                    bool impulse) -> bool
	{
		auto* ctx = VansScriptContext::GetInstance();
		if (!ctx || !ctx->GetScene() || transformID == 0) return false;

		VansEngine::VansPhysicsNode* targetNode = nullptr;
		for (auto* physicsNode : ctx->GetScene()->m_PhysicsNodes)
		{
			if (!physicsNode || physicsNode->GetTransformID() != transformID) continue;
			targetNode = physicsNode;
			break;
		}

		if (!targetNode || !targetNode->IsEnabled()) return false;

		auto& phys = VansEngine::VansPhysicsSystem::GetInstance();
		PxScene* scene = phys.GetScene();
		std::lock_guard<std::mutex> lock(phys.GetSimulationMutex());
		if (scene)
		{
			PxSceneWriteLock scopedWriteLock(*scene);
			targetNode->AddForce(glm::vec3(fx, fy, fz),
			                     impulse ? PxForceMode::eIMPULSE : PxForceMode::eFORCE);
		}
		else
		{
			targetNode->AddForce(glm::vec3(fx, fy, fz),
			                     impulse ? PxForceMode::eIMPULSE : PxForceMode::eFORCE);
		}

		return true;
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
