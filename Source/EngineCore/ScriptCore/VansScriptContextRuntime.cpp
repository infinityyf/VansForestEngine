#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "VansScriptContext.h"

#include "VansLuaUIBridge.h"
#include "VansLuaGameplayActionBridge.h"
#include "VansTransform.h"
#include "../AnimationCore/VansAnimationController.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AudioCore/VansAudioManager.h"
#include "../AudioCore/VansAudioNode.h"
#include "../AudioCore/Storage/VansAudioBusSnapshotAssetStorage.h"
#include "../AudioCore/Storage/VansAudioDuckingRulesAssetStorage.h"
#include "../Configration/VansConfigration.h"
#include "../EventCore/VansEventBus.h"
#include "../ParticleCore/Storage/VansParticleAssetStorage.h"
#include "../ParticleCore/VansParticleManager.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../PhysicsCore/VansClothNode.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansPhysicsVehicle.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../RenderCore/VansCamera.h"
#include "../RenderCore/VansCameraControlArbiter.h"
#include "../RenderCore/BRDFData/VansLight.h"
#include "../RenderCore/VansRenderNode.h"
#include "../RenderCore/VansScene.h"
#include "../RenderCore/VansVideoManager.h"
#include "../RenderCore/VulkanCore/VansVideoTexture.h"
#include "../RuntimeCore/VansFramePhase.h"
#include "../RuntimeCore/VansThreadContract.h"
#include "../RuntimeUI/Public/VansUIScreen.h"
#include "../RuntimeUI/Public/VansUISystem.h"
#include "../SceneCore/VansSceneRuntimeComponentKey.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../SceneRuntime/VansRuntimeWorld.h"
#include "../Util/VansInputManager.h"
#include "../Util/VansLog.h"
#include "../Util/VansProfiler.h"
#include "../VansTimer.h"

#include <array>

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

extern "C"
{
#include <lauxlib.h>
#include <lualib.h>
}

VansScriptContext* VansScriptContext::s_Instance = nullptr;

namespace
{
struct LuaTransformUserdata
{
	std::uint32_t transformID = 0;
	Vans::VansEntityHandle runtimeEntity;
	Vans::VansComponentHandle runtimeComponent;
};

struct LuaObjectUserdata
{
	VansScriptObject* object = nullptr;
	Vans::VansEntityHandle runtimeEntity;
};

struct LuaComponentUserdata
{
	VansScriptComponent* component = nullptr;
	Vans::VansComponentHandle runtimeComponent;
};

VansScriptContext* Context()
{
	return VansScriptContext::GetInstance();
}

VansGraphics::VansScene* Scene()
{
	auto* context = Context();
	return context ? context->GetScene() : nullptr;
}

VansEngine::VansAudioManager* AudioManager()
{
	auto* scene = Scene();
	return scene ? scene->GetAudioManager() : nullptr;
}

std::filesystem::path ResolveScriptPath(const std::string& scriptPath)
{
	std::filesystem::path path(scriptPath);
	if (path.is_absolute())
		return path;

	auto* context = Context();
	auto& projectManager = Vans::VansProjectManager::Get();
	if (projectManager.IsProjectLoaded() && !projectManager.GetProjectRootPath().empty())
		return std::filesystem::path(projectManager.GetProjectRootPath()) / path;

	if (context && !context->GetActiveProjectRoot().empty())
		return std::filesystem::path(context->GetActiveProjectRoot()) / path;

	auto* config = VansConfigration::GetInstance();
	return std::filesystem::path(config->GetProjectRootPath()) / path;
}

int PushError(lua_State* L, const char* message)
{
	lua_pushstring(L, message);
	return lua_error(L);
}

Vans::VansComponentHandle ResolveRuntimeComponentHandle(
	const VansScriptObject* object,
	const VansScriptComponent* component);
Vans::VansEntityHandle ResolveRuntimeEntityHandle(const VansScriptObject* object);
Vans::VansComponentHandle ResolveRuntimeTransformComponentHandle(
	const VansScriptObject* object,
	std::uint32_t transformID);
VansScriptObject* FindObjectByTransformID(std::uint32_t transformID);

void InvalidateTransformHandle(LuaTransformUserdata* handle)
{
	if (!handle)
		return;
	handle->transformID = UINT32_MAX;
	handle->runtimeEntity = {};
	handle->runtimeComponent = {};
}

LuaTransformUserdata* CheckTransform(lua_State* L, int index)
{
	auto* handle = static_cast<LuaTransformUserdata*>(
		luaL_checkudata(L, index, "Vans.Transform"));
	if (!handle || handle->transformID == UINT32_MAX)
		return handle;
	auto* scene = Scene();
	auto* runtimeWorld = scene ? scene->GetRuntimeWorld() : nullptr;
	if (!runtimeWorld)
	{
		InvalidateTransformHandle(handle);
		return handle;
	}
	if (!handle->runtimeEntity.IsValid() || !runtimeWorld->IsAlive(handle->runtimeEntity) ||
		!handle->runtimeComponent.IsValid() || !runtimeWorld->GetComponentHeader(handle->runtimeComponent))
	{
		VansScriptObject* object = FindObjectByTransformID(handle->transformID);
		handle->runtimeEntity = ResolveRuntimeEntityHandle(object);
		handle->runtimeComponent = ResolveRuntimeTransformComponentHandle(object, handle->transformID);
	}
	const Vans::VansComponentHeader* header = runtimeWorld->GetComponentHeader(handle->runtimeComponent);
	if (!handle->runtimeEntity.IsValid() ||
		!runtimeWorld->IsAlive(handle->runtimeEntity) ||
		!header ||
		header->owner != handle->runtimeEntity)
	{
		InvalidateTransformHandle(handle);
	}
	return handle;
}

LuaObjectUserdata* CheckObject(lua_State* L, int index)
{
	auto* handle = static_cast<LuaObjectUserdata*>(
		luaL_checkudata(L, index, "Vans.Object"));
	if (!handle || !handle->object)
		return handle;
	auto* scene = Scene();
	auto* runtimeWorld = scene ? scene->GetRuntimeWorld() : nullptr;
	if (!runtimeWorld)
	{
		handle->object = nullptr;
		handle->runtimeEntity = {};
		return handle;
	}
	if (!handle->runtimeEntity.IsValid())
		handle->runtimeEntity = ResolveRuntimeEntityHandle(handle->object);
	if (!handle->runtimeEntity.IsValid() || !runtimeWorld->IsAlive(handle->runtimeEntity))
	{
		handle->object = nullptr;
		handle->runtimeEntity = {};
	}
	return handle;
}

LuaComponentUserdata* CheckComponent(lua_State* L, int index)
{
	auto* handle = static_cast<LuaComponentUserdata*>(
		luaL_checkudata(L, index, "Vans.Component"));
	if (!handle || !handle->component)
		return handle;
	auto* scene = Scene();
	auto* runtimeWorld = scene ? scene->GetRuntimeWorld() : nullptr;
	if (!runtimeWorld)
	{
		handle->component = nullptr;
		return handle;
	}
	if (!handle->runtimeComponent.IsValid())
	{
		for (auto* object : scene->GetSceneObjects())
		{
			if (!object)
				continue;
			const auto found = std::find(object->m_Components.begin(), object->m_Components.end(), handle->component);
			if (found != object->m_Components.end())
			{
				handle->runtimeComponent = ResolveRuntimeComponentHandle(object, handle->component);
				break;
			}
		}
	}
	if (!handle->runtimeComponent.IsValid() ||
		!runtimeWorld->GetComponentHeader(handle->runtimeComponent))
	{
		handle->component = nullptr;
	}
	return handle;
}

VansScriptObject* ResolveObject(LuaObjectUserdata* handle)
{
	if (!handle || !handle->object)
		return nullptr;
	auto* scene = Scene();
	auto* runtimeWorld = scene ? scene->GetRuntimeWorld() : nullptr;
	if (!runtimeWorld)
	{
		handle->object = nullptr;
		handle->runtimeEntity = {};
		return nullptr;
	}
	if (!handle->runtimeEntity.IsValid())
		handle->runtimeEntity = ResolveRuntimeEntityHandle(handle->object);
	if (!handle->runtimeEntity.IsValid() || !runtimeWorld->IsAlive(handle->runtimeEntity))
	{
		handle->object = nullptr;
		handle->runtimeEntity = {};
		return nullptr;
	}
	for (auto* object : scene->GetSceneObjects())
	{
		if (object != handle->object)
			continue;
		const Vans::VansEntityHandle entity = ResolveRuntimeEntityHandle(object);
		if (entity == handle->runtimeEntity)
			return object;
		break;
	}
	handle->object = nullptr;
	handle->runtimeEntity = {};
	return nullptr;
}

Vans::VansEntityHandle ResolveRuntimeEntityHandle(const VansScriptObject* object)
{
	if (!object || object->m_EntityGuid.empty())
		return Vans::VansEntityHandle{};
	auto* scene = Scene();
	auto* runtimeWorld = scene ? scene->GetRuntimeWorld() : nullptr;
	if (!runtimeWorld)
		return Vans::VansEntityHandle{};
	const Vans::VansEntityHandle entity = runtimeWorld->Entities().FindByGuid(object->m_EntityGuid);
	return runtimeWorld->IsAlive(entity) ? entity : Vans::VansEntityHandle{};
}

Vans::VansComponentHandle ResolveRuntimeTransformComponentHandle(
	const VansScriptObject* object,
	std::uint32_t transformID)
{
	auto* scene = Scene();
	auto* runtimeWorld = scene ? scene->GetRuntimeWorld() : nullptr;
	if (!runtimeWorld)
		return Vans::VansComponentHandle{};
	const Vans::VansEntityHandle entity = ResolveRuntimeEntityHandle(object);
	if (!entity.IsValid())
		return Vans::VansComponentHandle{};
	const auto* storage = static_cast<const Vans::VansComponentStorage<Vans::VansRuntimeTransformComponent>*>(
		runtimeWorld->FindStorage(Vans::VansRuntimeComponentType_Transform));
	if (!storage)
		return Vans::VansComponentHandle{};
	const auto& headers = storage->Headers();
	for (const Vans::VansComponentHeader& header : headers)
	{
		if (header.owner != entity)
			continue;
		const Vans::VansRuntimeTransformComponent* transform = storage->Get(header.self);
		if (!transform)
			continue;
		if (transformID == UINT32_MAX || transform->transformStoreId == transformID)
			return header.self;
	}
	return Vans::VansComponentHandle{};
}

Vans::VansComponentHandle ResolveRuntimeComponentHandle(
	const VansScriptObject* object,
	const VansScriptComponent* component)
{
	if (!object || !component || component->m_ComponentGuid.empty())
		return Vans::VansComponentHandle{};
	auto* scene = Scene();
	auto* runtimeWorld = scene ? scene->GetRuntimeWorld() : nullptr;
	if (!runtimeWorld)
		return Vans::VansComponentHandle{};
	const std::string runtimeKey = Vans::CanonicalRuntimeComponentKeyForName(component->m_ComponentName);
	const std::uint16_t typeId = Vans::VansRuntimeComponentTypeIdForKey(runtimeKey);
	if (typeId == Vans::VansInvalidComponentTypeId)
		return Vans::VansComponentHandle{};
	const Vans::VansComponentHandle handle = runtimeWorld->FindComponentByGuid(
		component->m_ComponentGuid,
		typeId);
	const Vans::VansComponentHeader* header = runtimeWorld->GetComponentHeader(handle);
	if (!header)
		return Vans::VansComponentHandle{};
	const Vans::VansEntityHandle entity = runtimeWorld->Entities().FindByGuid(object->m_EntityGuid);
	if (!entity.IsValid() || header->owner != entity)
		return Vans::VansComponentHandle{};
	return handle;
}

Vans::VansRuntimeParticleComponent* RuntimeParticleComponent(LuaComponentUserdata* handle)
{
	if (!handle || handle->runtimeComponent.typeId != Vans::VansRuntimeComponentType_Particle)
		return nullptr;
	auto* scene = Scene();
	auto* runtimeWorld = scene ? scene->GetRuntimeWorld() : nullptr;
	if (!runtimeWorld || !runtimeWorld->GetComponentHeader(handle->runtimeComponent))
		return nullptr;
	auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeParticleComponent>*>(
		runtimeWorld->FindStorage(Vans::VansRuntimeComponentType_Particle));
	return storage ? storage->Get(handle->runtimeComponent) : nullptr;
}

void SyncRuntimeAudioComponent(VansScriptAudioComponent* audio)
{
	if (!audio)
		return;
	if (auto* scene = Scene())
		scene->SyncRuntimeAudioComponentFromFacade(*audio);
}

void SyncRuntimeVideoComponent(VansScriptVideoComponent* video)
{
	if (!video)
		return;
	if (auto* scene = Scene())
		scene->SyncRuntimeVideoComponentFromFacade(*video);
}

template <typename T>
T* FindRuntimeBackedComponent(VansScriptObject* object)
{
	if (!object)
		return nullptr;
	for (auto* component : object->m_Components)
	{
		auto* typedComponent = dynamic_cast<T*>(component);
		if (!typedComponent)
			continue;
		if (ResolveRuntimeComponentHandle(object, typedComponent).IsValid())
			return typedComponent;
	}
	return nullptr;
}

std::size_t CountRuntimeBackedComponents(VansScriptObject* object)
{
	if (!object)
		return 0;
	std::size_t count = 0;
	for (auto* component : object->m_Components)
	{
		if (ResolveRuntimeComponentHandle(object, component).IsValid())
			++count;
	}
	return count;
}

VansScriptComponent* GetRuntimeBackedComponentByIndex(VansScriptObject* object, std::size_t targetIndex)
{
	if (!object)
		return nullptr;
	std::size_t runtimeIndex = 0;
	for (auto* component : object->m_Components)
	{
		if (!ResolveRuntimeComponentHandle(object, component).IsValid())
			continue;
		if (runtimeIndex == targetIndex)
			return component;
		++runtimeIndex;
	}
	return nullptr;
}

void PushVec3(lua_State* L, const glm::vec3& value)
{
	lua_createtable(L, 0, 3);
	lua_pushnumber(L, value.x);
	lua_setfield(L, -2, "x");
	lua_pushnumber(L, value.y);
	lua_setfield(L, -2, "y");
	lua_pushnumber(L, value.z);
	lua_setfield(L, -2, "z");
}

glm::vec3 ReadVec3(lua_State* L, int index)
{
	if (lua_istable(L, index))
	{
		index = lua_absindex(L, index);
		lua_getfield(L, index, "x");
		const float x = static_cast<float>(luaL_optnumber(L, -1, 0.0));
		lua_pop(L, 1);
		lua_getfield(L, index, "y");
		const float y = static_cast<float>(luaL_optnumber(L, -1, 0.0));
		lua_pop(L, 1);
		lua_getfield(L, index, "z");
		const float z = static_cast<float>(luaL_optnumber(L, -1, 0.0));
		lua_pop(L, 1);
		return glm::vec3(x, y, z);
	}

	return glm::vec3(
		static_cast<float>(luaL_checknumber(L, index)),
		static_cast<float>(luaL_checknumber(L, index + 1)),
		static_cast<float>(luaL_checknumber(L, index + 2)));
}

VansScriptObject* FindObjectByTransformID(std::uint32_t transformID)
{
	auto* scene = Scene();
	if (!scene)
		return nullptr;
	for (auto* object : scene->GetSceneObjects())
	{
		if (object && object->m_TransformID == transformID)
			return object;
	}
	return nullptr;
}

void PushTransform(lua_State* L, std::uint32_t transformID)
{
	VansScriptObject* object = FindObjectByTransformID(transformID);
	const Vans::VansEntityHandle runtimeEntity = ResolveRuntimeEntityHandle(object);
	const Vans::VansComponentHandle runtimeComponent =
		ResolveRuntimeTransformComponentHandle(object, transformID);
	if (!runtimeEntity.IsValid() || !runtimeComponent.IsValid())
	{
		lua_pushnil(L);
		return;
	}
	auto* userdata = static_cast<LuaTransformUserdata*>(
		lua_newuserdatauv(L, sizeof(LuaTransformUserdata), 0));
	userdata->transformID = transformID;
	userdata->runtimeEntity = runtimeEntity;
	userdata->runtimeComponent = runtimeComponent;
	luaL_getmetatable(L, "Vans.Transform");
	lua_setmetatable(L, -2);
}

void PushObject(lua_State* L, VansScriptObject* object)
{
	if (!object)
	{
		lua_pushnil(L);
		return;
	}
	auto* userdata = static_cast<LuaObjectUserdata*>(
		lua_newuserdatauv(L, sizeof(LuaObjectUserdata), 0));
	userdata->object = object;
	userdata->runtimeEntity = ResolveRuntimeEntityHandle(object);
	if (!userdata->runtimeEntity.IsValid())
	{
		lua_pop(L, 1);
		lua_pushnil(L);
		return;
	}
	luaL_getmetatable(L, "Vans.Object");
	lua_setmetatable(L, -2);
}

void PushComponent(lua_State* L, VansScriptComponent* component)
{
	if (!component)
	{
		lua_pushnil(L);
		return;
	}
	Vans::VansComponentHandle runtimeComponent;
	if (auto* scene = Scene())
	{
		for (auto* object : scene->GetSceneObjects())
		{
			if (!object)
				continue;
			const auto found = std::find(object->m_Components.begin(), object->m_Components.end(), component);
			if (found != object->m_Components.end())
			{
				runtimeComponent = ResolveRuntimeComponentHandle(object, component);
				break;
			}
		}
	}
	if (!runtimeComponent.IsValid())
	{
		lua_pushnil(L);
		return;
	}
	auto* userdata = static_cast<LuaComponentUserdata*>(
		lua_newuserdatauv(L, sizeof(LuaComponentUserdata), 0));
	userdata->component = component;
	userdata->runtimeComponent = runtimeComponent;
	luaL_getmetatable(L, "Vans.Component");
	lua_setmetatable(L, -2);
}

int LuaTransformGetPosition(lua_State* L)
{
	auto* handle = CheckTransform(L, 1);
	if (!handle || handle->transformID == UINT32_MAX)
	{
		lua_pushnil(L);
		return 1;
	}
	auto& transform = VansGraphics::VansTransformStore::GetTransform(handle->transformID);
	PushVec3(L, transform.m_Position);
	return 1;
}

int LuaTransformSetPosition(lua_State* L)
{
	auto* handle = CheckTransform(L, 1);
	if (!handle || handle->transformID == UINT32_MAX)
		return 0;
	auto& transform = VansGraphics::VansTransformStore::GetTransform(handle->transformID);
	transform.m_Position = ReadVec3(L, 2);
	return 0;
}

int LuaTransformGetRotation(lua_State* L)
{
	auto* handle = CheckTransform(L, 1);
	if (!handle || handle->transformID == UINT32_MAX)
	{
		lua_pushnil(L);
		return 1;
	}
	auto& transform = VansGraphics::VansTransformStore::GetTransform(handle->transformID);
	PushVec3(L, transform.m_Rotation);
	return 1;
}

int LuaTransformSetRotation(lua_State* L)
{
	auto* handle = CheckTransform(L, 1);
	if (!handle || handle->transformID == UINT32_MAX)
		return 0;
	auto& transform = VansGraphics::VansTransformStore::GetTransform(handle->transformID);
	transform.m_Rotation = ReadVec3(L, 2);
	return 0;
}

int LuaTransformTranslate(lua_State* L)
{
	auto* handle = CheckTransform(L, 1);
	if (!handle || handle->transformID == UINT32_MAX)
		return 0;
	auto& transform = VansGraphics::VansTransformStore::GetTransform(handle->transformID);
	transform.m_Position += ReadVec3(L, 2);
	return 0;
}

int LuaObjectIsValid(lua_State* L)
{
	auto* object = ResolveObject(CheckObject(L, 1));
	lua_pushboolean(L, object != nullptr);
	return 1;
}

int LuaObjectGetName(lua_State* L)
{
	auto* object = ResolveObject(CheckObject(L, 1));
	lua_pushstring(L, object ? object->m_ObjectName.c_str() : "");
	return 1;
}

int LuaObjectGetTransform(lua_State* L)
{
	auto* object = ResolveObject(CheckObject(L, 1));
	if (!object)
		lua_pushnil(L);
	else
		PushTransform(L, object->m_TransformID);
	return 1;
}

int LuaObjectGetTransformID(lua_State* L)
{
	auto* object = ResolveObject(CheckObject(L, 1));
	lua_pushinteger(L, object ? static_cast<lua_Integer>(object->m_TransformID) : -1);
	return 1;
}

int LuaObjectGetComponentCount(lua_State* L)
{
	auto* object = ResolveObject(CheckObject(L, 1));
	lua_pushinteger(L, static_cast<lua_Integer>(CountRuntimeBackedComponents(object)));
	return 1;
}

int LuaObjectGetComponentByIndex(lua_State* L)
{
	auto* object = ResolveObject(CheckObject(L, 1));
	const lua_Integer index = luaL_checkinteger(L, 2);
	if (!object || index < 0)
	{
		lua_pushnil(L);
		return 1;
	}
	PushComponent(L, GetRuntimeBackedComponentByIndex(object, static_cast<std::size_t>(index)));
	return 1;
}

template<typename T>
int PushObjectComponent(lua_State* L)
{
	auto* object = ResolveObject(CheckObject(L, 1));
	PushComponent(L, FindRuntimeBackedComponent<T>(object));
	return 1;
}

int LuaObjectDestroy(lua_State* L)
{
	auto* handle = CheckObject(L, 1);
	auto* object = ResolveObject(handle);
	auto* scene = Scene();
	const bool queued = scene && object && scene->QueueDestroyEntity(object);
	if (queued)
	{
		handle->object = nullptr;
		handle->runtimeEntity = {};
	}
	lua_pushboolean(L, queued);
	return 1;
}

int LuaComponentIsValid(lua_State* L)
{
	auto* handle = CheckComponent(L, 1);
	bool valid = handle && handle->component != nullptr;
	if (valid && handle->runtimeComponent.IsValid())
	{
		auto* scene = Scene();
		auto* runtimeWorld = scene ? scene->GetRuntimeWorld() : nullptr;
		valid = runtimeWorld && runtimeWorld->GetComponentHeader(handle->runtimeComponent) != nullptr;
	}
	lua_pushboolean(L, valid);
	return 1;
}

int LuaComponentSetEnabled(lua_State* L)
{
	auto* handle = CheckComponent(L, 1);
	auto* component = handle ? handle->component : nullptr;
	if (!component)
		return 0;

	const bool enabled = lua_toboolean(L, 2) != 0;
	auto* scene = Scene();
	auto* runtimeWorld = scene ? scene->GetRuntimeWorld() : nullptr;
	bool projectedByRuntime = false;
	bool runtimeEffectiveEnabled = enabled;
	if (runtimeWorld && handle->runtimeComponent.IsValid())
	{
		runtimeWorld->Commands().SetComponentEnabled(handle->runtimeComponent, enabled);
		runtimeWorld->FlushCommands();
		runtimeEffectiveEnabled = runtimeWorld->IsComponentEffectivelyEnabled(handle->runtimeComponent);
		projectedByRuntime = scene->ApplyRuntimeComponentEnabled(
			handle->runtimeComponent,
			runtimeEffectiveEnabled);
	}

	if (projectedByRuntime)
	{
		component->MirrorRuntimeEnabledState(enabled, runtimeEffectiveEnabled);
		if (handle->runtimeComponent.typeId == Vans::VansRuntimeComponentType_UI)
		{
			std::vector<std::uint64_t> openScreens;
			if (scene->CopyRuntimeUIOpenScreens(handle->runtimeComponent, openScreens))
				component->MirrorRuntimeOpenScreens(openScreens);
		}
	}
	else
	{
		component->SetEnabled(enabled);
		if (scene &&
			handle->runtimeComponent.IsValid() &&
			handle->runtimeComponent.typeId == Vans::VansRuntimeComponentType_Script)
		{
			if (auto* luaComponent = dynamic_cast<VansLuaScriptComponent*>(component))
				scene->SyncRuntimeScriptComponentFromFacade(component->m_ComponentGuid, *luaComponent);
		}
	}
	return 0;
}

int LuaComponentIsEnabled(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	lua_pushboolean(L, component ? component->IsEnabled() : false);
	return 1;
}

int LuaComponentPlay(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* audio = dynamic_cast<VansScriptAudioComponent*>(component))
	{
		audio->m_Source.Play();
	}
	else if (auto* video = dynamic_cast<VansScriptVideoComponent*>(component))
	{
		if (video->m_VideoTex) video->m_VideoTex->Play();
	}
	else if (auto* particle = dynamic_cast<VansScriptParticleComponent*>(component))
	{
		particle->Play();
	}
	else if (auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component))
	{
		if (anim->m_AnimNode) anim->m_AnimNode->Play();
	}
	return 0;
}

int LuaComponentPause(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* audio = dynamic_cast<VansScriptAudioComponent*>(component))
	{
		audio->m_Source.Pause();
	}
	else if (auto* video = dynamic_cast<VansScriptVideoComponent*>(component))
	{
		if (video->m_VideoTex) video->m_VideoTex->Pause();
	}
	else if (auto* particle = dynamic_cast<VansScriptParticleComponent*>(component))
	{
		particle->Pause();
	}
	else if (auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component))
	{
		if (anim->m_AnimNode) anim->m_AnimNode->Pause();
	}
	return 0;
}

int LuaComponentStop(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* audio = dynamic_cast<VansScriptAudioComponent*>(component))
	{
		audio->m_Source.Stop();
	}
	else if (auto* particle = dynamic_cast<VansScriptParticleComponent*>(component))
	{
		particle->Stop();
	}
	else if (auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component))
	{
		if (anim->m_AnimNode) anim->m_AnimNode->Stop();
	}
	return 0;
}

int LuaComponentResume(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* audio = dynamic_cast<VansScriptAudioComponent*>(component))
	{
		audio->m_Source.Resume();
	}
	else if (auto* video = dynamic_cast<VansScriptVideoComponent*>(component))
	{
		if (video->m_VideoTex) video->m_VideoTex->Play();
	}
	else if (auto* particle = dynamic_cast<VansScriptParticleComponent*>(component))
	{
		particle->Play();
	}
	else if (auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component))
	{
		if (anim->m_AnimNode) anim->m_AnimNode->Play();
	}
	return 0;
}

int LuaComponentRestart(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* particle = dynamic_cast<VansScriptParticleComponent*>(component))
		particle->Restart();
	else if (auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component))
	{
		if (anim->m_AnimNode)
		{
			anim->m_AnimNode->Stop();
			anim->m_AnimNode->Play();
		}
	}
	else
		LuaComponentResume(L);
	return 0;
}

int LuaComponentUIOpenScreen(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* ui = dynamic_cast<VansScriptUIComponent*>(component);
	const char* path = luaL_checkstring(L, 2);
	if (ui && path && VansRuntime::VansUISystem::Get().IsInitialized())
	{
		auto screen = VansRuntime::VansUISystem::Get().LoadScreen(path);
		if (screen)
			ui->m_OpenScreens.push_back(screen->GetHandleId());
	}
	return 0;
}

int LuaComponentUICloseAllOwned(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* ui = dynamic_cast<VansScriptUIComponent*>(component))
		ui->CloseOpenedScreens();
	return 0;
}

int LuaComponentUIPreload(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* ui = dynamic_cast<VansScriptUIComponent*>(component);
	const char* path = luaL_checkstring(L, 2);
	if (ui && path && VansRuntime::VansUISystem::Get().IsInitialized())
	{
		ui->m_PreloadScreens.push_back(path);
		VansRuntime::VansUISystem::Get().PreloadScreen(path);
	}
	return 0;
}

int LuaComponentUIReleasePreloaded(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* ui = dynamic_cast<VansScriptUIComponent*>(component))
		ui->ReleasePreloaded();
	return 0;
}

int LuaComponentSwitchSource(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const char* name = luaL_checkstring(L, 2);
	bool ok = false;
	if (auto* audio = dynamic_cast<VansScriptAudioComponent*>(component))
		ok = audio->SwitchSource(name);
	else if (auto* video = dynamic_cast<VansScriptVideoComponent*>(component))
		ok = video->SwitchSource(name);
	lua_pushboolean(L, ok);
	return 1;
}

int LuaComponentIsPlaying(lua_State* L)
{
	auto* handle = CheckComponent(L, 1);
	auto* component = handle->component;
	bool value = false;
	if (auto* audio = dynamic_cast<VansScriptAudioComponent*>(component))
		value = audio->m_Source.IsPlaying();
	else if (auto* video = dynamic_cast<VansScriptVideoComponent*>(component))
		value = video->m_VideoTex && video->m_VideoTex->IsPlaying();
	else if (auto* particle = dynamic_cast<VansScriptParticleComponent*>(component))
	{
		if (auto* runtimeParticle = RuntimeParticleComponent(handle))
			value = runtimeParticle->isPlaying;
		else
			value = particle->m_IsPlaying;
	}
	lua_pushboolean(L, value);
	return 1;
}

int LuaComponentIsPaused(lua_State* L)
{
	auto* handle = CheckComponent(L, 1);
	auto* component = handle->component;
	bool value = false;
	if (auto* audio = dynamic_cast<VansScriptAudioComponent*>(component))
		value = audio->m_Source.IsPaused();
	else if (auto* video = dynamic_cast<VansScriptVideoComponent*>(component))
		value = video->m_VideoTex && video->m_VideoTex->IsReady() && !video->m_VideoTex->IsPlaying();
	else if (auto* particle = dynamic_cast<VansScriptParticleComponent*>(component))
	{
		if (auto* runtimeParticle = RuntimeParticleComponent(handle))
			value = runtimeParticle->runtime != nullptr && !runtimeParticle->isPlaying;
		else
			value = particle->m_Runtime != nullptr && !particle->m_IsPlaying;
	}
	lua_pushboolean(L, value);
	return 1;
}

int LuaComponentQueueMove(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* cct = dynamic_cast<VansScriptCharacterControllerComponent*>(component);
	if (cct && cct->m_ControllerNode)
	{
		const glm::vec3 movement = ReadVec3(L, 2);
		const int deltaTimeIndex = lua_istable(L, 2) ? 3 : 5;
		const float deltaTime = static_cast<float>(
			luaL_optnumber(L, deltaTimeIndex, VansGraphics::VansTimer::GetDeltaTime()));
		cct->m_ControllerNode->QueueMove(movement, deltaTime);
	}
	return 0;
}

int LuaComponentSetMotionIntent(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* cct = dynamic_cast<VansScriptCharacterControllerComponent*>(component);
	if (!cct || !cct->m_ControllerNode)
		return 0;

	Vans::VansCharacterMotionIntent intent;
	const glm::vec3 moveInputLocal = ReadVec3(L, 2);
	intent.moveInputLocal = glm::vec2(moveInputLocal.x, moveInputLocal.z);
	intent.desiredSpeed = static_cast<float>(luaL_optnumber(L, 3, 0.0));
	intent.movementReferenceYaw = static_cast<float>(luaL_optnumber(L, 4, 0.0));
	intent.desiredFacingYaw = static_cast<float>(luaL_optnumber(L, 5, 0.0));
	intent.hasFacing = lua_gettop(L) >= 5 && !lua_isnil(L, 5);
	intent.jumpRequested = lua_toboolean(L, 6) != 0;
	intent.jumpSpeed = static_cast<float>(luaL_optnumber(L, 7, 5.5));
	intent.gravity = static_cast<float>(luaL_optnumber(L, 8, 16.0));
	intent.valid = true;
	cct->m_ControllerNode->SetMotionIntent(intent);
	return 0;
}

int LuaComponentVehicleSetInputs(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* vehicle = dynamic_cast<VansScriptVehicleComponent*>(component);
	if (vehicle && vehicle->m_Vehicle)
	{
		const float throttle = static_cast<float>(luaL_optnumber(L, 2, 0.0));
		const float brake = static_cast<float>(luaL_optnumber(L, 3, 0.0));
		const float steer = static_cast<float>(luaL_optnumber(L, 4, 0.0));
		const float handbrake = static_cast<float>(luaL_optnumber(L, 5, 0.0));
		auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
		std::lock_guard<std::mutex> simLock(physics.GetSimulationMutex());
		vehicle->m_Vehicle->SetInputs(throttle, brake, steer, handbrake);
	}
	return 0;
}

int LuaComponentVehicleSetGear(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* vehicle = dynamic_cast<VansScriptVehicleComponent*>(component);
	if (vehicle && vehicle->m_Vehicle)
		vehicle->m_Vehicle->SetGear(static_cast<std::uint32_t>(luaL_checkinteger(L, 2)));
	return 0;
}

int LuaComponentVehicleSetAutomaticGear(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* vehicle = dynamic_cast<VansScriptVehicleComponent*>(component);
	if (vehicle && vehicle->m_Vehicle)
		vehicle->m_Vehicle->SetAutomaticGear(lua_toboolean(L, 2) != 0);
	return 0;
}

int LuaComponentSetPosition(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const glm::vec3 position = ReadVec3(L, 2);
	if (auto* cct = dynamic_cast<VansScriptCharacterControllerComponent*>(component))
	{
		if (cct->m_ControllerNode) cct->m_ControllerNode->SetPosition(position);
	}
	else if (auto* audio = dynamic_cast<VansScriptAudioComponent*>(component))
	{
		audio->m_Source.SetPosition(position.x, position.y, position.z);
	}
	return 0;
}

int LuaComponentSyncControllerFromTransform(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* cct = dynamic_cast<VansScriptCharacterControllerComponent*>(component);
	if (cct && cct->m_ControllerNode)
	{
		auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
		std::lock_guard<std::mutex> simLock(physics.GetSimulationMutex());
		cct->m_ControllerNode->SyncControllerFromTransform();
	}
	return 0;
}

int LuaComponentGetPosition(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* cct = dynamic_cast<VansScriptCharacterControllerComponent*>(component))
	{
		if (cct->m_ControllerNode)
		{
			PushVec3(L, cct->m_ControllerNode->GetPosition());
			return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

int LuaComponentIsGrounded(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* cct = dynamic_cast<VansScriptCharacterControllerComponent*>(component);
	lua_pushboolean(L, cct && cct->m_ControllerNode && cct->m_ControllerNode->IsGrounded());
	return 1;
}

int LuaComponentBindFollowRagdoll(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* cct = dynamic_cast<VansScriptCharacterControllerComponent*>(component);
	auto* ragdollHandle = CheckComponent(L, 2);
	auto* ragdoll = ragdollHandle ? dynamic_cast<VansScriptRagdollComponent*>(ragdollHandle->component) : nullptr;
	const char* bone = luaL_optstring(L, 3, "pelvis");
	if (cct)
		cct->BindFollowRagdoll(ragdoll, bone);
	return 0;
}

int LuaComponentClearFollowRagdoll(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* cct = dynamic_cast<VansScriptCharacterControllerComponent*>(component))
		cct->ClearFollowRagdoll();
	return 0;
}

int LuaComponentIsFollowRagdollEnabled(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* cct = dynamic_cast<VansScriptCharacterControllerComponent*>(component);
	lua_pushboolean(L, cct && cct->IsFollowRagdollEnabled());
	return 1;
}

int LuaComponentAnimPlayState(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const char* stateName = luaL_checkstring(L, 2);
	auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component);
	if (anim && anim->m_AnimNode)
		anim->m_AnimNode->Play(stateName);
	return 0;
}

int LuaComponentAnimSetBool(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const char* name = luaL_checkstring(L, 2);
	bool value = lua_toboolean(L, 3) != 0;
	auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component);
	if (anim && anim->m_AnimNode && anim->m_AnimNode->GetController())
		anim->m_AnimNode->GetController()->SetBool(name, value);
	return 0;
}

int LuaComponentAnimSetRootMotionEnabled(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const bool enabled = lua_toboolean(L, 2) != 0;
	auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component);
	if (anim && anim->m_AnimNode)
		anim->m_AnimNode->EnableRootMotion(enabled);
	return 0;
}

int LuaComponentAnimSetFloat(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const char* name = luaL_checkstring(L, 2);
	float value = static_cast<float>(luaL_checknumber(L, 3));
	auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component);
	if (anim && anim->m_AnimNode && anim->m_AnimNode->GetController())
		anim->m_AnimNode->GetController()->SetFloat(name, value);
	return 0;
}

int LuaComponentAnimSetInt(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const char* name = luaL_checkstring(L, 2);
	int value = static_cast<int>(luaL_checkinteger(L, 3));
	auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component);
	if (anim && anim->m_AnimNode && anim->m_AnimNode->GetController())
		anim->m_AnimNode->GetController()->SetInt(name, value);
	return 0;
}

int LuaComponentAnimSetVector3(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const char* name = luaL_checkstring(L, 2);
	const glm::vec3 value(
		static_cast<float>(luaL_checknumber(L, 3)),
		static_cast<float>(luaL_checknumber(L, 4)),
		static_cast<float>(luaL_checknumber(L, 5)));
	auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component);
	if (anim && anim->m_AnimNode && anim->m_AnimNode->GetController())
		anim->m_AnimNode->GetController()->SetVector3(name, value);
	return 0;
}

int LuaComponentAnimSetTrigger(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const char* name = luaL_checkstring(L, 2);
	auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component);
	if (anim && anim->m_AnimNode && anim->m_AnimNode->GetController())
		anim->m_AnimNode->GetController()->SetTrigger(name);
	return 0;
}

int LuaComponentAnimSwitchGraphSet(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const char* graphSetId = luaL_checkstring(L, 2);
	auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component);
	if (!anim || !anim->m_AnimNode)
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	const VansGraphics::VansGraphSetSwitchResult result = anim->m_AnimNode->SwitchGraphSet(graphSetId);
	const bool accepted = result == VansGraphics::VansGraphSetSwitchResult::Started
		|| result == VansGraphics::VansGraphSetSwitchResult::Completed
		|| result == VansGraphics::VansGraphSetSwitchResult::AlreadyActive
		|| result == VansGraphics::VansGraphSetSwitchResult::Queued;
	lua_pushboolean(L, accepted ? 1 : 0);
	return 1;
}

int LuaComponentAnimGetActiveGraphSet(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component);
	if (!anim || !anim->m_AnimNode)
	{
		lua_pushnil(L);
		return 1;
	}
	const std::string& graphSetId = anim->m_AnimNode->GetActiveGraphSetId();
	lua_pushlstring(L, graphSetId.data(), graphSetId.size());
	return 1;
}

int LuaComponentAnimIsGraphSetTransitioning(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component);
	lua_pushboolean(L, anim && anim->m_AnimNode
		&& anim->m_AnimNode->IsGraphSetTransitioning());
	return 1;
}

int LuaComponentAnimGetGraphSetTransitionProgress(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* anim = dynamic_cast<VansScriptAnimationComponent*>(component);
	lua_pushnumber(L, anim && anim->m_AnimNode
		? anim->m_AnimNode->GetGraphSetTransitionProgress() : 0.0f);
	return 1;
}

int LuaComponentGetVolume(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushnumber(L, audio ? audio->m_Source.GetVolume() : 0.0f);
	return 1;
}

int LuaComponentSetVolume(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
		audio->m_Source.SetVolume(static_cast<float>(luaL_checknumber(L, 2)));
	return 0;
}

int LuaComponentGetPitch(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushnumber(L, audio ? audio->m_Source.GetPitch() : 0.0f);
	return 1;
}

int LuaComponentSetPitch(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
		audio->m_Source.SetPitch(static_cast<float>(luaL_checknumber(L, 2)));
	return 0;
}

int LuaComponentGetLoop(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushboolean(L, audio && audio->m_Source.GetLoop());
	return 1;
}

int LuaComponentSetLoop(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
		audio->m_Source.SetLoop(lua_toboolean(L, 2) != 0);
	return 0;
}

int LuaComponentGetSpatial(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushboolean(L, audio && audio->m_Source.GetSpatial());
	return 1;
}

int LuaComponentSetSpatial(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
		audio->m_Source.SetSpatial(lua_toboolean(L, 2) != 0);
	return 0;
}

int LuaComponentGetRefDistance(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushnumber(L, audio ? audio->m_Source.GetRefDistance() : 0.0f);
	return 1;
}

int LuaComponentSetRefDistance(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
		audio->m_Source.SetRefDistance(static_cast<float>(luaL_checknumber(L, 2)));
	return 0;
}

int LuaComponentGetMaxDistance(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushnumber(L, audio ? audio->m_Source.GetMaxDistance() : 0.0f);
	return 1;
}

int LuaComponentSetMaxDistance(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
		audio->m_Source.SetMaxDistance(static_cast<float>(luaL_checknumber(L, 2)));
	return 0;
}

int LuaComponentGetRolloff(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushnumber(L, audio ? audio->m_Source.GetRolloff() : 0.0f);
	return 1;
}

int LuaComponentSetRolloff(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
		audio->m_Source.SetRolloff(static_cast<float>(luaL_checknumber(L, 2)));
	return 0;
}

int LuaComponentGetAttenuationMode(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushstring(L, audio
		? VansEngine::AudioAttenuationModeToString(audio->m_Source.GetAttenuationMode())
		: "");
	return 1;
}

int LuaComponentSetAttenuationMode(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
		audio->m_Source.SetAttenuationMode(
			VansEngine::AudioAttenuationModeFromString(luaL_checkstring(L, 2)));
	return 0;
}

int LuaComponentGetReverbSend(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushnumber(L, audio ? audio->m_Source.GetReverbSend() : 0.0f);
	return 1;
}

int LuaComponentSetReverbSend(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
		audio->m_Source.SetReverbSend(static_cast<float>(luaL_checknumber(L, 2)));
	return 0;
}

int LuaComponentGetLowpassHighFrequencyGain(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushnumber(L, audio ? audio->m_Source.GetLowpassHighFrequencyGain() : 1.0f);
	return 1;
}

int LuaComponentSetLowpassHighFrequencyGain(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
		audio->m_Source.SetLowpassHighFrequencyGain(static_cast<float>(luaL_checknumber(L, 2)));
	return 0;
}

int LuaComponentGetBus(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushstring(L, audio ? audio->m_Source.GetBusName().c_str() : "");
	return 1;
}

int LuaComponentSetBus(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
		audio->m_Source.SetBusName(luaL_checkstring(L, 2));
	return 0;
}

void PushLuaAudioBusState(lua_State* L, const VansEngine::AudioBusState& state, float effectiveGain)
{
	lua_newtable(L);
	lua_pushnumber(L, state.gain); lua_setfield(L, -2, "gain");
	lua_pushnumber(L, state.targetGain); lua_setfield(L, -2, "target_gain");
	lua_pushnumber(L, effectiveGain); lua_setfield(L, -2, "effective_gain");
	lua_pushnumber(L, state.lowpassHighFrequencyGain); lua_setfield(L, -2, "lowpass_high_frequency_gain");
	lua_pushnumber(L, state.lowpassHighFrequencyGain); lua_setfield(L, -2, "lowpassHighFrequencyGain");
	lua_pushboolean(L, state.muted); lua_setfield(L, -2, "muted");
	lua_pushboolean(L, state.soloed); lua_setfield(L, -2, "soloed");
	lua_pushboolean(L, VansEngine::IsAudioBusFading(state)); lua_setfield(L, -2, "fading");
}

bool ReadLuaOptionalNumberField(lua_State* L, int tableIndex, const char* field, float& value)
{
	lua_getfield(L, tableIndex, field);
	const bool hasValue = lua_isnumber(L, -1) != 0;
	if (hasValue)
		value = static_cast<float>(lua_tonumber(L, -1));
	lua_pop(L, 1);
	return hasValue;
}

bool ReadLuaOptionalBoolField(lua_State* L, int tableIndex, const char* field, bool& value)
{
	lua_getfield(L, tableIndex, field);
	const bool hasValue = lua_isboolean(L, -1) != 0;
	if (hasValue)
		value = lua_toboolean(L, -1) != 0;
	lua_pop(L, 1);
	return hasValue;
}

std::string ReadLuaOptionalStringField(lua_State* L, int tableIndex, const char* field)
{
	lua_getfield(L, tableIndex, field);
	std::string value;
	if (lua_isstring(L, -1))
		value = lua_tostring(L, -1);
	lua_pop(L, 1);
	return value;
}

bool ReadLuaAudioSnapshotEntry(
	lua_State* L,
	int keyIndex,
	int valueIndex,
	VansEngine::AudioBusSnapshotEntry& entry)
{
	keyIndex = lua_absindex(L, keyIndex);
	valueIndex = lua_absindex(L, valueIndex);

	if (lua_isnumber(L, valueIndex) && lua_type(L, keyIndex) == LUA_TSTRING)
	{
		entry.busName = lua_tostring(L, keyIndex);
		entry.gain = static_cast<float>(lua_tonumber(L, valueIndex));
		return true;
	}

	if (!lua_istable(L, valueIndex))
		return false;

	entry.busName = ReadLuaOptionalStringField(L, valueIndex, "bus");
	if (entry.busName.empty())
		entry.busName = ReadLuaOptionalStringField(L, valueIndex, "name");
	if (entry.busName.empty() && lua_type(L, keyIndex) == LUA_TSTRING)
		entry.busName = lua_tostring(L, keyIndex);
	if (entry.busName.empty())
		return false;

	if (!ReadLuaOptionalNumberField(L, valueIndex, "gain", entry.gain))
	{
		lua_rawgeti(L, valueIndex, 1);
		if (lua_isnumber(L, -1))
			entry.gain = static_cast<float>(lua_tonumber(L, -1));
		lua_pop(L, 1);
	}

	entry.overrideMuted = ReadLuaOptionalBoolField(L, valueIndex, "muted", entry.muted);
	entry.overrideSoloed = ReadLuaOptionalBoolField(L, valueIndex, "soloed", entry.soloed);
	entry.overrideLowpassHighFrequencyGain =
		ReadLuaOptionalNumberField(L, valueIndex, "lowpassHighFrequencyGain", entry.lowpassHighFrequencyGain) ||
		ReadLuaOptionalNumberField(L, valueIndex, "lowpass_high_frequency_gain", entry.lowpassHighFrequencyGain);
	return true;
}

int LuaAudioSetBusGain(lua_State* L)
{
	auto* manager = AudioManager();
	if (manager)
		manager->SetBusGain(luaL_checkstring(L, 1), static_cast<float>(luaL_checknumber(L, 2)));
	lua_pushboolean(L, manager != nullptr);
	return 1;
}

int LuaAudioSetBusLowpassHighFrequencyGain(lua_State* L)
{
	auto* manager = AudioManager();
	if (manager)
		manager->SetBusLowpassHighFrequencyGain(
			luaL_checkstring(L, 1),
			static_cast<float>(luaL_checknumber(L, 2)));
	lua_pushboolean(L, manager != nullptr);
	return 1;
}

int LuaAudioFadeBusGain(lua_State* L)
{
	auto* manager = AudioManager();
	if (manager)
	{
		manager->FadeBusGain(
			luaL_checkstring(L, 1),
			static_cast<float>(luaL_checknumber(L, 2)),
			static_cast<float>(luaL_optnumber(L, 3, 0.25)));
	}
	lua_pushboolean(L, manager != nullptr);
	return 1;
}

int LuaAudioSetBusMuted(lua_State* L)
{
	auto* manager = AudioManager();
	if (manager)
		manager->SetBusMuted(luaL_checkstring(L, 1), lua_toboolean(L, 2) != 0);
	lua_pushboolean(L, manager != nullptr);
	return 1;
}

int LuaAudioSetBusSoloed(lua_State* L)
{
	auto* manager = AudioManager();
	if (manager)
		manager->SetBusSoloed(luaL_checkstring(L, 1), lua_toboolean(L, 2) != 0);
	lua_pushboolean(L, manager != nullptr);
	return 1;
}

int LuaAudioGetBusState(lua_State* L)
{
	auto* manager = AudioManager();
	if (!manager)
	{
		lua_pushnil(L);
		return 1;
	}

	const std::string busName = luaL_checkstring(L, 1);
	PushLuaAudioBusState(
		L,
		manager->GetBusState(busName),
		manager->GetEffectiveBusGain(busName));
	return 1;
}

int LuaAudioApplySnapshot(lua_State* L)
{
	auto* manager = AudioManager();
	if (!manager)
	{
		lua_pushinteger(L, 0);
		return 1;
	}

	luaL_checktype(L, 1, LUA_TTABLE);
	const int snapshotTable = lua_absindex(L, 1);
	VansEngine::AudioBusSnapshot snapshot;
	snapshot.fadeSeconds = static_cast<float>(luaL_optnumber(L, 2, snapshot.fadeSeconds));
	ReadLuaOptionalNumberField(L, snapshotTable, "fadeSeconds", snapshot.fadeSeconds);
	ReadLuaOptionalNumberField(L, snapshotTable, "fade_seconds", snapshot.fadeSeconds);

	lua_pushnil(L);
	while (lua_next(L, snapshotTable) != 0)
	{
		if (lua_type(L, -2) == LUA_TSTRING)
		{
			const std::string key = lua_tostring(L, -2);
			if (key == "fadeSeconds" || key == "fade_seconds")
			{
				lua_pop(L, 1);
				continue;
			}
		}

		VansEngine::AudioBusSnapshotEntry entry;
		if (ReadLuaAudioSnapshotEntry(L, -2, -1, entry))
			snapshot.buses.push_back(entry);
		lua_pop(L, 1);
	}

	manager->ApplyBusSnapshot(snapshot);
	lua_pushinteger(L, static_cast<lua_Integer>(snapshot.buses.size()));
	return 1;
}

int LuaAudioApplyNamedSnapshot(lua_State* L)
{
	auto* manager = AudioManager();
	const bool applied = manager && manager->ApplyNamedBusSnapshot(luaL_checkstring(L, 1));
	lua_pushboolean(L, applied);
	return 1;
}

std::filesystem::path AudioControlAssetReadPath(const Vans::VansAssetRecord& record)
{
	if (!record.authoringPath.empty())
		return record.authoringPath;
	if (!record.artifactPath.empty() && record.artifactFormat == Vans::VansAssetArtifactFormat::Source)
		return record.artifactPath;
	return record.sourcePath;
}

std::optional<Vans::VansAssetRecord> FindAudioControlAssetRecord(const std::string& token)
{
	auto& projectManager = Vans::VansProjectManager::Get();
	Vans::VansAssetGuid guid;
	if (Vans::VansAssetGuid::TryParse(token, guid))
		return projectManager.FindAssetRecord(guid);
	return std::nullopt;
}

int LuaAudioApplySnapshotAsset(lua_State* L)
{
	auto* manager = AudioManager();
	if (!manager)
	{
		lua_pushinteger(L, 0);
		return 1;
	}

	const std::string token = luaL_checkstring(L, 1);
	const auto record = FindAudioControlAssetRecord(token);
	if (!record || record->type != Vans::VansAssetType::AudioBusSnapshot)
	{
		VANS_LOG_WARN("[LuaAudio] Audio bus snapshot asset not found: " << token);
		lua_pushinteger(L, 0);
		return 1;
	}

	Vans::VansAudioBusSnapshotAsset asset;
	std::string error;
	if (!Vans::VansAudioBusSnapshotAssetStorage::Load(AudioControlAssetReadPath(*record), asset, error))
	{
		VANS_LOG_WARN("[LuaAudio] Failed to load audio bus snapshot asset '" << token << "': " << error);
		lua_pushinteger(L, 0);
		return 1;
	}

	if (lua_isnumber(L, 2))
		asset.snapshot.fadeSeconds = static_cast<float>(lua_tonumber(L, 2));

	manager->ApplyBusSnapshot(asset.snapshot);
	lua_pushinteger(L, static_cast<lua_Integer>(asset.snapshot.buses.size()));
	return 1;
}

bool ReadLuaAudioDuckingRule(lua_State* L, int tableIndex, VansEngine::AudioDuckingRule& rule)
{
	tableIndex = lua_absindex(L, tableIndex);
	rule.triggerBusName = ReadLuaOptionalStringField(L, tableIndex, "trigger");
	if (rule.triggerBusName.empty())
		rule.triggerBusName = ReadLuaOptionalStringField(L, tableIndex, "trigger_bus");
	if (rule.triggerBusName.empty())
		rule.triggerBusName = ReadLuaOptionalStringField(L, tableIndex, "triggerBus");

	rule.targetBusName = ReadLuaOptionalStringField(L, tableIndex, "target");
	if (rule.targetBusName.empty())
		rule.targetBusName = ReadLuaOptionalStringField(L, tableIndex, "target_bus");
	if (rule.targetBusName.empty())
		rule.targetBusName = ReadLuaOptionalStringField(L, tableIndex, "targetBus");

	ReadLuaOptionalNumberField(L, tableIndex, "gain", rule.targetGain);
	ReadLuaOptionalNumberField(L, tableIndex, "target_gain", rule.targetGain);
	ReadLuaOptionalNumberField(L, tableIndex, "targetGain", rule.targetGain);
	ReadLuaOptionalNumberField(L, tableIndex, "attack", rule.attackSeconds);
	ReadLuaOptionalNumberField(L, tableIndex, "attack_seconds", rule.attackSeconds);
	ReadLuaOptionalNumberField(L, tableIndex, "attackSeconds", rule.attackSeconds);
	ReadLuaOptionalNumberField(L, tableIndex, "release", rule.releaseSeconds);
	ReadLuaOptionalNumberField(L, tableIndex, "release_seconds", rule.releaseSeconds);
	ReadLuaOptionalNumberField(L, tableIndex, "releaseSeconds", rule.releaseSeconds);
	ReadLuaOptionalBoolField(L, tableIndex, "enabled", rule.enabled);
	rule.Normalize();
	return !rule.triggerBusName.empty() && !rule.targetBusName.empty();
}

int LuaAudioAddDuckingRule(lua_State* L)
{
	auto* manager = AudioManager();
	if (!manager)
	{
		lua_pushboolean(L, false);
		return 1;
	}

	VansEngine::AudioDuckingRule rule;
	if (lua_istable(L, 1))
	{
		if (!ReadLuaAudioDuckingRule(L, 1, rule))
		{
			lua_pushboolean(L, false);
			return 1;
		}
	}
	else
	{
		rule.triggerBusName = luaL_checkstring(L, 1);
		rule.targetBusName = luaL_checkstring(L, 2);
		rule.targetGain = static_cast<float>(luaL_optnumber(L, 3, rule.targetGain));
		rule.attackSeconds = static_cast<float>(luaL_optnumber(L, 4, rule.attackSeconds));
		rule.releaseSeconds = static_cast<float>(luaL_optnumber(L, 5, rule.releaseSeconds));
		rule.Normalize();
	}

	manager->AddDuckingRule(rule);
	lua_pushboolean(L, true);
	return 1;
}

int LuaAudioClearDuckingRules(lua_State* L)
{
	auto* manager = AudioManager();
	if (manager)
		manager->ClearDuckingRules();
	lua_pushboolean(L, manager != nullptr);
	return 1;
}

int LuaAudioLoadDuckingRulesAsset(lua_State* L)
{
	auto* manager = AudioManager();
	if (!manager)
	{
		lua_pushinteger(L, 0);
		return 1;
	}

	const std::string token = luaL_checkstring(L, 1);
	const auto record = FindAudioControlAssetRecord(token);
	if (!record || record->type != Vans::VansAssetType::AudioDuckingRules)
	{
		VANS_LOG_WARN("[LuaAudio] Audio ducking rules asset not found: " << token);
		lua_pushinteger(L, 0);
		return 1;
	}

	Vans::VansAudioDuckingRulesAsset asset;
	std::string error;
	if (!Vans::VansAudioDuckingRulesAssetStorage::Load(AudioControlAssetReadPath(*record), asset, error))
	{
		VANS_LOG_WARN("[LuaAudio] Failed to load audio ducking rules asset '" << token << "': " << error);
		lua_pushinteger(L, 0);
		return 1;
	}

	const bool clearExisting = lua_isboolean(L, 2)
		? (lua_toboolean(L, 2) != 0)
		: true;
	if (clearExisting)
		manager->ClearDuckingRules();
	for (VansEngine::AudioDuckingRule rule : asset.rules)
		manager->AddDuckingRule(std::move(rule));
	lua_pushinteger(L, static_cast<lua_Integer>(asset.rules.size()));
	return 1;
}

int LuaComponentGetOcclusionEnabled(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushboolean(L, audio && audio->m_OcclusionSettings.enabled);
	return 1;
}

int LuaComponentSetOcclusionEnabled(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
	{
		audio->m_OcclusionSettings.enabled = lua_toboolean(L, 2) != 0;
		SyncRuntimeAudioComponent(audio);
	}
	return 0;
}

int LuaComponentSetOcclusionParams(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (!audio)
		return 0;

	audio->m_OcclusionSettings.blockedGain =
		static_cast<float>(luaL_optnumber(L, 2, audio->m_OcclusionSettings.blockedGain));
	audio->m_OcclusionSettings.blockedHighFrequencyGain =
		static_cast<float>(luaL_optnumber(L, 3, audio->m_OcclusionSettings.blockedHighFrequencyGain));
	audio->m_OcclusionSettings.queryIntervalSeconds =
		static_cast<float>(luaL_optnumber(L, 4, audio->m_OcclusionSettings.queryIntervalSeconds));
	audio->m_OcclusionSettings.Normalize();
	SyncRuntimeAudioComponent(audio);
	return 0;
}

int LuaComponentSetOcclusionMaterial(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (!audio)
		return 0;

	audio->m_OcclusionSettings.material =
		VansEngine::NormalizeAudioOcclusionMaterialName(luaL_optstring(L, 2, "custom"));
	audio->m_OcclusionSettings.materialThickness =
		static_cast<float>(luaL_optnumber(L, 3, audio->m_OcclusionSettings.materialThickness));
	audio->m_OcclusionSettings.Normalize();
	SyncRuntimeAudioComponent(audio);
	return 0;
}

int LuaComponentGetDopplerEnabled(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	lua_pushboolean(L, audio && audio->m_DopplerEnabled);
	return 1;
}

int LuaComponentSetDopplerEnabled(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (audio)
	{
		audio->m_DopplerEnabled = lua_toboolean(L, 2) != 0;
		if (!audio->m_DopplerEnabled)
			audio->m_Source.SetVelocity(0.0f, 0.0f, 0.0f);
		SyncRuntimeAudioComponent(audio);
	}
	return 0;
}

int LuaComponentSetCone(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* audio = dynamic_cast<VansScriptAudioComponent*>(component);
	if (!audio)
		return 0;

	audio->m_ConeSettings.enabled = lua_toboolean(L, 2) != 0;
	audio->m_ConeSettings.innerAngleDegrees =
		static_cast<float>(luaL_optnumber(L, 3, audio->m_ConeSettings.innerAngleDegrees));
	audio->m_ConeSettings.outerAngleDegrees =
		static_cast<float>(luaL_optnumber(L, 4, audio->m_ConeSettings.outerAngleDegrees));
	audio->m_ConeSettings.outerGain =
		static_cast<float>(luaL_optnumber(L, 5, audio->m_ConeSettings.outerGain));
	audio->m_ConeSettings.Normalize();
	audio->m_Source.SetCone(audio->m_ConeSettings);
	SyncRuntimeAudioComponent(audio);
	return 0;
}

int LuaComponentGetFilePath(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* audio = dynamic_cast<VansScriptAudioComponent*>(component))
		lua_pushstring(L, audio->m_Source.GetFilePath().c_str());
	else if (auto* video = dynamic_cast<VansScriptVideoComponent*>(component))
		lua_pushstring(L, video->m_VideoName.c_str());
	else if (auto* particle = dynamic_cast<VansScriptParticleComponent*>(component))
		lua_pushstring(L, particle->m_ParticleAssetPath.c_str());
	else
		lua_pushstring(L, "");
	return 1;
}

int LuaComponentGetFov(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* camera = dynamic_cast<VansScriptCameraComponent*>(component);
	lua_pushnumber(L, camera && camera->m_Camera ? camera->m_Camera->GetFov() : 0.0f);
	return 1;
}

int LuaComponentGetNearClip(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* camera = dynamic_cast<VansScriptCameraComponent*>(component);
	lua_pushnumber(L, camera && camera->m_Camera ? camera->m_Camera->GetNearClip() : 0.0f);
	return 1;
}

int LuaComponentGetFarClip(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* camera = dynamic_cast<VansScriptCameraComponent*>(component);
	lua_pushnumber(L, camera && camera->m_Camera ? camera->m_Camera->GetFarClip() : 0.0f);
	return 1;
}

int LuaComponentIsUserLookSuppressed(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* camera = dynamic_cast<VansScriptCameraComponent*>(component);
	auto* scene = Scene();
	const bool suppressed = camera && camera->m_Camera && scene &&
		scene->GetCamera() == camera->m_Camera && scene->IsUserCameraLookSuppressed();
	lua_pushboolean(L, suppressed);
	return 1;
}

int LuaComponentGetResolvedForward(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* camera = dynamic_cast<VansScriptCameraComponent*>(component);
	auto* scene = Scene();
	if (!camera || !camera->m_Camera)
	{
		PushVec3(L, glm::vec3(0.0f, 0.0f, 1.0f));
		lua_pushboolean(L, false);
		return 2;
	}

	VansGraphics::VansCameraControlPose pose = camera->m_Camera->CaptureControlPose();
	bool resolved = false;
	if (scene && scene->GetCamera() == camera->m_Camera)
		resolved = scene->CameraControlArbiter().GetLastResolvedPose(pose);
	const float yaw = glm::radians(pose.rotationDegrees.y);
	PushVec3(L, glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw)));
	lua_pushboolean(L, resolved);
	return 2;
}

int LuaComponentWorldToViewport(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* camera = dynamic_cast<VansScriptCameraComponent*>(component);
	glm::vec3 viewport(0.0f);
	const bool visible = camera && camera->m_Camera &&
		camera->m_Camera->ProjectWorldToViewport(ReadVec3(L, 2), viewport);
	PushVec3(L, viewport);
	lua_pushboolean(L, visible);
	return 2;
}

int LuaComponentSetDriveMode(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* ragdoll = dynamic_cast<VansScriptRagdollComponent*>(component))
		ragdoll->SetDriveMode(static_cast<int>(luaL_checkinteger(L, 2)));
	return 0;
}

int LuaComponentSetDriveModeWithVelocity(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* ragdoll = dynamic_cast<VansScriptRagdollComponent*>(component))
	{
		const int mode = static_cast<int>(luaL_checkinteger(L, 2));
		const glm::vec3 velocity = ReadVec3(L, 3);
		ragdoll->SetDriveModeWithVelocity(mode, velocity.x, velocity.y, velocity.z);
	}
	return 0;
}

int LuaComponentGetDriveMode(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* ragdoll = dynamic_cast<VansScriptRagdollComponent*>(component);
	lua_pushinteger(L, ragdoll ? ragdoll->GetDriveMode() : 0);
	return 1;
}

int LuaComponentSetBlendWeight(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* ragdoll = dynamic_cast<VansScriptRagdollComponent*>(component))
		ragdoll->SetBlendWeight(static_cast<float>(luaL_checknumber(L, 2)));
	return 0;
}

int LuaComponentGetBlendWeight(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* ragdoll = dynamic_cast<VansScriptRagdollComponent*>(component);
	lua_pushnumber(L, ragdoll ? ragdoll->GetBlendWeight() : 0.0f);
	return 1;
}

int LuaComponentHasRuntimeRagdoll(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* ragdoll = dynamic_cast<VansScriptRagdollComponent*>(component);
	lua_pushboolean(L, ragdoll && ragdoll->HasRuntimeRagdoll());
	return 1;
}

int LuaComponentGetRuntimeBodyCount(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* ragdoll = dynamic_cast<VansScriptRagdollComponent*>(component);
	lua_pushinteger(L, ragdoll ? ragdoll->GetRuntimeBodyCount() : 0);
	return 1;
}

int LuaComponentGetRuntimeJointCount(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* ragdoll = dynamic_cast<VansScriptRagdollComponent*>(component);
	lua_pushinteger(L, ragdoll ? ragdoll->GetRuntimeJointCount() : 0);
	return 1;
}

int LuaComponentApplyImpulse(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* ragdoll = dynamic_cast<VansScriptRagdollComponent*>(component))
	{
		const char* bone = luaL_checkstring(L, 2);
		const glm::vec3 impulse = ReadVec3(L, 3);
		ragdoll->ApplyImpulse(bone, impulse.x, impulse.y, impulse.z);
	}
	return 0;
}

template<typename LightComponent, typename LightVectorGetter>
bool GetLightIntensity(LightComponent* light, LightVectorGetter getter, float& out)
{
	if (!light || !light->m_LightManager || light->m_LightIndex < 0)
		return false;
	auto& lights = (light->m_LightManager->*getter)();
	const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
	if (index >= lights.size())
		return false;
	out = lights[index].m_Intensity;
	return true;
}

template<typename LightComponent, typename LightVectorGetter>
bool SetLightIntensity(LightComponent* light, LightVectorGetter getter, float value)
{
	if (!light || !light->m_LightManager || light->m_LightIndex < 0)
		return false;
	auto& lights = (light->m_LightManager->*getter)();
	const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
	if (index >= lights.size())
		return false;
	lights[index].m_Intensity = value;
	return true;
}

template<typename LightComponent, typename LightVectorGetter>
bool GetLightColor(LightComponent* light, LightVectorGetter getter, glm::vec3& out)
{
	if (!light || !light->m_LightManager || light->m_LightIndex < 0)
		return false;
	auto& lights = (light->m_LightManager->*getter)();
	const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
	if (index >= lights.size())
		return false;
	out = lights[index].m_Color;
	return true;
}

template<typename LightComponent, typename LightVectorGetter>
bool SetLightColor(LightComponent* light, LightVectorGetter getter, const glm::vec3& value)
{
	if (!light || !light->m_LightManager || light->m_LightIndex < 0)
		return false;
	auto& lights = (light->m_LightManager->*getter)();
	const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
	if (index >= lights.size())
		return false;
	lights[index].m_Color = value;
	return true;
}

int LuaComponentGetIntensity(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	float value = 0.0f;
	bool ok = false;
	if (auto* light = dynamic_cast<VansScriptDirectionalLightComponent*>(component))
		ok = GetLightIntensity(light, &VansGraphics::VansLightManager::GetDirectionLights, value);
	else if (auto* light = dynamic_cast<VansScriptPointLightComponent*>(component))
		ok = GetLightIntensity(light, &VansGraphics::VansLightManager::GetPointLights, value);
	else if (auto* light = dynamic_cast<VansScriptSpotLightComponent*>(component))
		ok = GetLightIntensity(light, &VansGraphics::VansLightManager::GetSpotLight, value);
	else if (auto* light = dynamic_cast<VansScriptRectLightComponent*>(component))
		ok = GetLightIntensity(light, &VansGraphics::VansLightManager::GetRectLights, value);
	lua_pushnumber(L, ok ? value : 0.0f);
	return 1;
}

int LuaComponentSetIntensity(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const float value = static_cast<float>(luaL_checknumber(L, 2));
	if (auto* light = dynamic_cast<VansScriptDirectionalLightComponent*>(component))
		SetLightIntensity(light, &VansGraphics::VansLightManager::GetDirectionLights, value);
	else if (auto* light = dynamic_cast<VansScriptPointLightComponent*>(component))
		SetLightIntensity(light, &VansGraphics::VansLightManager::GetPointLights, value);
	else if (auto* light = dynamic_cast<VansScriptSpotLightComponent*>(component))
		SetLightIntensity(light, &VansGraphics::VansLightManager::GetSpotLight, value);
	else if (auto* light = dynamic_cast<VansScriptRectLightComponent*>(component))
		SetLightIntensity(light, &VansGraphics::VansLightManager::GetRectLights, value);
	return 0;
}

int LuaComponentGetColor(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	glm::vec3 value(0.0f);
	bool ok = false;
	if (auto* light = dynamic_cast<VansScriptDirectionalLightComponent*>(component))
		ok = GetLightColor(light, &VansGraphics::VansLightManager::GetDirectionLights, value);
	else if (auto* light = dynamic_cast<VansScriptPointLightComponent*>(component))
		ok = GetLightColor(light, &VansGraphics::VansLightManager::GetPointLights, value);
	else if (auto* light = dynamic_cast<VansScriptSpotLightComponent*>(component))
		ok = GetLightColor(light, &VansGraphics::VansLightManager::GetSpotLight, value);
	else if (auto* light = dynamic_cast<VansScriptRectLightComponent*>(component))
		ok = GetLightColor(light, &VansGraphics::VansLightManager::GetRectLights, value);
	PushVec3(L, ok ? value : glm::vec3(0.0f));
	return 1;
}

int LuaComponentSetColor(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const glm::vec3 value = ReadVec3(L, 2);
	if (auto* light = dynamic_cast<VansScriptDirectionalLightComponent*>(component))
		SetLightColor(light, &VansGraphics::VansLightManager::GetDirectionLights, value);
	else if (auto* light = dynamic_cast<VansScriptPointLightComponent*>(component))
		SetLightColor(light, &VansGraphics::VansLightManager::GetPointLights, value);
	else if (auto* light = dynamic_cast<VansScriptSpotLightComponent*>(component))
		SetLightColor(light, &VansGraphics::VansLightManager::GetSpotLight, value);
	else if (auto* light = dynamic_cast<VansScriptRectLightComponent*>(component))
		SetLightColor(light, &VansGraphics::VansLightManager::GetRectLights, value);
	return 0;
}

int LuaComponentGetRadius(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	float value = 0.0f;
	if (auto* light = dynamic_cast<VansScriptPointLightComponent*>(component))
	{
		if (light->m_LightManager && light->m_LightIndex >= 0)
		{
			auto& lights = light->m_LightManager->GetPointLights();
			const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
			if (index < lights.size()) value = lights[index].m_Radius;
		}
	}
	else if (auto* light = dynamic_cast<VansScriptSpotLightComponent*>(component))
	{
		if (light->m_LightManager && light->m_LightIndex >= 0)
		{
			auto& lights = light->m_LightManager->GetSpotLight();
			const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
			if (index < lights.size()) value = lights[index].m_Radius;
		}
	}
	else if (auto* light = dynamic_cast<VansScriptRectLightComponent*>(component))
	{
		if (light->m_LightManager && light->m_LightIndex >= 0)
		{
			auto& lights = light->m_LightManager->GetRectLights();
			const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
			if (index < lights.size()) value = lights[index].m_Range;
		}
	}
	lua_pushnumber(L, value);
	return 1;
}

int LuaComponentSetRadius(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const float value = static_cast<float>(luaL_checknumber(L, 2));
	if (auto* light = dynamic_cast<VansScriptPointLightComponent*>(component))
	{
		if (light->m_LightManager && light->m_LightIndex >= 0)
		{
			auto& lights = light->m_LightManager->GetPointLights();
			const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
			if (index < lights.size()) lights[index].m_Radius = value;
		}
	}
	else if (auto* light = dynamic_cast<VansScriptSpotLightComponent*>(component))
	{
		if (light->m_LightManager && light->m_LightIndex >= 0)
		{
			auto& lights = light->m_LightManager->GetSpotLight();
			const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
			if (index < lights.size()) lights[index].m_Radius = value;
		}
	}
	else if (auto* light = dynamic_cast<VansScriptRectLightComponent*>(component))
	{
		if (light->m_LightManager && light->m_LightIndex >= 0)
		{
			auto& lights = light->m_LightManager->GetRectLights();
			const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
			if (index < lights.size()) lights[index].m_Range = value;
		}
	}
	return 0;
}

int LuaComponentGetInnerCutoff(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	float value = 0.0f;
	if (auto* light = dynamic_cast<VansScriptSpotLightComponent*>(component))
	{
		if (light->m_LightManager && light->m_LightIndex >= 0)
		{
			auto& lights = light->m_LightManager->GetSpotLight();
			const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
			if (index < lights.size()) value = lights[index].m_InnerCutOff;
		}
	}
	lua_pushnumber(L, value);
	return 1;
}

int LuaComponentSetInnerCutoff(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const float value = static_cast<float>(luaL_checknumber(L, 2));
	if (auto* light = dynamic_cast<VansScriptSpotLightComponent*>(component))
	{
		if (light->m_LightManager && light->m_LightIndex >= 0)
		{
			auto& lights = light->m_LightManager->GetSpotLight();
			const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
			if (index < lights.size()) lights[index].m_InnerCutOff = value;
		}
	}
	return 0;
}

int LuaComponentGetOuterCutoff(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	float value = 0.0f;
	if (auto* light = dynamic_cast<VansScriptSpotLightComponent*>(component))
	{
		if (light->m_LightManager && light->m_LightIndex >= 0)
		{
			auto& lights = light->m_LightManager->GetSpotLight();
			const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
			if (index < lights.size()) value = lights[index].m_OuterCutOff;
		}
	}
	lua_pushnumber(L, value);
	return 1;
}

int LuaComponentSetOuterCutoff(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const float value = static_cast<float>(luaL_checknumber(L, 2));
	if (auto* light = dynamic_cast<VansScriptSpotLightComponent*>(component))
	{
		if (light->m_LightManager && light->m_LightIndex >= 0)
		{
			auto& lights = light->m_LightManager->GetSpotLight();
			const std::size_t index = static_cast<std::size_t>(light->m_LightIndex);
			if (index < lights.size()) lights[index].m_OuterCutOff = value;
		}
	}
	return 0;
}

int LuaComponentSetFov(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* camera = dynamic_cast<VansScriptCameraComponent*>(component);
	if (camera && camera->m_Camera)
		camera->m_Camera->SetFov(static_cast<float>(luaL_checknumber(L, 2)));
	return 0;
}

int LuaComponentSetNearClip(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* camera = dynamic_cast<VansScriptCameraComponent*>(component);
	if (camera && camera->m_Camera)
		camera->m_Camera->SetNearClip(static_cast<float>(luaL_checknumber(L, 2)));
	return 0;
}

int LuaComponentSetFarClip(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* camera = dynamic_cast<VansScriptCameraComponent*>(component);
	if (camera && camera->m_Camera)
		camera->m_Camera->SetFarClip(static_cast<float>(luaL_checknumber(L, 2)));
	return 0;
}

int LuaComponentGetName(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	lua_pushstring(L, component ? component->m_ComponentName.c_str() : "");
	return 1;
}

int LuaComponentLoadParticle(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const char* path = luaL_checkstring(L, 2);
	auto* particle = dynamic_cast<VansScriptParticleComponent*>(component);
	lua_pushboolean(L, particle ? particle->LoadAsset(path) : false);
	return 1;
}

int LuaComponentSetWorldPosition(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* particle = dynamic_cast<VansScriptParticleComponent*>(component);
	if (particle)
	{
		particle->SetWorldPosition(
			static_cast<float>(luaL_checknumber(L, 2)),
			static_cast<float>(luaL_checknumber(L, 3)),
			static_cast<float>(luaL_checknumber(L, 4)));
	}
	return 0;
}

int LuaComponentClearWorldPosition(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* particle = dynamic_cast<VansScriptParticleComponent*>(component))
		particle->ClearWorldPositionOverride();
	return 0;
}

int LuaComponentGetPlayTime(lua_State* L)
{
	auto* handle = CheckComponent(L, 1);
	auto* component = handle->component;
	if (auto* particle = dynamic_cast<VansScriptParticleComponent*>(component))
	{
		if (auto* runtimeParticle = RuntimeParticleComponent(handle))
			lua_pushnumber(L, runtimeParticle->playTime);
		else
			lua_pushnumber(L, particle->m_PlayTime);
	}
	else
		lua_pushnumber(L, 0.0f);
	return 1;
}

int LuaComponentGetAssetPath(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	if (auto* particle = dynamic_cast<VansScriptParticleComponent*>(component))
		lua_pushstring(L, particle->m_ParticleAssetPath.c_str());
	else
		lua_pushstring(L, "");
	return 1;
}

int LuaComponentSetAssetPath(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	const char* path = luaL_checkstring(L, 2);
	auto* particle = dynamic_cast<VansScriptParticleComponent*>(component);
	lua_pushboolean(L, particle ? particle->LoadAsset(path) : false);
	return 1;
}

VansGraphics::VansParticleEmitter* GetParticleEmitterAt(VansScriptParticleComponent* particle, lua_Integer index)
{
	if (!particle || !particle->m_ParticleAsset || index < 0)
		return nullptr;
	const auto& emitters = particle->m_ParticleAsset->m_Emitters;
	if (static_cast<std::size_t>(index) >= emitters.size())
		return nullptr;
	return emitters[static_cast<std::size_t>(index)].get();
}

int LuaComponentGetEmitterCount(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* particle = dynamic_cast<VansScriptParticleComponent*>(component);
	lua_pushinteger(L, particle && particle->m_ParticleAsset
		? static_cast<lua_Integer>(particle->m_ParticleAsset->m_Emitters.size())
		: 0);
	return 1;
}

int LuaComponentGetEmitterName(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* particle = dynamic_cast<VansScriptParticleComponent*>(component);
	auto* emitter = GetParticleEmitterAt(particle, luaL_checkinteger(L, 2));
	lua_pushstring(L, emitter ? emitter->m_Name.c_str() : "");
	return 1;
}

int LuaComponentGetAliveCount(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* particle = dynamic_cast<VansScriptParticleComponent*>(component);
	if (lua_gettop(L) >= 2)
	{
		auto* emitter = GetParticleEmitterAt(particle, luaL_checkinteger(L, 2));
		lua_pushinteger(L, emitter ? static_cast<lua_Integer>(emitter->m_ParticlePool.m_AliveCount) : 0);
		return 1;
	}
	lua_pushinteger(L, particle && particle->m_Runtime
		? static_cast<lua_Integer>(particle->m_Runtime->m_AliveInstanceCount.load(std::memory_order_acquire))
		: 0);
	return 1;
}

int LuaComponentGetMaxCount(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* particle = dynamic_cast<VansScriptParticleComponent*>(component);
	if (lua_gettop(L) >= 2)
	{
		auto* emitter = GetParticleEmitterAt(particle, luaL_checkinteger(L, 2));
		lua_pushinteger(L, emitter ? static_cast<lua_Integer>(emitter->m_MaxParticles) : 0);
		return 1;
	}
	lua_Integer total = 0;
	if (particle && particle->m_ParticleAsset)
	{
		for (const auto& emitter : particle->m_ParticleAsset->m_Emitters)
			if (emitter) total += static_cast<lua_Integer>(emitter->m_MaxParticles);
	}
	lua_pushinteger(L, total);
	return 1;
}

int LuaComponentIsEmitterEnabled(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* particle = dynamic_cast<VansScriptParticleComponent*>(component);
	auto* emitter = GetParticleEmitterAt(particle, luaL_checkinteger(L, 2));
	lua_pushboolean(L, emitter && emitter->m_Enabled);
	return 1;
}

int LuaComponentSetEmitterEnabled(lua_State* L)
{
	auto* component = CheckComponent(L, 1)->component;
	auto* particle = dynamic_cast<VansScriptParticleComponent*>(component);
	auto* emitter = GetParticleEmitterAt(particle, luaL_checkinteger(L, 2));
	if (emitter)
		emitter->m_Enabled = lua_toboolean(L, 3) != 0;
	return 0;
}

int LuaSelfGetObject(lua_State* L)
{
	lua_getfield(L, 1, "__owner");
	return 1;
}

int LuaSelfGetTransform(lua_State* L)
{
	lua_getfield(L, 1, "__owner");
	if (!lua_isuserdata(L, -1))
		return 1;
	auto* object = ResolveObject(CheckObject(L, -1));
	lua_pop(L, 1);
	if (!object)
		lua_pushnil(L);
	else
		PushTransform(L, object->m_TransformID);
	return 1;
}

template<typename T>
int LuaSelfGetOwnerComponent(lua_State* L)
{
	lua_getfield(L, 1, "__owner");
	if (!lua_isuserdata(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	auto* object = ResolveObject(CheckObject(L, -1));
	lua_pop(L, 1);
	PushComponent(L, FindRuntimeBackedComponent<T>(object));
	return 1;
}

int LuaFindObject(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);
	auto* scene = Scene();
	PushObject(L, scene ? scene->FindObjectByName(name) : nullptr);
	return 1;
}

int LuaLog(lua_State* L)
{
	int top = lua_gettop(L);
	std::string message;
	for (int i = 1; i <= top; ++i)
	{
		size_t len = 0;
		const char* text = luaL_tolstring(L, i, &len);
		if (i > 1)
			message += "\t";
		message.append(text, len);
		lua_pop(L, 1);
	}
	VANS_LOG("[Lua] " << message);
	return 0;
}

int LuaTimeSeconds(lua_State* L)
{
	lua_pushnumber(L, VansGraphics::VansTimer::GetFrameTime());
	return 1;
}

int LuaTimelinePlay(lua_State* L)
{
	const char* componentGuid = luaL_checkstring(L, 1);
	const bool restart = lua_gettop(L) < 2 || lua_toboolean(L, 2) != 0;
	auto* scene = Scene();
	lua_pushboolean(L, scene && scene->PlayRuntimeTimeline(componentGuid, restart));
	return 1;
}

int LuaTimelinePause(lua_State* L)
{
	const char* componentGuid = luaL_checkstring(L, 1);
	auto* scene = Scene();
	lua_pushboolean(L, scene && scene->PauseRuntimeTimeline(componentGuid));
	return 1;
}

int LuaTimelineResume(lua_State* L)
{
	const char* componentGuid = luaL_checkstring(L, 1);
	auto* scene = Scene();
	lua_pushboolean(L, scene && scene->ResumeRuntimeTimeline(componentGuid));
	return 1;
}

int LuaTimelineStop(lua_State* L)
{
	const char* componentGuid = luaL_checkstring(L, 1);
	auto* scene = Scene();
	lua_pushboolean(L, scene && scene->StopRuntimeTimeline(componentGuid));
	return 1;
}

int LuaTimelineState(lua_State* L)
{
	const char* componentGuid = luaL_checkstring(L, 1);
	std::string state;
	std::int64_t tick = 0;
	auto* scene = Scene();
	if (!scene || !scene->GetRuntimeTimelineState(componentGuid, state, tick))
	{
		lua_pushnil(L);
		lua_pushinteger(L, 0);
		return 2;
	}
	lua_pushstring(L, state.c_str());
	lua_pushinteger(L, static_cast<lua_Integer>(tick));
	return 2;
}

int KeyFromLua(lua_State* L, int index)
{
	if (lua_isinteger(L, index))
		return static_cast<int>(lua_tointeger(L, index));

	std::string key = luaL_checkstring(L, index);
	std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
		return static_cast<char>(std::toupper(ch));
	});
	if (key.size() == 1)
	{
		const unsigned char ch = static_cast<unsigned char>(key[0]);
		if (std::isalpha(ch))
			return GLFW_KEY_A + (std::toupper(ch) - 'A');
		if (std::isdigit(ch))
			return GLFW_KEY_0 + (ch - '0');
	}
	if (key == "W") return GLFW_KEY_W;
	if (key == "A") return GLFW_KEY_A;
	if (key == "S") return GLFW_KEY_S;
	if (key == "D") return GLFW_KEY_D;
	if (key == "UP") return GLFW_KEY_UP;
	if (key == "DOWN") return GLFW_KEY_DOWN;
	if (key == "LEFT") return GLFW_KEY_LEFT;
	if (key == "RIGHT") return GLFW_KEY_RIGHT;
	if (key == "ESC" || key == "ESCAPE") return GLFW_KEY_ESCAPE;
	if (key == "ENTER" || key == "RETURN") return GLFW_KEY_ENTER;
	if (key == "TAB") return GLFW_KEY_TAB;
	if (key == "BACKSPACE") return GLFW_KEY_BACKSPACE;
	if (key == "SPACE") return GLFW_KEY_SPACE;
	if (key == "SHIFT") return GLFW_KEY_LEFT_SHIFT;
	if (key == "LEFT_SHIFT") return GLFW_KEY_LEFT_SHIFT;
	if (key == "RIGHT_SHIFT") return GLFW_KEY_RIGHT_SHIFT;
	if (key == "LEFT_ALT") return GLFW_KEY_LEFT_ALT;
	if (key == "RIGHT_ALT") return GLFW_KEY_RIGHT_ALT;
	if (key == "CTRL" || key == "CONTROL") return GLFW_KEY_LEFT_CONTROL;
	if (key == "LEFT_CONTROL") return GLFW_KEY_LEFT_CONTROL;
	if (key == "RIGHT_CONTROL") return GLFW_KEY_RIGHT_CONTROL;
	if (key == "E") return GLFW_KEY_E;
	if (key == "Q") return GLFW_KEY_Q;
	if (key == "M") return GLFW_KEY_M;
	if (key == "R") return GLFW_KEY_R;
	if (key == "F") return GLFW_KEY_F;
	return GLFW_KEY_UNKNOWN;
}

Vans::MouseButton MouseButtonFromLua(lua_State* L, int index)
{
	if (lua_isinteger(L, index))
		return static_cast<Vans::MouseButton>(static_cast<int>(lua_tointeger(L, index)));

	const std::string button = luaL_checkstring(L, index);
	if (button == "LEFT") return Vans::MouseButton::Left;
	if (button == "RIGHT") return Vans::MouseButton::Right;
	if (button == "MIDDLE") return Vans::MouseButton::Middle;
	if (button == "BUTTON4") return Vans::MouseButton::Button4;
	if (button == "BUTTON5") return Vans::MouseButton::Button5;
	if (button == "BUTTON6") return Vans::MouseButton::Button6;
	if (button == "BUTTON7") return Vans::MouseButton::Button7;
	if (button == "BUTTON8") return Vans::MouseButton::Button8;
	return Vans::MouseButton::Left;
}

void PushPhysicsHit(lua_State* L, const physx::PxRaycastHit& hit)
{
	lua_createtable(L, 0, 8);
	lua_pushboolean(L, true);
	lua_setfield(L, -2, "hit");
	lua_pushnumber(L, hit.distance);
	lua_setfield(L, -2, "distance");
	PushVec3(L, glm::vec3(hit.position.x, hit.position.y, hit.position.z));
	lua_setfield(L, -2, "position");
	PushVec3(L, glm::vec3(hit.normal.x, hit.normal.y, hit.normal.z));
	lua_setfield(L, -2, "normal");

	auto* node = hit.actor ? static_cast<VansEngine::VansPhysicsNode*>(hit.actor->userData) : nullptr;
	lua_pushstring(L, node ? node->GetName().c_str() : (hit.actor && hit.actor->getName() ? hit.actor->getName() : ""));
	lua_setfield(L, -2, "object_name");
	const int transformID = node ? static_cast<int>(node->GetTransformID()) : -1;
	lua_pushinteger(L, transformID);
	lua_setfield(L, -2, "transform_id");
	PushObject(L, transformID >= 0 ? FindObjectByTransformID(static_cast<std::uint32_t>(transformID)) : nullptr);
	lua_setfield(L, -2, "object");
}

void PushOverlapHit(lua_State* L, const physx::PxOverlapHit& hit)
{
	lua_createtable(L, 0, 4);
	auto* node = hit.actor ? static_cast<VansEngine::VansPhysicsNode*>(hit.actor->userData) : nullptr;
	lua_pushstring(L, node ? node->GetName().c_str() : (hit.actor && hit.actor->getName() ? hit.actor->getName() : ""));
	lua_setfield(L, -2, "object_name");
	const int transformID = node ? static_cast<int>(node->GetTransformID()) : -1;
	lua_pushinteger(L, transformID);
	lua_setfield(L, -2, "transform_id");
	PushObject(L, transformID >= 0 ? FindObjectByTransformID(static_cast<std::uint32_t>(transformID)) : nullptr);
	lua_setfield(L, -2, "object");
	lua_pushboolean(L, hit.shape != nullptr);
	lua_setfield(L, -2, "has_shape");
}

int LuaPhysicsRaycast(lua_State* L)
{
	const glm::vec3 origin = ReadVec3(L, 1);
	const int directionIndex = lua_istable(L, 1) ? 2 : 4;
	glm::vec3 direction = ReadVec3(L, directionIndex);
	const float maxDistance = static_cast<float>(
		luaL_optnumber(L, lua_istable(L, directionIndex) ? directionIndex + 1 : directionIndex + 3, 10000.0));
	const float length = glm::length(direction);
	if (length <= 0.0001f)
	{
		lua_pushnil(L);
		return 1;
	}
	direction /= length;

	auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
	auto* scene = physics.GetScene();
	if (!scene)
	{
		lua_pushnil(L);
		return 1;
	}

	std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
	physx::PxSceneReadLock scopedReadLock(*scene);
	physx::PxRaycastBuffer hit;
	const bool ok = scene->raycast(
		physx::PxVec3(origin.x, origin.y, origin.z),
		physx::PxVec3(direction.x, direction.y, direction.z),
		maxDistance,
		hit);
	if (!ok || !hit.hasBlock)
	{
		lua_pushnil(L);
		return 1;
	}
	PushPhysicsHit(L, hit.block);
	return 1;
}

int LuaPhysicsRaycastAll(lua_State* L)
{
	const glm::vec3 origin = ReadVec3(L, 1);
	const int directionIndex = lua_istable(L, 1) ? 2 : 4;
	glm::vec3 direction = ReadVec3(L, directionIndex);
	const float maxDistance = static_cast<float>(
		luaL_optnumber(L, lua_istable(L, directionIndex) ? directionIndex + 1 : directionIndex + 3, 10000.0));
	const float length = glm::length(direction);
	if (length <= 0.0001f)
	{
		lua_newtable(L);
		return 1;
	}
	direction /= length;

	auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
	auto* scene = physics.GetScene();
	if (!scene)
	{
		lua_newtable(L);
		return 1;
	}

	std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
	physx::PxSceneReadLock scopedReadLock(*scene);
	physx::PxRaycastHit hits[64];
	physx::PxRaycastBuffer buffer(hits, 64);
	scene->raycast(
		physx::PxVec3(origin.x, origin.y, origin.z),
		physx::PxVec3(direction.x, direction.y, direction.z),
		maxDistance,
		buffer);
	lua_createtable(L, static_cast<int>(buffer.nbTouches) + (buffer.hasBlock ? 1 : 0), 0);
	int outIndex = 1;
	for (physx::PxU32 i = 0; i < buffer.nbTouches; ++i)
	{
		PushPhysicsHit(L, buffer.touches[i]);
		lua_rawseti(L, -2, outIndex++);
	}
	if (buffer.hasBlock)
	{
		PushPhysicsHit(L, buffer.block);
		lua_rawseti(L, -2, outIndex++);
	}
	return 1;
}

int LuaPhysicsOverlapSphere(lua_State* L)
{
	const glm::vec3 center = ReadVec3(L, 1);
	const int radiusIndex = lua_istable(L, 1) ? 2 : 4;
	const float radius = static_cast<float>(luaL_checknumber(L, radiusIndex));

	auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
	auto* scene = physics.GetScene();
	if (!scene || radius <= 0.0f)
	{
		lua_newtable(L);
		return 1;
	}

	std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
	physx::PxSceneReadLock scopedReadLock(*scene);
	physx::PxOverlapHit hits[64];
	physx::PxOverlapBuffer buffer(hits, 64);
	scene->overlap(
		physx::PxSphereGeometry(radius),
		physx::PxTransform(physx::PxVec3(center.x, center.y, center.z)),
		buffer);
	lua_createtable(L, static_cast<int>(buffer.nbTouches) + (buffer.hasBlock ? 1 : 0), 0);
	int outIndex = 1;
	for (physx::PxU32 i = 0; i < buffer.nbTouches; ++i)
	{
		PushOverlapHit(L, buffer.touches[i]);
		lua_rawseti(L, -2, outIndex++);
	}
	if (buffer.hasBlock)
	{
		PushOverlapHit(L, buffer.block);
		lua_rawseti(L, -2, outIndex++);
	}
	return 1;
}

int LuaInputIsKeyDown(lua_State* L)
{
	lua_pushboolean(L, Vans::VansInputManager::Get().IsKeyDown(KeyFromLua(L, 1)));
	return 1;
}

int LuaInputIsKeyPressed(lua_State* L)
{
	lua_pushboolean(L, Vans::VansInputManager::Get().IsKeyPressed(KeyFromLua(L, 1)));
	return 1;
}

int LuaInputIsKeyReleased(lua_State* L)
{
	lua_pushboolean(L, Vans::VansInputManager::Get().IsKeyReleased(KeyFromLua(L, 1)));
	return 1;
}

int LuaInputGetMouseDelta(lua_State* L)
{
	double x = 0.0;
	double y = 0.0;
	Vans::VansInputManager::Get().GetMouseDelta(x, y);
	lua_pushnumber(L, x);
	lua_pushnumber(L, y);
	return 2;
}

int LuaInputGetMousePosition(lua_State* L)
{
	double x = 0.0;
	double y = 0.0;
	Vans::VansInputManager::Get().GetMousePosition(x, y);
	lua_pushnumber(L, x);
	lua_pushnumber(L, y);
	return 2;
}

int LuaInputGetUIMousePosition(lua_State* L)
{
	double rawX = 0.0;
	double rawY = 0.0;
	Vans::VansInputManager::Get().GetMousePosition(rawX, rawY);

	double uiX = rawX;
	double uiY = rawY;
	VansRuntime::VansUISystem::Get().TransformMouseToView(rawX, rawY, uiX, uiY);

	lua_pushnumber(L, uiX);
	lua_pushnumber(L, uiY);
	return 2;
}

int LuaInputGetWindowSize(lua_State* L)
{
	int width = 0;
	int height = 0;
	if (GLFWwindow* window = Vans::VansInputManager::Get().GetWindow())
		glfwGetWindowSize(window, &width, &height);
	lua_pushinteger(L, width);
	lua_pushinteger(L, height);
	return 2;
}

int LuaInputGetUIViewSize(lua_State* L)
{
	double width = 0.0;
	double height = 0.0;
	if (!VansRuntime::VansUISystem::Get().GetViewSize(width, height))
	{
		if (GLFWwindow* window = Vans::VansInputManager::Get().GetWindow())
		{
			int windowWidth = 0;
			int windowHeight = 0;
			glfwGetWindowSize(window, &windowWidth, &windowHeight);
			width = static_cast<double>(windowWidth);
			height = static_cast<double>(windowHeight);
		}
	}

	lua_pushnumber(L, width);
	lua_pushnumber(L, height);
	return 2;
}

int LuaInputIsMouseButtonDown(lua_State* L)
{
	lua_pushboolean(L, Vans::VansInputManager::Get().IsMouseButtonDown(MouseButtonFromLua(L, 1)));
	return 1;
}

int LuaInputIsMouseButtonPressed(lua_State* L)
{
	lua_pushboolean(L, Vans::VansInputManager::Get().IsMouseButtonPressed(MouseButtonFromLua(L, 1)));
	return 1;
}

int LuaInputIsMouseButtonReleased(lua_State* L)
{
	lua_pushboolean(L, Vans::VansInputManager::Get().IsMouseButtonReleased(MouseButtonFromLua(L, 1)));
	return 1;
}

int LuaInputSetMouseCapture(lua_State* L)
{
	Vans::VansInputManager::Get().SetCursorCaptureEnabled(lua_toboolean(L, 1) != 0);
	return 0;
}

int LuaInputSetCursorMode(lua_State* L)
{
	const char* modeText = luaL_checkstring(L, 1);
	std::string mode = modeText ? modeText : "";
	std::transform(mode.begin(), mode.end(), mode.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (mode == "game" || mode == "captured" || mode == "capture" || mode == "locked" || mode == "lock")
	{
		Vans::VansInputManager::Get().SetCursorCaptureEnabled(true);
		return 0;
	}
	if (mode == "ui" || mode == "menu" || mode == "normal" || mode == "visible" || mode == "free")
	{
		Vans::VansInputManager::Get().SetCursorCaptureEnabled(false);
		return 0;
	}

	return luaL_error(L, "unknown cursor mode '%s'", modeText);
}

int LuaInputIsMouseCaptured(lua_State* L)
{
	lua_pushboolean(L, Vans::VansInputManager::Get().IsCursorCaptureEnabled());
	return 1;
}

void RegisterMethods(lua_State* L, const char* metatableName, const luaL_Reg* methods)
{
	luaL_newmetatable(L, metatableName);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_setfuncs(L, methods, 0);
	lua_pop(L, 1);
}

bool ProtectedCall(lua_State* L, int argCount, int resultCount, std::string& error)
{
	if (lua_pcall(L, argCount, resultCount, 0) != LUA_OK)
	{
		error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown Lua error";
		lua_pop(L, 1);
		return false;
	}
	return true;
}

VansScriptComponent* FindComponentByReference(
	VansScriptObject* object,
	const VansScriptSerializedObjectReference& reference)
{
	if (!object)
		return nullptr;

	const std::string expectedType = Vans::CanonicalRuntimeComponentKeyForName(reference.componentType);
	for (auto* component : object->m_Components)
	{
		if (!component)
			continue;
		if (!reference.componentGuid.empty() && component->m_ComponentGuid == reference.componentGuid)
			return component;
		if (!expectedType.empty() &&
			Vans::CanonicalRuntimeComponentKeyForName(component->m_ComponentName) == expectedType)
		{
			return component;
		}
	}
	return nullptr;
}

bool PushResolvedObjectReference(lua_State* L, const VansScriptSerializedObjectReference& reference)
{
	if (reference.domain != "SceneEntity" && reference.domain != "SceneComponent")
		return false;

	auto* scene = Scene();
	if (!scene)
		return false;

	const std::string entityGuid = !reference.entityGuid.empty()
		? reference.entityGuid
		: reference.guid;
	VansScriptObject* object = !entityGuid.empty() ? scene->FindObjectByGuid(entityGuid) : nullptr;
	if (reference.domain == "SceneEntity")
	{
		PushObject(L, object);
		return true;
	}

	PushComponent(L, FindComponentByReference(object, reference));
	return true;
}

void PushRawObjectReference(lua_State* L, const VansScriptSerializedObjectReference& reference)
{
	lua_createtable(L, 0, 6);
	lua_pushstring(L, reference.domain.c_str());
	lua_setfield(L, -2, "domain");
	lua_pushstring(L, reference.guid.c_str());
	lua_setfield(L, -2, "guid");
	lua_pushstring(L, reference.assetType.c_str());
	lua_setfield(L, -2, "asset_type");
	lua_pushstring(L, reference.entityGuid.c_str());
	lua_setfield(L, -2, "entity_guid");
	lua_pushstring(L, reference.componentGuid.c_str());
	lua_setfield(L, -2, "component_guid");
	lua_pushstring(L, reference.componentType.c_str());
	lua_setfield(L, -2, "component_type");
}

void PushSerializedField(lua_State* L, const VansScriptSerializedFieldValue& field)
{
	switch (field.type)
	{
	case VansScriptSerializedFieldType::Bool:
		lua_pushboolean(L, field.boolValue);
		break;
	case VansScriptSerializedFieldType::Int:
		lua_pushinteger(L, static_cast<lua_Integer>(field.intValue));
		break;
	case VansScriptSerializedFieldType::Float:
		lua_pushnumber(L, field.floatValue);
		break;
	case VansScriptSerializedFieldType::String:
		lua_pushstring(L, field.stringValue.c_str());
		break;
	case VansScriptSerializedFieldType::ObjectReference:
		if (!PushResolvedObjectReference(L, field.objectReference))
			PushRawObjectReference(L, field.objectReference);
		break;
	default:
		lua_pushnil(L);
		break;
	}
}

void PushPhysicsEvent(lua_State* L, const VansScriptPhysicsEventInfo& info)
{
	lua_createtable(L, 0, 5);
	lua_pushstring(L, info.otherName.c_str());
	lua_setfield(L, -2, "other_name");
	lua_pushinteger(L, info.otherTransformID);
	lua_setfield(L, -2, "other_transform_id");
	lua_pushnumber(L, info.impulse);
	lua_setfield(L, -2, "impulse");
	lua_createtable(L, 0, 3);
	lua_pushnumber(L, info.contactPoint[0]); lua_setfield(L, -2, "x");
	lua_pushnumber(L, info.contactPoint[1]); lua_setfield(L, -2, "y");
	lua_pushnumber(L, info.contactPoint[2]); lua_setfield(L, -2, "z");
	lua_setfield(L, -2, "contact_point");
	lua_createtable(L, 0, 3);
	lua_pushnumber(L, info.contactNormal[0]); lua_setfield(L, -2, "x");
	lua_pushnumber(L, info.contactNormal[1]); lua_setfield(L, -2, "y");
	lua_pushnumber(L, info.contactNormal[2]); lua_setfield(L, -2, "z");
	lua_setfield(L, -2, "contact_normal");
}
}

void VansScriptComponent::SetEnabled(bool enabled)
{
	if (m_Destroyed) return;
	if (m_Enabled == enabled) return;
	m_Enabled = enabled;
	RefreshEffectiveEnabled();
}

void VansScriptComponent::MirrorRuntimeEnabledState(bool selfEnabled, bool effectiveEnabled)
{
	if (m_Destroyed) return;
	m_Enabled = selfEnabled;
	m_EffectiveEnabled = effectiveEnabled;
}

void VansScriptComponent::ApplyOwnerActive(bool active)
{
	if (m_Destroyed) return;
	m_OwnerActive = active;
	RefreshEffectiveEnabled();
}

void VansScriptComponent::Destroy()
{
	if (m_Destroyed) return;
	if (m_EffectiveEnabled)
	{
		OnDisable();
		m_EffectiveEnabled = false;
	}
	m_Enabled = false;
	OnDestroy();
	m_Destroyed = true;
}

void VansScriptComponent::RefreshEffectiveEnabled()
{
	const bool effective = m_Enabled && m_OwnerActive;
	if (m_EffectiveEnabled == effective)
		return;
	m_EffectiveEnabled = effective;
	if (effective) OnEnable(); else OnDisable();
}

void VansScriptObject::SetActive(bool active)
{
	if (m_Active == active) return;
	m_Active = active;
	for (auto* comp : m_Components)
		if (comp) comp->ApplyOwnerActive(active);
}

void VansScriptObject::AddComponent(VansScriptComponent* comp)
{
	if (comp)
	{
		comp->ApplyOwnerActive(m_Active);
		m_Components.push_back(comp);
	}
}

std::uint32_t VansScriptObject::ReleaseOwnedTransform()
{
	if (!m_OwnsTransform)
		return UINT32_MAX;
	m_OwnsTransform = false;
	return m_TransformID;
}

VansScriptObject::~VansScriptObject()
{
	for (auto* component : m_Components)
	{
		if (component)
			component->Destroy();
		delete component;
	}
	m_Components.clear();
	if (m_OwnsTransform)
		VansGraphics::VansTransformStore::FreeTransform(m_TransformID);
}

void VansScriptRenderComponent::OnEnable()
{
	if (!m_RenderNodes.empty())
	{
		for (auto* renderNode : m_RenderNodes)
			if (renderNode) renderNode->SetEnabled(true);
		return;
	}
	if (m_RenderNode) m_RenderNode->SetEnabled(true);
}

void VansScriptRenderComponent::OnDisable()
{
	if (!m_RenderNodes.empty())
	{
		for (auto* renderNode : m_RenderNodes)
			if (renderNode) renderNode->SetEnabled(false);
		return;
	}
	if (m_RenderNode) m_RenderNode->SetEnabled(false);
}

void VansScriptPhysicsComponent::OnEnable() { if (m_PhysicsNode) m_PhysicsNode->SetEnabled(true); }
void VansScriptPhysicsComponent::OnDisable() { if (m_PhysicsNode) m_PhysicsNode->SetEnabled(false); }
void VansScriptClothComponent::OnEnable() { if (m_ClothNode) m_ClothNode->SetEnabled(true); }
void VansScriptClothComponent::OnDisable() { if (m_ClothNode) m_ClothNode->SetEnabled(false); }
void VansScriptAnimationComponent::OnEnable() { if (m_AnimNode) m_AnimNode->SetEnabled(true); }
void VansScriptAnimationComponent::OnDisable() { if (m_AnimNode) m_AnimNode->SetEnabled(false); }
void VansScriptCameraComponent::OnEnable() { if (m_Camera) m_Camera->SetEnabled(true); }
void VansScriptCameraComponent::OnDisable() { if (m_Camera) m_Camera->SetEnabled(false); }
void VansScriptCameraComponent::OnDestroy()
{
	if (m_Camera) m_Camera->SetTransformID(UINT32_MAX);
}
void VansScriptAudioComponent::OnEnable()
{
	m_Source.SetEnabled(true);
	SyncRuntimeAudioComponent(this);
}
void VansScriptAudioComponent::OnDisable()
{
	m_Source.SetVelocity(0.0f, 0.0f, 0.0f);
	m_Source.SetEnabled(false);
	m_HasLastAudioPosition = false;
	SyncRuntimeAudioComponent(this);
}
void VansScriptAudioComponent::OnDestroy()
{
	m_Source.Stop();
	SyncRuntimeAudioComponent(this);
}
void VansScriptParticleComponent::OnEnable() { Play(); }
void VansScriptParticleComponent::OnDisable() { Pause(); }
void VansScriptParticleComponent::OnDestroy()
{
	if (m_Runtime)
		VansParticleManager::Instance().UnregisterRuntime(m_Runtime.get());
}

VansScriptUIComponent::~VansScriptUIComponent()
{
	CloseOpenedScreens();
	ReleasePreloaded();
}

void VansScriptUIComponent::Preload()
{
	if (!VansRuntime::VansUISystem::Get().IsInitialized())
		return;
	for (const std::string& screenPath : m_PreloadScreens)
		VansRuntime::VansUISystem::Get().PreloadScreen(screenPath);
}

void VansScriptUIComponent::ReleasePreloaded()
{
	if (!VansRuntime::VansUISystem::Get().IsInitialized())
		return;
	for (const std::string& screenPath : m_PreloadScreens)
		VansRuntime::VansUISystem::Get().ReleaseScreen(screenPath);
}

void VansScriptUIComponent::OpenConfiguredScreens()
{
	if (!VansRuntime::VansUISystem::Get().IsInitialized())
		return;

	for (const std::string& screenPath : m_AutoOpenScreens)
	{
		auto screen = VansRuntime::VansUISystem::Get().LoadScreen(screenPath);
		if (screen)
			m_OpenScreens.push_back(screen->GetHandleId());
	}
}

void VansScriptUIComponent::CloseOpenedScreens()
{
	if (!VansRuntime::VansUISystem::Get().IsInitialized())
	{
		m_OpenScreens.clear();
		return;
	}

	for (VansRuntime::VansUIHandleId screenId : m_OpenScreens)
		VansRuntime::VansUISystem::Get().CloseScreen(screenId);
	m_OpenScreens.clear();
}

void VansScriptUIComponent::MirrorRuntimeOpenScreens(const std::vector<std::uint64_t>& openScreens)
{
	m_OpenScreens.clear();
	m_OpenScreens.reserve(openScreens.size());
	for (std::uint64_t screenId : openScreens)
		m_OpenScreens.push_back(static_cast<VansRuntime::VansUIHandleId>(screenId));
}

void VansScriptUIComponent::OnEnable()
{
	Preload();
	OpenConfiguredScreens();
}

void VansScriptUIComponent::OnDisable()
{
	CloseOpenedScreens();
	ReleasePreloaded();
}

VansScriptRagdollComponent::VansScriptRagdollComponent() { m_ComponentName = "Ragdoll"; }
VansScriptCharacterControllerComponent::VansScriptCharacterControllerComponent() { m_ComponentName = "CharacterController"; }

void VansScriptRagdollComponent::OnEnable() { if (m_AnimNode) m_AnimNode->SetEnabled(true); }
void VansScriptRagdollComponent::OnDisable() { if (m_AnimNode) m_AnimNode->SetEnabled(false); }
void VansScriptCharacterControllerComponent::OnEnable() { if (m_ControllerNode) m_ControllerNode->SetEnabled(true); }
void VansScriptCharacterControllerComponent::OnDisable() { if (m_ControllerNode) m_ControllerNode->SetEnabled(false); }

void VansScriptRagdollComponent::SetDriveMode(int mode) { SetDriveModeWithVelocity(mode, 0.0f, 0.0f, 0.0f); }
void VansScriptRagdollComponent::SetDriveModeWithVelocity(int mode, float vx, float vy, float vz)
{
	if (!m_AnimNode) return;
	auto driveMode = VansEngine::RagdollDriveMode::Animation;
	if (mode == 1) driveMode = VansEngine::RagdollDriveMode::Physics;
	else if (mode == 2) driveMode = VansEngine::RagdollDriveMode::Blend;
	VansEngine::VansRagdollSystem::GetInstance().SetDriveMode(m_AnimNode, driveMode, glm::vec3(vx, vy, vz));
}
int VansScriptRagdollComponent::GetDriveMode() const
{
	if (!m_AnimNode) return 0;
	return static_cast<int>(VansEngine::VansRagdollSystem::GetInstance().GetDriveMode(m_AnimNode));
}
void VansScriptRagdollComponent::SetBlendWeight(float weight)
{
	if (m_AnimNode) VansEngine::VansRagdollSystem::GetInstance().SetBlendWeight(m_AnimNode, weight);
}
float VansScriptRagdollComponent::GetBlendWeight() const
{
	return m_AnimNode ? VansEngine::VansRagdollSystem::GetInstance().GetBlendWeight(m_AnimNode) : 0.0f;
}
bool VansScriptRagdollComponent::HasRuntimeRagdoll() const
{
	return m_AnimNode && VansEngine::VansRagdollSystem::GetInstance().HasRagdoll(m_AnimNode);
}
int VansScriptRagdollComponent::GetRuntimeBodyCount() const
{
	return m_AnimNode ? VansEngine::VansRagdollSystem::GetInstance().GetBodyCount(m_AnimNode) : 0;
}
int VansScriptRagdollComponent::GetRuntimeJointCount() const
{
	return m_AnimNode ? VansEngine::VansRagdollSystem::GetInstance().GetJointCount(m_AnimNode) : 0;
}
void VansScriptRagdollComponent::ApplyImpulse(const std::string& boneName, float ix, float iy, float iz)
{
	if (m_AnimNode) VansEngine::VansRagdollSystem::GetInstance().ApplyImpulse(m_AnimNode, boneName, glm::vec3(ix, iy, iz));
}

void VansScriptCharacterControllerComponent::BindFollowRagdoll(VansScriptRagdollComponent* ragdollComp, const std::string& rootBone)
{
	if (m_ControllerNode && ragdollComp && ragdollComp->m_AnimNode)
		m_ControllerNode->SetFollowRagdoll(ragdollComp->m_AnimNode, rootBone);
}
void VansScriptCharacterControllerComponent::ClearFollowRagdoll() { if (m_ControllerNode) m_ControllerNode->ClearFollowRagdoll(); }
bool VansScriptCharacterControllerComponent::IsFollowRagdollEnabled() const { return m_ControllerNode && m_ControllerNode->IsFollowRagdollEnabled(); }

void VansScriptDirectionalLightComponent::RebindSceneLightIndex(
	VansScriptLightIndexKind kind,
	int oldIndex,
	int newIndex)
{
	if (kind == VansScriptLightIndexKind::Directional && m_LightIndex == oldIndex)
		m_LightIndex = newIndex;
}

void VansScriptPointLightComponent::RebindSceneLightIndex(
	VansScriptLightIndexKind kind,
	int oldIndex,
	int newIndex)
{
	if (kind == VansScriptLightIndexKind::Point && m_LightIndex == oldIndex)
		m_LightIndex = newIndex;
}

void VansScriptSpotLightComponent::RebindSceneLightIndex(
	VansScriptLightIndexKind kind,
	int oldIndex,
	int newIndex)
{
	if (kind == VansScriptLightIndexKind::Spot && m_LightIndex == oldIndex)
		m_LightIndex = newIndex;
}

void VansScriptRectLightComponent::RebindSceneLightIndex(
	VansScriptLightIndexKind kind,
	int oldIndex,
	int newIndex)
{
	if (kind == VansScriptLightIndexKind::Rect && m_LightIndex == oldIndex)
		m_LightIndex = newIndex;
}

bool VansScriptAudioComponent::SwitchSource(const std::string& name)
{
	const bool switched = m_Source.SwitchSource(name);
	if (switched)
	{
		m_Source.SetVelocity(0.0f, 0.0f, 0.0f);
		m_HasLastAudioPosition = false;
		SyncRuntimeAudioComponent(this);
	}
	return switched;
}

bool VansScriptVideoComponent::SwitchSource(const std::string& name)
{
	if (!m_VideoManager) return false;
	auto* newVideo = m_VideoManager->Get(name);
	if (!newVideo) return false;
	if (m_VideoTex && m_VideoTex->IsPlaying())
		m_VideoTex->Pause();
	m_VideoTex = newVideo;
	m_VideoName = name;
	SyncRuntimeVideoComponent(this);
	return true;
}

void VansScriptVideoComponent::OnDestroy()
{
	if (m_VideoTex) m_VideoTex->Pause();
}

void VansScriptParticleComponent::Play()
{
	if (!m_Runtime) return;
	m_IsPlaying = true;
	m_Runtime->m_IsPlaying = true;
	if (auto* scene = Scene())
		scene->SyncRuntimeParticleComponentFromFacade(*this);
}

void VansScriptParticleComponent::Stop()
{
	if (!m_Runtime) return;
	m_IsPlaying = false;
	m_PlayTime = 0.0f;
	m_Runtime->m_IsPlaying = false;
	m_Runtime->m_PlayTime = 0.0f;
	m_Runtime->m_AliveInstanceCount.store(0, std::memory_order_release);
	for (auto& buffer : m_Runtime->m_InstanceBuffers)
		buffer.clear();
	if (auto* scene = Scene())
		scene->SyncRuntimeParticleComponentFromFacade(*this);
}

void VansScriptParticleComponent::Pause()
{
	if (!m_Runtime) return;
	m_IsPlaying = false;
	m_Runtime->m_IsPlaying = false;
	if (auto* scene = Scene())
		scene->SyncRuntimeParticleComponentFromFacade(*this);
}

void VansScriptParticleComponent::MirrorRuntimeEnabledState(bool selfEnabled, bool effectiveEnabled)
{
	VansScriptComponent::MirrorRuntimeEnabledState(selfEnabled, effectiveEnabled);
	m_IsPlaying = effectiveEnabled;
	if (m_Runtime)
		m_Runtime->m_IsPlaying = effectiveEnabled;
	if (auto* scene = Scene())
		scene->SyncRuntimeParticleComponentFromFacade(*this);
}

void VansScriptParticleComponent::Restart()
{
	Stop();
	Play();
}

void VansScriptParticleComponent::SetWorldPosition(float x, float y, float z)
{
	m_HasWorldPositionOverride = true;
	m_WorldPositionOverride = glm::vec3(x, y, z);
	if (auto* scene = Scene())
		scene->SyncRuntimeParticleComponentFromFacade(*this);
}

void VansScriptParticleComponent::ClearWorldPositionOverride()
{
	m_HasWorldPositionOverride = false;
	if (auto* scene = Scene())
		scene->SyncRuntimeParticleComponentFromFacade(*this);
}

bool VansScriptParticleComponent::LoadAsset(const std::string& path)
{
	auto newAsset = std::make_unique<VansGraphics::VansParticleAsset>();
	std::string error;
	if (!VansGraphics::VansParticleAssetStorage::Load(path, *newAsset, error))
		return false;
	m_ParticleAssetPath = path;
	m_ParticleAsset = std::move(newAsset);
	m_Runtime = std::make_unique<VansGraphics::VansParticleRuntime>();
	m_Runtime->m_Asset = m_ParticleAsset.get();
	if (auto* scene = Scene())
		scene->SyncRuntimeParticleComponentFromFacade(*this);
	return true;
}

void VansScriptParticleComponent::OnUpdate(float deltaTime)
{
	if (!m_Runtime || !m_IsPlaying) return;
	m_Runtime->Update(deltaTime);
	m_PlayTime = m_Runtime->m_PlayTime;
	if (auto* scene = Scene())
		scene->SyncRuntimeParticleComponentFromFacade(*this);
}

VansLuaScriptComponent::~VansLuaScriptComponent()
{
	Teardown();
	m_State = VansLuaScriptState::Destroyed;
}

void VansLuaScriptComponent::CacheCallback(lua_State* L, const char* name, int& ref)
{
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_InstanceRef);
	lua_getfield(L, -1, name);
	if (lua_isfunction(L, -1))
		ref = luaL_ref(L, LUA_REGISTRYINDEX);
	else
	{
		lua_pop(L, 1);
		ref = LUA_NOREF;
	}
	lua_pop(L, 1);
}

void VansLuaScriptComponent::ReleaseLuaRefs(lua_State* L)
{
	int* refs[] = {
		&m_ModuleRef, &m_InstanceRef, &m_OnStartRef, &m_OnEnableRef, &m_OnDisableRef, &m_UpdateRef,
		&m_CollisionEnterRef, &m_CollisionExitRef, &m_TriggerEnterRef, &m_TriggerExitRef
	};
	for (int* ref : refs)
	{
		if (*ref != LUA_NOREF && *ref != LUA_REFNIL)
			luaL_unref(L, LUA_REGISTRYINDEX, *ref);
		*ref = LUA_NOREF;
	}
}

void VansLuaScriptComponent::EnterFaultedState(const char* phase, const std::string& error)
{
	m_IsValid = false;
	m_Enabled = false;
	m_State = VansLuaScriptState::Faulted;
	VANS_LOG_ERROR("[LuaScript] Faulted " << m_ScriptPath << "::" << m_EntryName << " during " << phase << ": " << error);
}

bool VansLuaScriptComponent::Instantiate()
{
	if (m_State == VansLuaScriptState::Destroyed)
		return false;

	auto* context = VansScriptContext::GetInstance();
	lua_State* L = context ? context->GetLuaState() : nullptr;
	if (!L)
	{
		EnterFaultedState("load", "Lua runtime is not initialized");
		return false;
	}

	m_State = VansLuaScriptState::Loading;
	m_IsValid = false;
	std::filesystem::path absPath = ResolveScriptPath(m_ScriptPath);
	if (luaL_loadfile(L, absPath.string().c_str()) != LUA_OK)
	{
		std::string error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "load failed";
		lua_pop(L, 1);
		EnterFaultedState("load", error);
		return false;
	}

	std::string error;
	if (!ProtectedCall(L, 0, 1, error))
	{
		EnterFaultedState("execute", error);
		return false;
	}
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		EnterFaultedState("execute", "script must return a table");
		return false;
	}
	if (!m_EntryName.empty())
	{
		lua_getfield(L, -1, m_EntryName.c_str());
		if (lua_istable(L, -1))
			lua_remove(L, -2);
		else
			lua_pop(L, 1);
	}
	m_ModuleRef = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_newtable(L);
	const int instanceIndex = lua_absindex(L, -1);
	PushObject(L, m_OwnerObject);
	lua_setfield(L, -2, "__owner");
	lua_pushstring(L, m_ScriptPath.c_str());
	lua_setfield(L, -2, "__script_path");
	lua_getglobal(L, "__vans_script_instance_methods");
	if (lua_istable(L, -1))
	{
		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{
			lua_pushvalue(L, -2);
			lua_pushvalue(L, -2);
			lua_settable(L, instanceIndex);
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	for (const auto& [name, value] : m_SerializedFields)
	{
		PushSerializedField(L, value);
		lua_setfield(L, -2, name.c_str());
	}
	lua_newtable(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_ModuleRef);
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, -2);
	m_InstanceRef = luaL_ref(L, LUA_REGISTRYINDEX);

	CacheCallback(L, "on_start", m_OnStartRef);
	CacheCallback(L, "on_enable", m_OnEnableRef);
	CacheCallback(L, "on_disable", m_OnDisableRef);
	CacheCallback(L, "on_update", m_UpdateRef);
	if (m_UpdateRef == LUA_NOREF)
		CacheCallback(L, "update", m_UpdateRef);
	CacheCallback(L, "on_collision_enter", m_CollisionEnterRef);
	CacheCallback(L, "on_collision_exit", m_CollisionExitRef);
	CacheCallback(L, "on_trigger_enter", m_TriggerEnterRef);
	CacheCallback(L, "on_trigger_exit", m_TriggerExitRef);

	m_IsValid = true;
	m_State = VansLuaScriptState::Disabled;
	if (m_EnableRequested)
		SetEnabled(true);
	return true;
}

void VansLuaScriptComponent::Enable()
{
	m_EnableRequested = true;
	if (m_IsValid && m_State != VansLuaScriptState::Faulted && !m_Enabled)
		SetEnabled(true);
}

void VansLuaScriptComponent::Disable()
{
	m_EnableRequested = false;
	if (m_Enabled)
		SetEnabled(false);
	else if (m_State != VansLuaScriptState::Faulted && m_State != VansLuaScriptState::Destroyed)
		m_State = VansLuaScriptState::Disabled;
}

void VansLuaScriptComponent::OnEnable()
{
	if (!m_IsValid) return;
	m_State = VansLuaScriptState::Active;
	lua_State* L = VansScriptContext::GetInstance()->GetLuaState();

	if (!m_HasStarted && m_OnStartRef != LUA_NOREF)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, m_OnStartRef);
		lua_rawgeti(L, LUA_REGISTRYINDEX, m_InstanceRef);
		std::string startError;
		if (!ProtectedCall(L, 1, 0, startError))
		{
			EnterFaultedState("on_start", startError);
			return;
		}
	}
	m_HasStarted = true;

	if (m_OnEnableRef == LUA_NOREF) return;
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_OnEnableRef);
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_InstanceRef);
	std::string error;
	if (!ProtectedCall(L, 1, 0, error))
		EnterFaultedState("on_enable", error);
	else
		m_State = VansLuaScriptState::Active;
}

void VansLuaScriptComponent::OnDisable()
{
	if (!m_IsValid) return;
	if (m_OnDisableRef == LUA_NOREF)
	{
		m_State = VansLuaScriptState::Disabled;
		return;
	}
	lua_State* L = VansScriptContext::GetInstance()->GetLuaState();
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_OnDisableRef);
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_InstanceRef);
	std::string error;
	if (!ProtectedCall(L, 1, 0, error))
		EnterFaultedState("on_disable", error);
	else
		m_State = VansLuaScriptState::Disabled;
}

void VansLuaScriptComponent::CallUpdate(float deltaTime)
{
	if (!m_IsValid || m_State != VansLuaScriptState::Active || m_UpdateRef == LUA_NOREF) return;
	lua_State* L = VansScriptContext::GetInstance()->GetLuaState();
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_UpdateRef);
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_InstanceRef);
	lua_pushnumber(L, deltaTime);
	std::string error;
	if (!ProtectedCall(L, 2, 0, error))
		EnterFaultedState("update", error);
}

void VansLuaScriptComponent::Teardown()
{
	if (m_State == VansLuaScriptState::Destroyed)
		return;
	if (auto* context = VansScriptContext::GetInstance())
	{
		context->UnregisterScriptComponent(this);
		if (auto* L = context->GetLuaState())
			ReleaseLuaRefs(L);
	}
	m_IsValid = false;
	m_EnableRequested = false;
	m_Enabled = false;
	m_HasStarted = false;
	m_State = VansLuaScriptState::Unloaded;
}

void VansLuaScriptComponent::OnDestroy()
{
	Teardown();
}

void VansLuaScriptComponent::CallOnCollisionEnter(const VansScriptPhysicsEventInfo& info)
{
	if (!m_IsValid || m_State != VansLuaScriptState::Active || m_CollisionEnterRef == LUA_NOREF) return;
	lua_State* L = VansScriptContext::GetInstance()->GetLuaState();
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_CollisionEnterRef);
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_InstanceRef);
	PushPhysicsEvent(L, info);
	std::string error;
	if (!ProtectedCall(L, 2, 0, error)) EnterFaultedState("on_collision_enter", error);
}
void VansLuaScriptComponent::CallOnCollisionExit(const VansScriptPhysicsEventInfo& info)
{
	if (!m_IsValid || m_State != VansLuaScriptState::Active || m_CollisionExitRef == LUA_NOREF) return;
	lua_State* L = VansScriptContext::GetInstance()->GetLuaState();
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_CollisionExitRef);
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_InstanceRef);
	PushPhysicsEvent(L, info);
	std::string error;
	if (!ProtectedCall(L, 2, 0, error)) EnterFaultedState("on_collision_exit", error);
}
void VansLuaScriptComponent::CallOnTriggerEnter(const VansScriptPhysicsEventInfo& info)
{
	if (!m_IsValid || m_State != VansLuaScriptState::Active || m_TriggerEnterRef == LUA_NOREF) return;
	lua_State* L = VansScriptContext::GetInstance()->GetLuaState();
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_TriggerEnterRef);
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_InstanceRef);
	PushPhysicsEvent(L, info);
	std::string error;
	if (!ProtectedCall(L, 2, 0, error)) EnterFaultedState("on_trigger_enter", error);
}
void VansLuaScriptComponent::CallOnTriggerExit(const VansScriptPhysicsEventInfo& info)
{
	if (!m_IsValid || m_State != VansLuaScriptState::Active || m_TriggerExitRef == LUA_NOREF) return;
	lua_State* L = VansScriptContext::GetInstance()->GetLuaState();
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_TriggerExitRef);
	lua_rawgeti(L, LUA_REGISTRYINDEX, m_InstanceRef);
	PushPhysicsEvent(L, info);
	std::string error;
	if (!ProtectedCall(L, 2, 0, error)) EnterFaultedState("on_trigger_exit", error);
}

VansScriptContext::~VansScriptContext()
{
	ShutdownLua();
}

void VansScriptContext::AssertLuaThread() const
{
	VANS_ASSERT_MAIN_THREAD();
	assert(m_LuaThreadId == std::thread::id{} || m_LuaThreadId == std::this_thread::get_id());
}

void VansScriptContext::VansScriptSetup()
{
	VANS_ASSERT_MAIN_THREAD();
	if (m_LuaState)
	{
		RefreshActiveProjectRoot();
		InstallLuaSearchPath();
		return;
	}
	s_Instance = this;
	m_LuaThreadId = std::this_thread::get_id();

	RefreshActiveProjectRoot();

	m_LuaState = luaL_newstate();
	if (!m_LuaState)
		throw std::runtime_error("Failed to create Lua state");
	luaL_openlibs(m_LuaState);
	RegisterLuaBindings();
	InstallLuaSearchPath();
	m_EventConnections.Add(Vans::VansEventBus::Get().Subscribe<VansEngine::VansPhysicsContactEvent>(
		[this](const VansEngine::VansPhysicsContactEvent& event)
		{
			HandlePhysicsContactEvent(event);
		},
		Vans::VansEventLane::Physics,
		0,
		"VansScriptContext.PhysicsContact"));
	RebuildScriptSchedule();
	VANS_LOG("[LuaScript] Lua runtime initialized");
}

void VansScriptContext::RefreshActiveProjectRoot()
{
	auto& projectManager = Vans::VansProjectManager::Get();
	if (projectManager.IsProjectLoaded() && !projectManager.GetProjectRootPath().empty())
		m_ActiveProjectRoot = projectManager.GetProjectRootPath();
	else if (m_ActiveProjectRoot.empty())
		m_ActiveProjectRoot = VansConfigration::GetInstance()->GetProjectRootPath();
}

void VansScriptContext::SetActiveProjectRoot(const std::string& projectRoot)
{
	if (!projectRoot.empty())
		m_ActiveProjectRoot = projectRoot;
	if (m_LuaState)
		InstallLuaSearchPath();
}

void VansInstallLuaProjectSearchPath(
	lua_State* luaState,
	const std::filesystem::path& projectRoot)
{
	if (!luaState || projectRoot.empty()) return;
	lua_getglobal(luaState, "package");
	if (!lua_istable(luaState, -1))
	{
		lua_pop(luaState, 1);
		return;
	}
	lua_getfield(luaState, -1, "path");
	std::string current = lua_tostring(luaState, -1) ? lua_tostring(luaState, -1) : "";
	lua_pop(luaState, 1);
	const std::filesystem::path root = projectRoot.lexically_normal();
	const std::array<std::string, 4> projectPatterns{
		(root / "Scripts" / "?.lua").generic_string(),
		(root / "Scripts" / "?" / "init.lua").generic_string(),
		(root / "?.lua").generic_string(),
		(root / "?" / "init.lua").generic_string()
	};
	for (auto pattern = projectPatterns.rbegin(); pattern != projectPatterns.rend(); ++pattern)
		if (current.find(*pattern) == std::string::npos)
			current = *pattern + ";" + current;
	lua_pushstring(luaState, current.c_str());
	lua_setfield(luaState, -2, "path");
	lua_pop(luaState, 1);
}

void VansScriptContext::InstallLuaSearchPath()
{
	VansInstallLuaProjectSearchPath(m_LuaState, m_ActiveProjectRoot);
}

void VansScriptContext::RegisterLuaBindings()
{
	lua_State* L = m_LuaState;

	const luaL_Reg transformMethods[] = {
		{ "get_position", LuaTransformGetPosition },
		{ "getPosition", LuaTransformGetPosition },
		{ "set_position", LuaTransformSetPosition },
		{ "setPosition", LuaTransformSetPosition },
		{ "get_rotation", LuaTransformGetRotation },
		{ "getRotation", LuaTransformGetRotation },
		{ "set_rotation", LuaTransformSetRotation },
		{ "setRotation", LuaTransformSetRotation },
		{ "translate", LuaTransformTranslate },
		{ nullptr, nullptr }
	};
	RegisterMethods(L, "Vans.Transform", transformMethods);

	const luaL_Reg objectMethods[] = {
		{ "is_valid", LuaObjectIsValid },
		{ "isValid", LuaObjectIsValid },
		{ "get_name", LuaObjectGetName },
		{ "getName", LuaObjectGetName },
		{ "get_transform", LuaObjectGetTransform },
		{ "getTransform", LuaObjectGetTransform },
		{ "get_transform_id", LuaObjectGetTransformID },
		{ "getTransformID", LuaObjectGetTransformID },
		{ "get_component_count", LuaObjectGetComponentCount },
		{ "getComponentCount", LuaObjectGetComponentCount },
		{ "get_component_by_index", LuaObjectGetComponentByIndex },
		{ "getComponentByIndex", LuaObjectGetComponentByIndex },
		{ "destroy", LuaObjectDestroy },
		{ "get_render_comp", PushObjectComponent<VansScriptRenderComponent> },
		{ "getRenderComp", PushObjectComponent<VansScriptRenderComponent> },
		{ "get_anim_comp", PushObjectComponent<VansScriptAnimationComponent> },
		{ "getAnimComp", PushObjectComponent<VansScriptAnimationComponent> },
		{ "get_ragdoll_comp", PushObjectComponent<VansScriptRagdollComponent> },
		{ "getRagdollComp", PushObjectComponent<VansScriptRagdollComponent> },
		{ "get_cct_comp", PushObjectComponent<VansScriptCharacterControllerComponent> },
		{ "getCCTComp", PushObjectComponent<VansScriptCharacterControllerComponent> },
		{ "get_vehicle_comp", PushObjectComponent<VansScriptVehicleComponent> },
		{ "getVehicleComp", PushObjectComponent<VansScriptVehicleComponent> },
		{ "get_camera_comp", PushObjectComponent<VansScriptCameraComponent> },
		{ "getCameraComp", PushObjectComponent<VansScriptCameraComponent> },
		{ "get_directional_light_comp", PushObjectComponent<VansScriptDirectionalLightComponent> },
		{ "getDirectionalLightComp", PushObjectComponent<VansScriptDirectionalLightComponent> },
		{ "get_point_light_comp", PushObjectComponent<VansScriptPointLightComponent> },
		{ "getPointLightComp", PushObjectComponent<VansScriptPointLightComponent> },
		{ "get_spot_light_comp", PushObjectComponent<VansScriptSpotLightComponent> },
		{ "getSpotLightComp", PushObjectComponent<VansScriptSpotLightComponent> },
		{ "get_rect_light_comp", PushObjectComponent<VansScriptRectLightComponent> },
		{ "getRectLightComp", PushObjectComponent<VansScriptRectLightComponent> },
		{ "get_audio_comp", PushObjectComponent<VansScriptAudioComponent> },
		{ "getAudioComp", PushObjectComponent<VansScriptAudioComponent> },
		{ "get_video_comp", PushObjectComponent<VansScriptVideoComponent> },
		{ "getVideoComp", PushObjectComponent<VansScriptVideoComponent> },
		{ "get_particle_comp", PushObjectComponent<VansScriptParticleComponent> },
		{ "getParticleComp", PushObjectComponent<VansScriptParticleComponent> },
		{ "get_ui_comp", PushObjectComponent<VansScriptUIComponent> },
		{ "getUIComp", PushObjectComponent<VansScriptUIComponent> },
		{ nullptr, nullptr }
	};
	RegisterMethods(L, "Vans.Object", objectMethods);

	const luaL_Reg componentMethods[] = {
		{ "is_valid", LuaComponentIsValid },
		{ "is_bound", LuaComponentIsValid },
		{ "isValid", LuaComponentIsValid },
		{ "isBound", LuaComponentIsValid },
		{ "set_enabled", LuaComponentSetEnabled },
		{ "is_enabled", LuaComponentIsEnabled },
		{ "get_name", LuaComponentGetName },
		{ "play", LuaComponentPlay },
		{ "pause", LuaComponentPause },
		{ "stop", LuaComponentStop },
		{ "resume", LuaComponentResume },
		{ "restart", LuaComponentRestart },
		{ "open_screen", LuaComponentUIOpenScreen },
		{ "close_all_owned", LuaComponentUICloseAllOwned },
		{ "preload_screen", LuaComponentUIPreload },
		{ "release_preloaded", LuaComponentUIReleasePreloaded },
		{ "switch_source", LuaComponentSwitchSource },
		{ "is_playing", LuaComponentIsPlaying },
		{ "is_paused", LuaComponentIsPaused },
		{ "get_volume", LuaComponentGetVolume },
		{ "set_volume", LuaComponentSetVolume },
		{ "get_pitch", LuaComponentGetPitch },
		{ "set_pitch", LuaComponentSetPitch },
		{ "get_loop", LuaComponentGetLoop },
		{ "set_loop", LuaComponentSetLoop },
		{ "get_spatial", LuaComponentGetSpatial },
		{ "set_spatial", LuaComponentSetSpatial },
		{ "get_ref_distance", LuaComponentGetRefDistance },
		{ "set_ref_distance", LuaComponentSetRefDistance },
		{ "get_max_distance", LuaComponentGetMaxDistance },
		{ "set_max_distance", LuaComponentSetMaxDistance },
		{ "get_rolloff", LuaComponentGetRolloff },
		{ "set_rolloff", LuaComponentSetRolloff },
		{ "get_attenuation_mode", LuaComponentGetAttenuationMode },
		{ "set_attenuation_mode", LuaComponentSetAttenuationMode },
		{ "get_reverb_send", LuaComponentGetReverbSend },
		{ "set_reverb_send", LuaComponentSetReverbSend },
		{ "get_lowpass_high_frequency_gain", LuaComponentGetLowpassHighFrequencyGain },
		{ "set_lowpass_high_frequency_gain", LuaComponentSetLowpassHighFrequencyGain },
		{ "get_bus", LuaComponentGetBus },
		{ "set_bus", LuaComponentSetBus },
		{ "get_occlusion_enabled", LuaComponentGetOcclusionEnabled },
		{ "set_occlusion_enabled", LuaComponentSetOcclusionEnabled },
		{ "set_occlusion_params", LuaComponentSetOcclusionParams },
		{ "set_occlusion_material", LuaComponentSetOcclusionMaterial },
		{ "get_doppler_enabled", LuaComponentGetDopplerEnabled },
		{ "set_doppler_enabled", LuaComponentSetDopplerEnabled },
		{ "set_cone", LuaComponentSetCone },
		{ "get_file_path", LuaComponentGetFilePath },
		{ "move", LuaComponentQueueMove },
		{ "queue_move", LuaComponentQueueMove },
		{ "set_motion_intent", LuaComponentSetMotionIntent },
		{ "set_inputs", LuaComponentVehicleSetInputs },
		{ "setInputs", LuaComponentVehicleSetInputs },
		{ "set_gear", LuaComponentVehicleSetGear },
		{ "setGear", LuaComponentVehicleSetGear },
		{ "set_automatic_gear", LuaComponentVehicleSetAutomaticGear },
		{ "setAutomaticGear", LuaComponentVehicleSetAutomaticGear },
		{ "get_position", LuaComponentGetPosition },
		{ "set_position", LuaComponentSetPosition },
		{ "sync_from_transform", LuaComponentSyncControllerFromTransform },
		{ "is_grounded", LuaComponentIsGrounded },
		{ "bind_follow_ragdoll", LuaComponentBindFollowRagdoll },
		{ "clear_follow_ragdoll", LuaComponentClearFollowRagdoll },
		{ "is_follow_ragdoll_enabled", LuaComponentIsFollowRagdollEnabled },
		{ "play_state", LuaComponentAnimPlayState },
		{ "set_bool", LuaComponentAnimSetBool },
		{ "set_root_motion_enabled", LuaComponentAnimSetRootMotionEnabled },
		{ "set_float", LuaComponentAnimSetFloat },
		{ "set_int", LuaComponentAnimSetInt },
		{ "set_vector3", LuaComponentAnimSetVector3 },
		{ "set_trigger", LuaComponentAnimSetTrigger },
		{ "switch_graph_set", LuaComponentAnimSwitchGraphSet },
		{ "get_active_graph_set", LuaComponentAnimGetActiveGraphSet },
		{ "is_graph_set_transitioning", LuaComponentAnimIsGraphSetTransitioning },
		{ "get_graph_set_transition_progress", LuaComponentAnimGetGraphSetTransitionProgress },
		{ "set_drive_mode", LuaComponentSetDriveMode },
		{ "set_drive_mode_with_velocity", LuaComponentSetDriveModeWithVelocity },
		{ "get_drive_mode", LuaComponentGetDriveMode },
		{ "set_blend_weight", LuaComponentSetBlendWeight },
		{ "get_blend_weight", LuaComponentGetBlendWeight },
		{ "has_runtime_ragdoll", LuaComponentHasRuntimeRagdoll },
		{ "get_runtime_body_count", LuaComponentGetRuntimeBodyCount },
		{ "get_runtime_joint_count", LuaComponentGetRuntimeJointCount },
		{ "apply_impulse", LuaComponentApplyImpulse },
		{ "get_intensity", LuaComponentGetIntensity },
		{ "set_intensity", LuaComponentSetIntensity },
		{ "get_color", LuaComponentGetColor },
		{ "set_color", LuaComponentSetColor },
		{ "get_radius", LuaComponentGetRadius },
		{ "set_radius", LuaComponentSetRadius },
		{ "get_inner_cutoff", LuaComponentGetInnerCutoff },
		{ "set_inner_cutoff", LuaComponentSetInnerCutoff },
		{ "get_outer_cutoff", LuaComponentGetOuterCutoff },
		{ "set_outer_cutoff", LuaComponentSetOuterCutoff },
		{ "get_fov", LuaComponentGetFov },
		{ "set_fov", LuaComponentSetFov },
		{ "get_near_clip", LuaComponentGetNearClip },
		{ "set_near_clip", LuaComponentSetNearClip },
		{ "get_far_clip", LuaComponentGetFarClip },
		{ "set_far_clip", LuaComponentSetFarClip },
		{ "is_user_look_suppressed", LuaComponentIsUserLookSuppressed },
		{ "get_resolved_forward", LuaComponentGetResolvedForward },
		{ "world_to_viewport", LuaComponentWorldToViewport },
		{ "load_asset", LuaComponentLoadParticle },
		{ "set_asset_path", LuaComponentSetAssetPath },
		{ "get_asset_path", LuaComponentGetAssetPath },
		{ "get_play_time", LuaComponentGetPlayTime },
		{ "get_emitter_count", LuaComponentGetEmitterCount },
		{ "get_emitter_name", LuaComponentGetEmitterName },
		{ "get_alive_count", LuaComponentGetAliveCount },
		{ "get_max_count", LuaComponentGetMaxCount },
		{ "is_emitter_enabled", LuaComponentIsEmitterEnabled },
		{ "set_emitter_enabled", LuaComponentSetEmitterEnabled },
		{ "set_world_position", LuaComponentSetWorldPosition },
		{ "clear_world_position", LuaComponentClearWorldPosition },
		{ nullptr, nullptr }
	};
	RegisterMethods(L, "Vans.Component", componentMethods);

	lua_newtable(L);
	lua_pushcfunction(L, LuaLog); lua_setfield(L, -2, "log");
	lua_pushcfunction(L, LuaFindObject); lua_setfield(L, -2, "find_object");
	lua_pushcfunction(L, LuaTimeSeconds); lua_setfield(L, -2, "time_seconds");

	lua_newtable(L);
	lua_pushcfunction(L, LuaTimelinePlay); lua_setfield(L, -2, "play");
	lua_pushcfunction(L, LuaTimelinePause); lua_setfield(L, -2, "pause");
	lua_pushcfunction(L, LuaTimelineResume); lua_setfield(L, -2, "resume");
	lua_pushcfunction(L, LuaTimelineStop); lua_setfield(L, -2, "stop");
	lua_pushcfunction(L, LuaTimelineState); lua_setfield(L, -2, "state");
	lua_setfield(L, -2, "timeline");

	lua_newtable(L);
	lua_pushcfunction(L, LuaAudioSetBusGain); lua_setfield(L, -2, "set_bus_gain");
	lua_pushcfunction(L, LuaAudioSetBusGain); lua_setfield(L, -2, "setBusGain");
	lua_pushcfunction(L, LuaAudioSetBusLowpassHighFrequencyGain); lua_setfield(L, -2, "set_bus_lowpass_high_frequency_gain");
	lua_pushcfunction(L, LuaAudioSetBusLowpassHighFrequencyGain); lua_setfield(L, -2, "setBusLowpassHighFrequencyGain");
	lua_pushcfunction(L, LuaAudioFadeBusGain); lua_setfield(L, -2, "fade_bus_gain");
	lua_pushcfunction(L, LuaAudioFadeBusGain); lua_setfield(L, -2, "fadeBusGain");
	lua_pushcfunction(L, LuaAudioSetBusMuted); lua_setfield(L, -2, "set_bus_muted");
	lua_pushcfunction(L, LuaAudioSetBusMuted); lua_setfield(L, -2, "setBusMuted");
	lua_pushcfunction(L, LuaAudioSetBusSoloed); lua_setfield(L, -2, "set_bus_soloed");
	lua_pushcfunction(L, LuaAudioSetBusSoloed); lua_setfield(L, -2, "setBusSoloed");
	lua_pushcfunction(L, LuaAudioGetBusState); lua_setfield(L, -2, "get_bus_state");
	lua_pushcfunction(L, LuaAudioGetBusState); lua_setfield(L, -2, "getBusState");
	lua_pushcfunction(L, LuaAudioApplySnapshot); lua_setfield(L, -2, "apply_snapshot");
	lua_pushcfunction(L, LuaAudioApplySnapshot); lua_setfield(L, -2, "applySnapshot");
	lua_pushcfunction(L, LuaAudioApplyNamedSnapshot); lua_setfield(L, -2, "apply_named_snapshot");
	lua_pushcfunction(L, LuaAudioApplyNamedSnapshot); lua_setfield(L, -2, "applyNamedSnapshot");
	lua_pushcfunction(L, LuaAudioApplySnapshotAsset); lua_setfield(L, -2, "apply_snapshot_asset");
	lua_pushcfunction(L, LuaAudioApplySnapshotAsset); lua_setfield(L, -2, "applySnapshotAsset");
	lua_pushcfunction(L, LuaAudioAddDuckingRule); lua_setfield(L, -2, "add_ducking_rule");
	lua_pushcfunction(L, LuaAudioAddDuckingRule); lua_setfield(L, -2, "addDuckingRule");
	lua_pushcfunction(L, LuaAudioClearDuckingRules); lua_setfield(L, -2, "clear_ducking_rules");
	lua_pushcfunction(L, LuaAudioClearDuckingRules); lua_setfield(L, -2, "clearDuckingRules");
	lua_pushcfunction(L, LuaAudioLoadDuckingRulesAsset); lua_setfield(L, -2, "load_ducking_rules_asset");
	lua_pushcfunction(L, LuaAudioLoadDuckingRulesAsset); lua_setfield(L, -2, "loadDuckingRulesAsset");
	lua_setfield(L, -2, "audio");

	lua_newtable(L);
	lua_pushcfunction(L, LuaPhysicsRaycast); lua_setfield(L, -2, "raycast");
	lua_pushcfunction(L, LuaPhysicsRaycast); lua_setfield(L, -2, "raycast_vec");
	lua_pushcfunction(L, LuaPhysicsRaycastAll); lua_setfield(L, -2, "raycast_all");
	lua_pushcfunction(L, LuaPhysicsOverlapSphere); lua_setfield(L, -2, "overlap_sphere");
	lua_pushcfunction(L, LuaPhysicsOverlapSphere); lua_setfield(L, -2, "overlap_sphere_vec");
	lua_setfield(L, -2, "physics_query");

	lua_newtable(L);
	lua_pushcfunction(L, LuaInputIsKeyDown); lua_setfield(L, -2, "is_key_down");
	lua_pushcfunction(L, LuaInputIsKeyPressed); lua_setfield(L, -2, "is_key_pressed");
	lua_pushcfunction(L, LuaInputIsKeyReleased); lua_setfield(L, -2, "is_key_released");
	lua_pushcfunction(L, LuaInputGetMouseDelta); lua_setfield(L, -2, "get_mouse_delta");
	lua_pushcfunction(L, LuaInputGetMousePosition); lua_setfield(L, -2, "get_mouse_position");
	lua_pushcfunction(L, LuaInputGetUIMousePosition); lua_setfield(L, -2, "get_ui_mouse_position");
	lua_pushcfunction(L, LuaInputGetWindowSize); lua_setfield(L, -2, "get_window_size");
	lua_pushcfunction(L, LuaInputGetUIViewSize); lua_setfield(L, -2, "get_ui_view_size");
	lua_pushcfunction(L, LuaInputIsMouseButtonDown); lua_setfield(L, -2, "is_mouse_button_down");
	lua_pushcfunction(L, LuaInputIsMouseButtonPressed); lua_setfield(L, -2, "is_mouse_button_pressed");
	lua_pushcfunction(L, LuaInputIsMouseButtonReleased); lua_setfield(L, -2, "is_mouse_button_released");
	lua_pushcfunction(L, LuaInputSetCursorMode); lua_setfield(L, -2, "set_cursor_mode");
	lua_pushcfunction(L, LuaInputSetMouseCapture); lua_setfield(L, -2, "set_mouse_capture");
	lua_pushcfunction(L, LuaInputIsMouseCaptured); lua_setfield(L, -2, "is_mouse_captured");
	lua_setfield(L, -2, "input");

	VansRuntime::VansLuaUIBridge::Register(L);
	VansRuntime::VansLuaGameplayActionBridge::Register(L);
	lua_setglobal(L, "vans");

	lua_newtable(L);
	lua_pushcfunction(L, LuaSelfGetObject); lua_setfield(L, -2, "get_object");
	lua_pushcfunction(L, LuaSelfGetTransform); lua_setfield(L, -2, "get_transform");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptRenderComponent>); lua_setfield(L, -2, "get_render_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptRenderComponent>); lua_setfield(L, -2, "getRenderComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptAnimationComponent>); lua_setfield(L, -2, "get_anim_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptAnimationComponent>); lua_setfield(L, -2, "getAnimComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptRagdollComponent>); lua_setfield(L, -2, "get_ragdoll_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptRagdollComponent>); lua_setfield(L, -2, "getRagdollComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptCharacterControllerComponent>); lua_setfield(L, -2, "get_cct_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptCharacterControllerComponent>); lua_setfield(L, -2, "getCCTComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptVehicleComponent>); lua_setfield(L, -2, "get_vehicle_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptVehicleComponent>); lua_setfield(L, -2, "getVehicleComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptCameraComponent>); lua_setfield(L, -2, "get_camera_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptCameraComponent>); lua_setfield(L, -2, "getCameraComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptDirectionalLightComponent>); lua_setfield(L, -2, "get_directional_light_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptDirectionalLightComponent>); lua_setfield(L, -2, "getDirectionalLightComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptPointLightComponent>); lua_setfield(L, -2, "get_point_light_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptPointLightComponent>); lua_setfield(L, -2, "getPointLightComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptSpotLightComponent>); lua_setfield(L, -2, "get_spot_light_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptSpotLightComponent>); lua_setfield(L, -2, "getSpotLightComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptRectLightComponent>); lua_setfield(L, -2, "get_rect_light_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptRectLightComponent>); lua_setfield(L, -2, "getRectLightComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptAudioComponent>); lua_setfield(L, -2, "get_audio_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptAudioComponent>); lua_setfield(L, -2, "getAudioComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptVideoComponent>); lua_setfield(L, -2, "get_video_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptVideoComponent>); lua_setfield(L, -2, "getVideoComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptParticleComponent>); lua_setfield(L, -2, "get_particle_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptParticleComponent>); lua_setfield(L, -2, "getParticleComp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptUIComponent>); lua_setfield(L, -2, "get_ui_comp");
	lua_pushcfunction(L, LuaSelfGetOwnerComponent<VansScriptUIComponent>); lua_setfield(L, -2, "getUIComp");
	lua_setglobal(L, "__vans_script_instance_methods");
}

void VansScriptContext::ShutdownLua()
{
	if (!m_LuaState) return;
	AssertLuaThread();
	m_EventConnections.DisconnectAll();
	VansRuntime::VansLuaUIBridge::Shutdown(m_LuaState);
	VansRuntime::VansLuaGameplayActionBridge::Shutdown(m_LuaState);
	ClearTrackedModules();
	s_Instance = nullptr;
	lua_close(m_LuaState);
	m_LuaState = nullptr;
	m_ActiveProjectRoot.clear();
}

void VansScriptContext::ClearTrackedModules()
{
	std::vector<VansLuaScriptComponent*> components;
	components.reserve(m_ScheduledScripts.size());
	for (const auto& scheduled : m_ScheduledScripts)
		if (scheduled.component) components.push_back(scheduled.component);
	for (auto* component : components)
		component->Teardown();
	m_ScheduledScripts.clear();
	m_EventSubscribers.clear();
}

void VansScriptContext::ReloadAllLuaScripts()
{
	for (const auto& scheduled : m_ScheduledScripts)
	{
		if (!scheduled.component) continue;
		scheduled.component->Teardown();
		scheduled.component->Instantiate();
		scheduled.component->Enable();
		if (m_Scene && !scheduled.componentGuid.empty())
			m_Scene->SyncRuntimeScriptComponentFromFacade(scheduled.componentGuid, *scheduled.component);
	}
}

void VansScriptContext::SetScene(VansGraphics::VansScene* scene)
{
	if (m_Scene == scene) return;
	m_Scene = scene;
	RebuildScriptSchedule();
}

void VansScriptContext::AttachSceneWithoutRebuild(VansGraphics::VansScene* scene)
{
	m_Scene = scene;
}

void VansScriptContext::RegisterScriptComponent(VansScriptObject* owner, VansLuaScriptComponent* component)
{
	if (!owner || !component) return;
	auto existing = std::find_if(m_ScheduledScripts.begin(), m_ScheduledScripts.end(),
		[component](const ScheduledScript& scheduled) { return scheduled.component == component; });
	if (existing != m_ScheduledScripts.end())
		return;
	const bool cameraScript = owner->GetComponent<VansScriptCameraComponent>() != nullptr;
	ScheduledScript scheduled;
	scheduled.owner = owner;
	scheduled.component = component;
	scheduled.entityGuid = owner->m_EntityGuid;
	scheduled.componentGuid = component->m_ComponentGuid;
	scheduled.cameraScript = cameraScript;
	if (m_Scene)
	{
		if (auto* runtimeWorld = m_Scene->GetRuntimeWorld())
		{
			if (!scheduled.entityGuid.empty())
				scheduled.runtimeEntity = runtimeWorld->Entities().FindByGuid(scheduled.entityGuid);
			if (!scheduled.componentGuid.empty())
				scheduled.runtimeComponent = runtimeWorld->FindComponentByGuid(
					scheduled.componentGuid,
					Vans::VansRuntimeComponentType_Script);
		}
	}
	m_ScheduledScripts.push_back(std::move(scheduled));
	ScriptEventSubscriber subscriber;
	subscriber.component = component;
	subscriber.entityGuid = owner->m_EntityGuid;
	subscriber.componentGuid = component->m_ComponentGuid;
	if (m_Scene)
	{
		if (auto* runtimeWorld = m_Scene->GetRuntimeWorld())
		{
			if (!subscriber.entityGuid.empty())
				subscriber.runtimeEntity = runtimeWorld->Entities().FindByGuid(subscriber.entityGuid);
			if (!subscriber.componentGuid.empty())
				subscriber.runtimeComponent = runtimeWorld->FindComponentByGuid(
					subscriber.componentGuid,
					Vans::VansRuntimeComponentType_Script);
		}
	}
	m_EventSubscribers[owner->m_TransformID].push_back(std::move(subscriber));
}

void VansScriptContext::UnregisterScriptComponent(VansLuaScriptComponent* component)
{
	if (!component) return;
	m_ScheduledScripts.erase(
		std::remove_if(m_ScheduledScripts.begin(), m_ScheduledScripts.end(),
			[component](const ScheduledScript& scheduled) { return scheduled.component == component; }),
		m_ScheduledScripts.end());
	for (auto it = m_EventSubscribers.begin(); it != m_EventSubscribers.end();)
	{
		auto& subscribers = it->second;
		subscribers.erase(
			std::remove_if(subscribers.begin(), subscribers.end(),
				[component](const ScriptEventSubscriber& subscriber)
				{
					return subscriber.component == component;
				}),
			subscribers.end());
		if (subscribers.empty()) it = m_EventSubscribers.erase(it);
		else ++it;
	}
}

void VansScriptContext::RebuildScriptSchedule()
{
	m_ScheduledScripts.clear();
	m_EventSubscribers.clear();
	if (!m_Scene) return;
	for (auto* owner : m_Scene->GetSceneObjects())
	{
		if (!owner) continue;
		for (auto* baseComponent : owner->m_Components)
			if (auto* component = dynamic_cast<VansLuaScriptComponent*>(baseComponent))
				RegisterScriptComponent(owner, component);
	}
}

bool VansScriptContext::ResolveRuntimeHandles(ScheduledScript& scheduled) const
{
	if (!m_Scene)
		return true;
	auto* runtimeWorld = m_Scene->GetRuntimeWorld();
	if (!runtimeWorld)
		return true;

	if ((!scheduled.runtimeEntity.IsValid() || !runtimeWorld->IsAlive(scheduled.runtimeEntity)) &&
		!scheduled.entityGuid.empty())
	{
		scheduled.runtimeEntity = runtimeWorld->Entities().FindByGuid(scheduled.entityGuid);
	}
	if (!scheduled.runtimeEntity.IsValid() || !runtimeWorld->IsAlive(scheduled.runtimeEntity))
		return false;

	if ((!scheduled.runtimeComponent.IsValid() ||
		!runtimeWorld->GetComponentHeader(scheduled.runtimeComponent)) &&
		!scheduled.componentGuid.empty())
	{
		scheduled.runtimeComponent = runtimeWorld->FindComponentByGuid(
			scheduled.componentGuid,
			Vans::VansRuntimeComponentType_Script);
	}
	return scheduled.runtimeComponent.IsValid() &&
		runtimeWorld->GetComponentHeader(scheduled.runtimeComponent) != nullptr;
}

bool VansScriptContext::ResolveRuntimeHandles(ScriptEventSubscriber& subscriber) const
{
	if (!m_Scene)
		return true;
	auto* runtimeWorld = m_Scene->GetRuntimeWorld();
	if (!runtimeWorld)
		return true;

	if ((!subscriber.runtimeEntity.IsValid() || !runtimeWorld->IsAlive(subscriber.runtimeEntity)) &&
		!subscriber.entityGuid.empty())
	{
		subscriber.runtimeEntity = runtimeWorld->Entities().FindByGuid(subscriber.entityGuid);
	}
	if (!subscriber.runtimeEntity.IsValid() || !runtimeWorld->IsAlive(subscriber.runtimeEntity))
		return false;

	if ((!subscriber.runtimeComponent.IsValid() ||
		!runtimeWorld->GetComponentHeader(subscriber.runtimeComponent)) &&
		!subscriber.componentGuid.empty())
	{
		subscriber.runtimeComponent = runtimeWorld->FindComponentByGuid(
			subscriber.componentGuid,
			Vans::VansRuntimeComponentType_Script);
	}
	return subscriber.runtimeComponent.IsValid() &&
		runtimeWorld->GetComponentHeader(subscriber.runtimeComponent) != nullptr;
}

void VansScriptContext::VansScriptUpdate()
{
	VANS_PROFILE_SCOPE("Script::VansScriptUpdate", Vans::ProfileCategory::Script);
	VansScriptPreUpdate();
	UpdateScriptComponents(false, false);
}

void VansScriptContext::VansScriptUpdateNonCameraScripts()
{
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
	VANS_PROFILE_SCOPE("Script::VansScriptUpdateNonCameraScripts", Vans::ProfileCategory::Script);
	VansScriptPreUpdate();
	UpdateScriptComponents(false, true);
}

void VansScriptContext::VansScriptUpdateCameraScripts()
{
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
	VANS_PROFILE_SCOPE("Script::VansScriptUpdateCameraScripts", Vans::ProfileCategory::Script);
	UpdateScriptComponents(true, false);
}

void VansScriptContext::VansScriptPreUpdate()
{
	if (!m_Scene) return;
	m_Scene->FlushPendingEntityDestructions();
	Vans::VansEventBus::Get().Flush(Vans::VansEventLane::Physics);
}

void VansScriptContext::UpdateScriptComponents(bool cameraScriptsOnly, bool skipCameraScripts)
{
	if (!m_Scene) return;
	const float deltaTime = static_cast<float>(VansGraphics::VansTimer::GetLastFrameDelta());
	for (ScheduledScript& scheduled : m_ScheduledScripts)
	{
		if (cameraScriptsOnly && !scheduled.cameraScript) continue;
		if (skipCameraScripts && scheduled.cameraScript) continue;
		auto* component = scheduled.component;
		if (!component) continue;
		if (!ResolveRuntimeHandles(scheduled))
			continue;
		if (component->m_State == VansLuaScriptState::Unloaded &&
			!component->m_ScriptPath.empty() &&
			!component->m_EntryName.empty())
		{
			component->m_OwnerObject = scheduled.owner;
			component->Instantiate();
			component->Enable();
			if (!scheduled.componentGuid.empty())
				m_Scene->SyncRuntimeScriptComponentFromFacade(scheduled.componentGuid, *component);
		}
		component->CallUpdate(deltaTime);
		if (!scheduled.componentGuid.empty())
			m_Scene->SyncRuntimeScriptComponentFromFacade(scheduled.componentGuid, *component);
	}
}

void VansScriptContext::HandlePhysicsContactEvent(const VansEngine::VansPhysicsContactEvent& event)
{
	if (!m_Scene) return;
	DispatchEventToObject(event, event.transformID_A, event.transformID_B,
		event.nameB, event.contactPoint, event.contactNormal, event.impulse);
	if (event.type == VansEngine::VansPhysicsContactEventType::CollisionEnter ||
		event.type == VansEngine::VansPhysicsContactEventType::CollisionExit)
	{
		DispatchEventToObject(event, event.transformID_B, event.transformID_A,
			event.nameA, event.contactPoint, -event.contactNormal, event.impulse);
	}
}

void VansScriptContext::DispatchEventToObject(
	const VansEngine::VansPhysicsContactEvent& event,
	std::uint32_t selfTransformID,
	std::uint32_t otherTransformID,
	const std::string& otherName,
	const glm::vec3& contactPoint,
	const glm::vec3& contactNormal,
	float impulse)
{
	auto subscriberIt = m_EventSubscribers.find(selfTransformID);
	if (subscriberIt == m_EventSubscribers.end())
		return;

	VansScriptPhysicsEventInfo info;
	info.otherName = otherName;
	info.otherTransformID = otherTransformID;
	info.contactPoint[0] = contactPoint.x;
	info.contactPoint[1] = contactPoint.y;
	info.contactPoint[2] = contactPoint.z;
	info.contactNormal[0] = contactNormal.x;
	info.contactNormal[1] = contactNormal.y;
	info.contactNormal[2] = contactNormal.z;
	info.impulse = impulse;

	for (ScriptEventSubscriber& subscriber : subscriberIt->second)
	{
		auto* component = subscriber.component;
		if (!component) continue;
		if (!ResolveRuntimeHandles(subscriber))
			continue;
		switch (event.type)
		{
		case VansEngine::VansPhysicsContactEventType::CollisionEnter:
			component->CallOnCollisionEnter(info); break;
		case VansEngine::VansPhysicsContactEventType::CollisionExit:
			component->CallOnCollisionExit(info); break;
		case VansEngine::VansPhysicsContactEventType::TriggerEnter:
			component->CallOnTriggerEnter(info); break;
		case VansEngine::VansPhysicsContactEventType::TriggerExit:
			component->CallOnTriggerExit(info); break;
		}
	}
}
