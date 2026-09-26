#include "VansLuaUIBridge.h"

#include "VansLuaValueConverter.h"
#include "../EventCore/VansEventBus.h"
#include "../RuntimeUI/Public/VansUIComponent.h"
#include "../RuntimeUI/Public/VansUIComponentRegistry.h"
#include "../RuntimeUI/Public/VansUIElementHandle.h"
#include "../RuntimeUI/Public/VansUIEvents.h"
#include "../RuntimeUI/Public/VansUIResourceRegistry.h"
#include "../RuntimeUI/Public/VansUIScreen.h"
#include "../RuntimeUI/Public/VansUIScreenManager.h"
#include "../RuntimeUI/Public/VansUISystem.h"
#include "../RuntimeUI/Public/VansUIViewModel.h"
#include "../Util/VansLog.h"

#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C"
{
#include <lauxlib.h>
}

namespace VansRuntime
{
	namespace
	{
		constexpr const char* kScreenMeta = "Vans.UI.Screen";
		constexpr const char* kElementMeta = "Vans.UI.Element";
		constexpr const char* kViewModelMeta = "Vans.UI.ViewModel";
		constexpr const char* kComponentMeta = "Vans.UI.Component";
		constexpr const char* kSubscriptionMeta = "Vans.UI.Subscription";
		using LuaUIActionSubscriptionId = std::uint64_t;
		constexpr LuaUIActionSubscriptionId kInvalidLuaUIActionSubscription = 0;

		struct LuaUIScreenUserdata
		{
			VansUIHandleId screenId = kInvalidUIHandle;
		};

		struct LuaUIElementUserdata
		{
			VansUIHandleId screenId = kInvalidUIHandle;
			VansUIHandleId componentId = kInvalidUIHandle;
			std::string elementName;
		};

		struct LuaUISubscriptionUserdata
		{
			LuaUIActionSubscriptionId id = kInvalidLuaUIActionSubscription;
		};

		struct LuaUIViewModelUserdata
		{
			VansUIHandleId viewModelId = kInvalidUIHandle;
		};

		struct LuaUIComponentUserdata
		{
			VansUIHandleId componentId = kInvalidUIHandle;
		};

		struct LuaUIActionSubscription
		{
			lua_State* L = nullptr;
			int ref = LUA_NOREF;
			Vans::VansEventConnection connection;
		};

		struct LuaCommandCallback
		{
			lua_State* L = nullptr;
			int ref = LUA_NOREF;
		};

		struct LuaUIBridgeSession
		{
			std::uint64_t generation = AllocateUIHandle();
			std::unordered_map<LuaUIActionSubscriptionId, LuaUIActionSubscription> actionSubscriptions;
			std::unordered_map<std::uint64_t, LuaCommandCallback> commandCallbacks;
			std::unordered_map<VansUIHandleId, std::shared_ptr<VansUIViewModel>> viewModels;
			std::unordered_map<VansUIHandleId, std::vector<std::uint64_t>> viewModelCommandTokens;
			std::unordered_map<VansUIHandleId, std::vector<LuaUIActionSubscriptionId>> screenElementSubscriptions;
			std::unordered_map<VansUIHandleId, std::vector<LuaUIActionSubscriptionId>> componentElementSubscriptions;
			LuaUIActionSubscriptionId nextActionSubscriptionId = 1;
			std::uint64_t nextCommandToken = 1;
		};

		std::unordered_map<lua_State*, LuaUIBridgeSession> g_Sessions;

		LuaUIBridgeSession* FindSession(lua_State* state)
		{
			const auto found = g_Sessions.find(state);
			return found != g_Sessions.end() ? &found->second : nullptr;
		}

		LuaUIBridgeSession* FindSession(lua_State* state, std::uint64_t generation)
		{
			LuaUIBridgeSession* session = FindSession(state);
			return session && session->generation == generation ? session : nullptr;
		}

		LuaUIBridgeSession& RequireSession(lua_State* state)
		{
			return g_Sessions.try_emplace(state).first->second;
		}

		LuaUIScreenUserdata* CheckScreen(lua_State* L, int index)
		{
			return static_cast<LuaUIScreenUserdata*>(luaL_checkudata(L, index, kScreenMeta));
		}

		LuaUIElementUserdata* CheckElement(lua_State* L, int index)
		{
			return static_cast<LuaUIElementUserdata*>(luaL_checkudata(L, index, kElementMeta));
		}

		LuaUISubscriptionUserdata* CheckSubscription(lua_State* L, int index)
		{
			return static_cast<LuaUISubscriptionUserdata*>(luaL_checkudata(L, index, kSubscriptionMeta));
		}

		LuaUIViewModelUserdata* CheckViewModel(lua_State* L, int index)
		{
			return static_cast<LuaUIViewModelUserdata*>(luaL_checkudata(L, index, kViewModelMeta));
		}

		LuaUIComponentUserdata* CheckComponent(lua_State* L, int index)
		{
			return static_cast<LuaUIComponentUserdata*>(luaL_checkudata(L, index, kComponentMeta));
		}

		std::shared_ptr<VansUIScreen> ResolveScreen(VansUIHandleId screenId)
		{
			if (screenId == kInvalidUIHandle)
				return nullptr;
			if (!VansUISystem::Get().IsInitialized())
				return nullptr;
			return VansUISystem::Get().GetScreenManager().GetScreen(screenId);
		}

		void PushScreen(lua_State* L, VansUIHandleId screenId)
		{
			auto* userdata = static_cast<LuaUIScreenUserdata*>(
				lua_newuserdatauv(L, sizeof(LuaUIScreenUserdata), 0));
			new (userdata) LuaUIScreenUserdata{ screenId };
			luaL_getmetatable(L, kScreenMeta);
			lua_setmetatable(L, -2);
		}

		void PushElement(
			lua_State* L,
			VansUIHandleId screenId,
			std::string elementName,
			VansUIHandleId componentId = kInvalidUIHandle)
		{
			auto* userdata = static_cast<LuaUIElementUserdata*>(
				lua_newuserdatauv(L, sizeof(LuaUIElementUserdata), 0));
			new (userdata) LuaUIElementUserdata{ screenId, componentId, std::move(elementName) };
			luaL_getmetatable(L, kElementMeta);
			lua_setmetatable(L, -2);
		}

		void PushSubscription(lua_State* L, LuaUIActionSubscriptionId id)
		{
			auto* userdata = static_cast<LuaUISubscriptionUserdata*>(
				lua_newuserdatauv(L, sizeof(LuaUISubscriptionUserdata), 0));
			new (userdata) LuaUISubscriptionUserdata{ id };
			luaL_getmetatable(L, kSubscriptionMeta);
			lua_setmetatable(L, -2);
		}

		void PushViewModel(lua_State* L, VansUIHandleId viewModelId)
		{
			auto* userdata = static_cast<LuaUIViewModelUserdata*>(
				lua_newuserdatauv(L, sizeof(LuaUIViewModelUserdata), 0));
			new (userdata) LuaUIViewModelUserdata{ viewModelId };
			luaL_getmetatable(L, kViewModelMeta);
			lua_setmetatable(L, -2);
		}

		void PushComponent(lua_State* L, VansUIHandleId componentId)
		{
			auto* userdata = static_cast<LuaUIComponentUserdata*>(
				lua_newuserdatauv(L, sizeof(LuaUIComponentUserdata), 0));
			new (userdata) LuaUIComponentUserdata{ componentId };
			luaL_getmetatable(L, kComponentMeta);
			lua_setmetatable(L, -2);
		}

		std::shared_ptr<VansUIComponentInstance> ResolveComponent(VansUIHandleId componentId)
		{
			if (componentId == kInvalidUIHandle)
				return nullptr;
			return VansUIComponentRegistry::Get().GetComponent(componentId);
		}

		std::shared_ptr<VansUIViewModel> ResolveViewModel(
			lua_State* state,
			VansUIHandleId viewModelId)
		{
			const LuaUIBridgeSession* session = FindSession(state);
			if (!session)
				return nullptr;
			const auto it = session->viewModels.find(viewModelId);
			return it != session->viewModels.end() ? it->second : nullptr;
		}

		std::shared_ptr<VansUIViewModel> CreateViewModelFromTable(
			lua_State* L,
			int index,
			std::string* error = nullptr)
		{
			auto vm = std::make_shared<VansUIViewModel>();
			if (lua_istable(L, index))
			{
				VansUIVariantMap values;
				std::string codecError;
				if (!VansLuaValueConverter::TryToVariantMap(
					L, index, values, codecError))
				{
					if (error)
						*error = std::move(codecError);
					return nullptr;
				}
				for (auto& [name, value] : values)
					vm->SetValue(name, std::move(value));
			}
			return vm;
		}

		std::shared_ptr<VansUIViewModel> ReadOptionalViewModel(lua_State* L, int index)
		{
			if (luaL_testudata(L, index, kViewModelMeta))
				return ResolveViewModel(L, CheckViewModel(L, index)->viewModelId);
			if (lua_istable(L, index))
				return CreateViewModelFromTable(L, index);
			return nullptr;
		}

		VansUIHandleId RegisterViewModel(
			lua_State* state,
			std::shared_ptr<VansUIViewModel> vm)
		{
			if (!vm)
				return kInvalidUIHandle;

			const VansUIHandleId id = AllocateUIHandle();
			RequireSession(state).viewModels.emplace(id, std::move(vm));
			return id;
		}

		std::string VariantToString(const VansUIVariant& value)
		{
			return std::visit([](const auto& typedValue) -> std::string
			{
				using T = std::decay_t<decltype(typedValue)>;
				if constexpr (std::is_same_v<T, std::monostate>)
					return {};
				else if constexpr (std::is_same_v<T, bool>)
					return typedValue ? "true" : "false";
				else if constexpr (std::is_same_v<T, std::int64_t>)
					return std::to_string(typedValue);
				else if constexpr (std::is_same_v<T, double>)
					return std::to_string(typedValue);
				else if constexpr (std::is_same_v<T, std::string>)
					return typedValue;
				else if constexpr (std::is_same_v<T, VansUIHandleId>)
					return std::to_string(typedValue);
				else
					return {};
			}, value.value);
		}

		LuaUIActionSubscriptionId SubscribeLuaAction(
			lua_State* L,
			std::string actionName,
			int ref,
			bool includeParams,
			const char* callbackName)
		{
			if (!L || actionName.empty() || ref == LUA_NOREF)
				return kInvalidLuaUIActionSubscription;

			LuaUIBridgeSession& session = RequireSession(L);
			const std::uint64_t sessionGeneration = session.generation;
			const LuaUIActionSubscriptionId id = session.nextActionSubscriptionId++;
			Vans::VansEventConnection connection = Vans::VansEventBus::Get().Subscribe<VansUIActionEvent>(
				[L, sessionGeneration, id, actionName = std::move(actionName), includeParams,
					callbackName = std::string(callbackName ? callbackName : "UI action")](const VansUIActionEvent& action)
				{
					if (action.name != actionName)
						return;

					LuaUIBridgeSession* activeSession = FindSession(L, sessionGeneration);
					if (!activeSession)
						return;
					const auto it = activeSession->actionSubscriptions.find(id);
					if (it == activeSession->actionSubscriptions.end() ||
						it->second.ref == LUA_NOREF)
						return;

					lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.ref);
					lua_newtable(L);
					lua_pushstring(L, action.name.c_str());
					lua_setfield(L, -2, "name");
					if (includeParams)
					{
						std::string codecError;
						if (!VansLuaValueConverter::TryPushVariantMap(
							L, action.params, codecError))
						{
							lua_pop(L, 2);
							VANS_LOG_ERROR("[LuaUI] Cannot encode action params: " << codecError);
							return;
						}
						lua_setfield(L, -2, "params");
					}
					lua_pushinteger(L, static_cast<lua_Integer>(action.sourceScreen));
					lua_setfield(L, -2, "source_screen");
					lua_pushstring(L, action.sourceElement.c_str());
					lua_setfield(L, -2, "source_element");

					if (lua_pcall(L, 1, 0, 0) != LUA_OK)
					{
						const char* error = lua_tostring(L, -1);
						VANS_LOG_ERROR("[LuaUI] " << callbackName << " callback failed: "
							<< (error ? error : "unknown error"));
						lua_pop(L, 1);
					}
				},
				Vans::VansEventLane::MainThread);
			session.actionSubscriptions.emplace(id,
				LuaUIActionSubscription{ L, ref, std::move(connection) });
			return id;
		}

		void UnsubscribeLuaAction(lua_State* L, LuaUIActionSubscriptionId id)
		{
			if (id == kInvalidLuaUIActionSubscription)
				return;

			LuaUIBridgeSession* session = FindSession(L);
			if (!session)
				return;
			if (const auto it = session->actionSubscriptions.find(id);
				it != session->actionSubscriptions.end())
			{
				if (it->second.L == L && it->second.ref != LUA_NOREF)
					luaL_unref(L, LUA_REGISTRYINDEX, it->second.ref);
				session->actionSubscriptions.erase(it);
			}
		}

		void ReleaseScreenLuaSubscriptions(lua_State* L, VansUIHandleId screenId)
		{
			LuaUIBridgeSession* session = FindSession(L);
			if (!session)
				return;
			const auto it = session->screenElementSubscriptions.find(screenId);
			if (it == session->screenElementSubscriptions.end())
				return;

			for (LuaUIActionSubscriptionId id : it->second)
				UnsubscribeLuaAction(L, id);
			session->screenElementSubscriptions.erase(it);
		}

		void ReleaseAllScreenLuaSubscriptions(lua_State* L)
		{
			std::vector<VansUIHandleId> screenIds;
			LuaUIBridgeSession* session = FindSession(L);
			if (!session)
				return;
			screenIds.reserve(session->screenElementSubscriptions.size());
			for (const auto& [screenId, tokens] : session->screenElementSubscriptions)
			{
				(void)tokens;
				screenIds.push_back(screenId);
			}
			for (VansUIHandleId screenId : screenIds)
				ReleaseScreenLuaSubscriptions(L, screenId);
		}

		void ReleaseComponentLuaSubscriptions(lua_State* L, VansUIHandleId componentId)
		{
			LuaUIBridgeSession* session = FindSession(L);
			if (!session)
				return;
			const auto it = session->componentElementSubscriptions.find(componentId);
			if (it == session->componentElementSubscriptions.end())
				return;

			for (LuaUIActionSubscriptionId id : it->second)
				UnsubscribeLuaAction(L, id);
			session->componentElementSubscriptions.erase(it);
		}

		void ReleaseAllComponentLuaSubscriptions(lua_State* L)
		{
			std::vector<VansUIHandleId> componentIds;
			LuaUIBridgeSession* session = FindSession(L);
			if (!session)
				return;
			componentIds.reserve(session->componentElementSubscriptions.size());
			for (const auto& [componentId, tokens] : session->componentElementSubscriptions)
			{
				(void)tokens;
				componentIds.push_back(componentId);
			}
			for (VansUIHandleId componentId : componentIds)
				ReleaseComponentLuaSubscriptions(L, componentId);
		}

		void ReleaseCommandCallback(lua_State* L, std::uint64_t token)
		{
			if (token == 0)
				return;

			LuaUIBridgeSession* session = FindSession(L);
			if (!session)
				return;
			const auto it = session->commandCallbacks.find(token);
			if (it == session->commandCallbacks.end())
				return;

			if (it->second.L == L && it->second.ref != LUA_NOREF)
				luaL_unref(L, LUA_REGISTRYINDEX, it->second.ref);
			session->commandCallbacks.erase(it);
		}

		void ReleaseViewModelCommands(lua_State* L, VansUIHandleId viewModelId)
		{
			LuaUIBridgeSession* session = FindSession(L);
			if (!session)
				return;
			const auto it = session->viewModelCommandTokens.find(viewModelId);
			if (it == session->viewModelCommandTokens.end())
				return;

			for (std::uint64_t token : it->second)
				ReleaseCommandCallback(L, token);
			session->viewModelCommandTokens.erase(it);
		}

		VansUIElementHandle ResolveElement(lua_State* L, LuaUIElementUserdata* userdata)
		{
			if (!userdata)
				return {};
			auto screen = ResolveScreen(userdata->screenId);
			if (screen)
				return screen->FindElement(userdata->elementName);

			auto component = ResolveComponent(userdata->componentId);
			return component ? component->FindElement(userdata->elementName) : VansUIElementHandle{};
		}

		int UIIsAvailable(lua_State* L)
		{
			lua_pushboolean(L, VansUISystem::Get().IsInitialized());
			return 1;
		}

		int UIOpenScreen(lua_State* L)
		{
			const char* path = luaL_checkstring(L, 1);
			if (!VansUISystem::Get().IsInitialized())
			{
				lua_pushnil(L);
				return 1;
			}
			auto screen = VansUISystem::Get().LoadScreen(path ? path : "", ReadOptionalViewModel(L, 2));
			if (!screen)
			{
				lua_pushnil(L);
				return 1;
			}
			PushScreen(L, screen->GetHandleId());
			return 1;
		}

		int UIPushScreen(lua_State* L)
		{
			const char* path = luaL_checkstring(L, 1);
			if (!VansUISystem::Get().IsInitialized())
			{
				lua_pushnil(L);
				return 1;
			}

			auto screen = VansUISystem::Get().GetScreenManager().PushScreenConfig(
				path ? path : "",
				ReadOptionalViewModel(L, 2));
			if (!screen)
			{
				lua_pushnil(L);
				return 1;
			}
			PushScreen(L, screen->GetHandleId());
			return 1;
		}

		int UIReplaceScreen(lua_State* L)
		{
			const char* path = luaL_checkstring(L, 1);
			if (!VansUISystem::Get().IsInitialized())
			{
				lua_pushnil(L);
				return 1;
			}

			auto screen = VansUISystem::Get().GetScreenManager().ReplaceScreenConfig(
				path ? path : "",
				ReadOptionalViewModel(L, 2));
			if (!screen)
			{
				lua_pushnil(L);
				return 1;
			}
			PushScreen(L, screen->GetHandleId());
			return 1;
		}

		int UIPopScreen(lua_State* L)
		{
			if (VansUISystem::Get().IsInitialized())
				VansUISystem::Get().GetScreenManager().PopScreen();
			return 0;
		}

		int UIPreloadScreen(lua_State* L)
		{
			const char* path = luaL_checkstring(L, 1);
			const bool ok = VansUISystem::Get().IsInitialized() &&
				VansUISystem::Get().PreloadScreen(path ? path : "");
			lua_pushboolean(L, ok);
			return 1;
		}

		int UIReleaseScreen(lua_State* L)
		{
			const char* path = luaL_checkstring(L, 1);
			if (VansUISystem::Get().IsInitialized())
				VansUISystem::Get().ReleaseScreen(path ? path : "");
			return 0;
		}

		int UIReloadScreen(lua_State* L)
		{
			const char* path = luaL_checkstring(L, 1);
			if (!VansUISystem::Get().IsInitialized())
			{
				lua_pushnil(L);
				return 1;
			}

			auto screen = VansUISystem::Get().ReloadScreen(
				path ? path : "",
				ReadOptionalViewModel(L, 2));
			if (!screen)
			{
				lua_pushnil(L);
				return 1;
			}
			PushScreen(L, screen->GetHandleId());
			return 1;
		}

		int UIShowHUD(lua_State* L)
		{
			const char* path = luaL_checkstring(L, 1);
			if (!VansUISystem::Get().IsInitialized())
			{
				lua_pushnil(L);
				return 1;
			}

			auto screen = VansUISystem::Get().GetScreenManager().SetHUDConfig(
				path ? path : "",
				ReadOptionalViewModel(L, 2));
			if (!screen)
			{
				lua_pushnil(L);
				return 1;
			}
			screen->Show();
			PushScreen(L, screen->GetHandleId());
			return 1;
		}

		int UIHideHUD(lua_State* L)
		{
			if (VansUISystem::Get().IsInitialized())
				VansUISystem::Get().GetScreenManager().HideHUD();
			return 0;
		}

		int UIShowModal(lua_State* L)
		{
			const char* path = luaL_checkstring(L, 1);
			if (!VansUISystem::Get().IsInitialized())
			{
				lua_pushnil(L);
				return 1;
			}

			auto screen = VansUISystem::Get().GetScreenManager().ShowModalConfig(
				path ? path : "",
				ReadOptionalViewModel(L, 2));
			if (!screen)
			{
				lua_pushnil(L);
				return 1;
			}
			PushScreen(L, screen->GetHandleId());
			return 1;
		}

		int UIHideModal(lua_State* L)
		{
			if (VansUISystem::Get().IsInitialized())
				VansUISystem::Get().GetScreenManager().HideModal();
			return 0;
		}

		int UIShowOverlay(lua_State* L)
		{
			const char* name = luaL_checkstring(L, 1);
			const char* path = luaL_checkstring(L, 2);
			if (VansUISystem::Get().IsInitialized())
			{
				VansUISystem::Get().GetScreenManager().ShowOverlay(
					name ? name : "",
					path ? path : "",
					ReadOptionalViewModel(L, 3));
			}
			return 0;
		}

		int UIHideOverlay(lua_State* L)
		{
			const char* name = luaL_checkstring(L, 1);
			if (VansUISystem::Get().IsInitialized())
				VansUISystem::Get().GetScreenManager().HideOverlay(name ? name : "");
			return 0;
		}

		int UIFindScreen(lua_State* L)
		{
			const char* name = luaL_checkstring(L, 1);
			if (!VansUISystem::Get().IsInitialized())
			{
				lua_pushnil(L);
				return 1;
			}

			auto screen = VansUISystem::Get().GetScreenManager().GetScreenByName(name ? name : "");
			if (!screen)
			{
				lua_pushnil(L);
				return 1;
			}
			PushScreen(L, screen->GetHandleId());
			return 1;
		}

		int UICloseAll(lua_State* L)
		{
			if (VansUISystem::Get().IsInitialized())
			{
				ReleaseAllScreenLuaSubscriptions(L);
				ReleaseAllComponentLuaSubscriptions(L);
				VansUIComponentRegistry::Get().CloseAll();
				VansUISystem::Get().GetScreenManager().CloseAll();
			}
			return 0;
		}

		int UICloseScreen(lua_State* L)
		{
			if (luaL_testudata(L, 1, kScreenMeta))
			{
				auto* screen = CheckScreen(L, 1);
				ReleaseScreenLuaSubscriptions(L, screen->screenId);
				VansUISystem::Get().CloseScreen(screen->screenId);
				screen->screenId = kInvalidUIHandle;
			}
			else
			{
				const char* name = luaL_checkstring(L, 1);
				if (VansUISystem::Get().IsInitialized())
				{
					if (auto screen = VansUISystem::Get().GetScreenManager().GetScreenByName(name ? name : ""))
						ReleaseScreenLuaSubscriptions(L, screen->GetHandleId());
				}
				VansUISystem::Get().CloseScreenByName(name ? name : "");
			}
			return 0;
		}

		int UICloseScreenByName(lua_State* L)
		{
			const char* name = luaL_checkstring(L, 1);
			if (VansUISystem::Get().IsInitialized())
			{
				if (auto screen = VansUISystem::Get().GetScreenManager().GetScreenByName(name ? name : ""))
					ReleaseScreenLuaSubscriptions(L, screen->GetHandleId());
			}
			VansUISystem::Get().CloseScreenByName(name ? name : "");
			return 0;
		}

		int UIDispatch(lua_State* L)
		{
			const char* actionName = luaL_checkstring(L, 1);
			VansUIVariantMap params;
			if (lua_istable(L, 2))
			{
				std::string error;
				if (!VansLuaValueConverter::TryToVariantMap(L, 2, params, error))
					return luaL_argerror(L, 2, error.c_str());
			}
			Vans::VansEventBus::Get().PublishNow(VansUIActionEvent{
				actionName ? actionName : "", std::move(params) });
			return 0;
		}

		int UIOnAction(lua_State* L)
		{
			const char* actionName = luaL_checkstring(L, 1);
			luaL_checktype(L, 2, LUA_TFUNCTION);
			lua_pushvalue(L, 2);
			const int ref = luaL_ref(L, LUA_REGISTRYINDEX);

			const LuaUIActionSubscriptionId id = SubscribeLuaAction(
				L, actionName ? actionName : "", ref, true, "UI action");
			if (id == kInvalidLuaUIActionSubscription)
			{
				luaL_unref(L, LUA_REGISTRYINDEX, ref);
				lua_pushnil(L);
				return 1;
			}

			PushSubscription(L, id);
			return 1;
		}

		int UICreateViewModel(lua_State* L)
		{
			std::string error;
			auto vm = CreateViewModelFromTable(L, 1, &error);
			if (!vm && !error.empty())
				return luaL_argerror(L, 1, error.c_str());
			const VansUIHandleId id = RegisterViewModel(L, std::move(vm));
			if (id == kInvalidUIHandle)
			{
				lua_pushnil(L);
				return 1;
			}
			PushViewModel(L, id);
			return 1;
		}

		int UIGetToken(lua_State* L)
		{
			const char* group = luaL_checkstring(L, 1);
			const char* name = luaL_checkstring(L, 2);
			const auto token = VansUIResourceRegistry::Get().GetToken(
				group ? group : "",
				name ? name : "");
			if (!token)
			{
				lua_pushnil(L);
				return 1;
			}
			std::string error;
			if (!VansLuaValueConverter::TryPushVariant(L, *token, error))
				return luaL_error(L, "%s", error.c_str());
			return 1;
		}

		int UILocalize(lua_State* L)
		{
			const char* key = luaL_checkstring(L, 1);
			const char* locale = luaL_optstring(L, 2, "default");
			const char* fallback = luaL_optstring(L, 3, "");
			const std::string value = VansUIResourceRegistry::Get().Localize(
				key ? key : "",
				locale ? locale : "default",
				fallback ? fallback : "");
			lua_pushstring(L, value.c_str());
			return 1;
		}

		int UILoadComponent(lua_State* L)
		{
			const char* path = luaL_checkstring(L, 1);
			if (!VansUISystem::Get().IsInitialized())
			{
				lua_pushnil(L);
				return 1;
			}

			auto component = VansUIComponentRegistry::Get().LoadComponent(path ? path : "");
			if (!component)
			{
				lua_pushnil(L);
				return 1;
			}

			PushComponent(L, component->GetHandleId());
			return 1;
		}

		int UIBindViewModel(lua_State* L)
		{
			auto* screen = CheckScreen(L, 1);
			auto* vm = CheckViewModel(L, 2);
			auto resolvedScreen = ResolveScreen(screen->screenId);
			auto resolvedVm = ResolveViewModel(L, vm->viewModelId);
			if (resolvedScreen && resolvedVm)
				resolvedScreen->SetViewModel(std::move(resolvedVm));
			return 0;
		}

		int ScreenIsValid(lua_State* L)
		{
			auto* screen = CheckScreen(L, 1);
			lua_pushboolean(L, ResolveScreen(screen->screenId) != nullptr);
			return 1;
		}

		int ScreenClose(lua_State* L)
		{
			auto* screen = CheckScreen(L, 1);
			ReleaseScreenLuaSubscriptions(L, screen->screenId);
			VansUISystem::Get().CloseScreen(screen->screenId);
			screen->screenId = kInvalidUIHandle;
			return 0;
		}

		int ScreenFind(lua_State* L)
		{
			auto* screen = CheckScreen(L, 1);
			const char* elementName = luaL_checkstring(L, 2);
			auto resolvedScreen = ResolveScreen(screen->screenId);
			if (!resolvedScreen || !elementName)
			{
				lua_pushnil(L);
				return 1;
			}
			VansUIElementHandle element = resolvedScreen->FindElement(elementName);
			if (!element.IsValid())
			{
				lua_pushnil(L);
				return 1;
			}
			PushElement(L, screen->screenId, elementName);
			return 1;
		}

		int ScreenGetName(lua_State* L)
		{
			auto* screen = CheckScreen(L, 1);
			auto resolvedScreen = ResolveScreen(screen->screenId);
			lua_pushstring(L, resolvedScreen ? resolvedScreen->GetName().c_str() : "");
			return 1;
		}

		int ScreenShow(lua_State* L)
		{
			auto* screen = CheckScreen(L, 1);
			if (auto resolvedScreen = ResolveScreen(screen->screenId))
				resolvedScreen->Show();
			return 0;
		}

		int ScreenHide(lua_State* L)
		{
			auto* screen = CheckScreen(L, 1);
			if (auto resolvedScreen = ResolveScreen(screen->screenId))
				resolvedScreen->Hide();
			return 0;
		}

		int ScreenBindViewModel(lua_State* L)
		{
			lua_pushcfunction(L, UIBindViewModel);
			lua_pushvalue(L, 1);
			lua_pushvalue(L, 2);
			lua_call(L, 2, 0);
			return 0;
		}

		int ElementIsValid(lua_State* L)
		{
			auto* element = CheckElement(L, 1);
			lua_pushboolean(L, ResolveElement(L, element).IsValid());
			return 1;
		}

		int ElementSetText(lua_State* L)
		{
			auto* element = CheckElement(L, 1);
			const char* text = luaL_checkstring(L, 2);
			ResolveElement(L, element).SetText(text ? text : "");
			return 0;
		}

		int ElementGetText(lua_State* L)
		{
			auto* element = CheckElement(L, 1);
			lua_pushstring(L, ResolveElement(L, element).GetText().c_str());
			return 1;
		}

		int ElementSetVisible(lua_State* L)
		{
			auto* element = CheckElement(L, 1);
			ResolveElement(L, element).SetVisible(lua_toboolean(L, 2) != 0);
			return 0;
		}

		int ElementIsVisible(lua_State* L)
		{
			auto* element = CheckElement(L, 1);
			lua_pushboolean(L, ResolveElement(L, element).IsVisible());
			return 1;
		}

		int ElementSetProperty(lua_State* L)
		{
			auto* element = CheckElement(L, 1);
			const char* property = luaL_checkstring(L, 2);
			VansUIVariant value;
			std::string error;
			if (!VansLuaValueConverter::TryToVariant(L, 3, value, error))
				return luaL_argerror(L, 3, error.c_str());
			ResolveElement(L, element).SetProperty(property ? property : "", VariantToString(value));
			return 0;
		}

		int ElementOn(lua_State* L)
		{
			auto* element = CheckElement(L, 1);
			const char* eventName = luaL_checkstring(L, 2);
			luaL_checktype(L, 3, LUA_TFUNCTION);

			if (!eventName || std::string(eventName) != "Click")
			{
				lua_pushnil(L);
				return 1;
			}

			VansUIElementHandle resolvedElement = ResolveElement(L, element);
			if (!resolvedElement.IsValid())
			{
				lua_pushnil(L);
				return 1;
			}

			lua_pushvalue(L, 3);
			const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
			const std::string actionName = "__lua.element." +
				std::to_string(AllocateUIHandle());
			const LuaUIActionSubscriptionId id = SubscribeLuaAction(
				L, actionName, ref, false, "UI element");
			if (id == kInvalidLuaUIActionSubscription)
			{
				luaL_unref(L, LUA_REGISTRYINDEX, ref);
				lua_pushnil(L);
				return 1;
			}

			const VansUIHandleId screenId = element->screenId;
			const VansUIHandleId componentId = element->componentId;
			const std::string elementName = element->elementName;
			if (componentId != kInvalidUIHandle)
				RequireSession(L).componentElementSubscriptions[componentId].push_back(id);
			else
				RequireSession(L).screenElementSubscriptions[screenId].push_back(id);
			resolvedElement.BindClick([actionName, screenId, elementName]()
			{
				Vans::VansEventBus::Get().PublishNow(
					VansUIActionEvent{ actionName, {}, screenId, elementName });
			});
			PushSubscription(L, id);
			return 1;
		}

		int ViewModelSet(lua_State* L)
		{
			auto* vm = CheckViewModel(L, 1);
			const char* name = luaL_checkstring(L, 2);
			VansUIVariant value;
			std::string error;
			if (!VansLuaValueConverter::TryToVariant(L, 3, value, error))
				return luaL_argerror(L, 3, error.c_str());
			if (auto resolvedVm = ResolveViewModel(L, vm->viewModelId))
				resolvedVm->SetValue(name ? name : "", std::move(value));
			return 0;
		}

		int ViewModelGet(lua_State* L)
		{
			auto* vm = CheckViewModel(L, 1);
			const char* name = luaL_checkstring(L, 2);
			auto resolvedVm = ResolveViewModel(L, vm->viewModelId);
			if (!resolvedVm || !name)
			{
				lua_pushnil(L);
				return 1;
			}
			const VansUIVariant* value = resolvedVm->GetValue(name);
			if (!value)
			{
				lua_pushnil(L);
				return 1;
			}
			std::string error;
			if (!VansLuaValueConverter::TryPushVariant(L, *value, error))
				return luaL_error(L, "%s", error.c_str());
			return 1;
		}

		int ViewModelCommand(lua_State* L)
		{
			auto* vm = CheckViewModel(L, 1);
			const char* commandName = luaL_checkstring(L, 2);
			luaL_checktype(L, 3, LUA_TFUNCTION);
			auto resolvedVm = ResolveViewModel(L, vm->viewModelId);
			if (!resolvedVm || !commandName || commandName[0] == '\0')
				return 0;

			lua_pushvalue(L, 3);
			const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
			LuaUIBridgeSession& session = RequireSession(L);
			const std::uint64_t sessionGeneration = session.generation;
			const std::uint64_t token = session.nextCommandToken++;
			session.commandCallbacks[token] = LuaCommandCallback{ L, ref };
			session.viewModelCommandTokens[vm->viewModelId].push_back(token);

			resolvedVm->BindCommand(commandName, [L, sessionGeneration, token]()
			{
				LuaUIBridgeSession* activeSession = FindSession(L, sessionGeneration);
				if (!activeSession)
					return;
				const auto it = activeSession->commandCallbacks.find(token);
				if (it == activeSession->commandCallbacks.end() || it->second.ref == LUA_NOREF)
					return;

				lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.ref);
				if (lua_pcall(L, 0, 0, 0) != LUA_OK)
				{
					const char* error = lua_tostring(L, -1);
					VANS_LOG_ERROR("[LuaUI] ViewModel command failed: " << (error ? error : "unknown error"));
					lua_pop(L, 1);
				}
			});

			resolvedVm->BindCommandWithParam(commandName,
				[L, sessionGeneration, token](const std::string& parameter)
			{
				LuaUIBridgeSession* activeSession = FindSession(L, sessionGeneration);
				if (!activeSession)
					return;
				const auto it = activeSession->commandCallbacks.find(token);
				if (it == activeSession->commandCallbacks.end() || it->second.ref == LUA_NOREF)
					return;

				lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.ref);
				lua_pushstring(L, parameter.c_str());
				if (lua_pcall(L, 1, 0, 0) != LUA_OK)
				{
					const char* error = lua_tostring(L, -1);
					VANS_LOG_ERROR("[LuaUI] ViewModel parameterized command failed: " << (error ? error : "unknown error"));
					lua_pop(L, 1);
				}
			});
			return 0;
		}

		int ViewModelSetCanExecute(lua_State* L)
		{
			auto* vm = CheckViewModel(L, 1);
			const char* commandName = luaL_checkstring(L, 2);
			if (auto resolvedVm = ResolveViewModel(L, vm->viewModelId))
				resolvedVm->SetCommandCanExecute(commandName ? commandName : "", lua_toboolean(L, 3) != 0);
			return 0;
		}

		int ViewModelRunCommand(lua_State* L)
		{
			auto* vm = CheckViewModel(L, 1);
			const char* commandName = luaL_checkstring(L, 2);
			auto resolvedVm = ResolveViewModel(L, vm->viewModelId);
			bool ok = false;
			if (resolvedVm && commandName)
			{
				if (lua_gettop(L) >= 3)
				{
					VansUIVariant parameter;
					std::string error;
					if (!VansLuaValueConverter::TryToVariant(L, 3, parameter, error))
						return luaL_argerror(L, 3, error.c_str());
					ok = resolvedVm->ExecuteCommandWithParam(
						commandName, VariantToString(parameter));
				}
				else
					ok = resolvedVm->ExecuteCommand(commandName);
			}
			lua_pushboolean(L, ok);
			return 1;
		}

		int ViewModelIsValid(lua_State* L)
		{
			auto* vm = CheckViewModel(L, 1);
			lua_pushboolean(L, ResolveViewModel(L, vm->viewModelId) != nullptr);
			return 1;
		}

		int ComponentIsValid(lua_State* L)
		{
			auto* component = CheckComponent(L, 1);
			lua_pushboolean(L, ResolveComponent(component->componentId) != nullptr);
			return 1;
		}

		int ComponentClose(lua_State* L)
		{
			auto* component = CheckComponent(L, 1);
			ReleaseComponentLuaSubscriptions(L, component->componentId);
			VansUIComponentRegistry::Get().CloseComponent(component->componentId);
			component->componentId = kInvalidUIHandle;
			return 0;
		}

		int ComponentFind(lua_State* L)
		{
			auto* component = CheckComponent(L, 1);
			const char* elementName = luaL_checkstring(L, 2);
			auto resolvedComponent = ResolveComponent(component->componentId);
			if (!resolvedComponent || !elementName)
			{
				lua_pushnil(L);
				return 1;
			}

			VansUIElementHandle element = resolvedComponent->FindElement(elementName);
			if (!element.IsValid())
			{
				lua_pushnil(L);
				return 1;
			}

			PushElement(L, kInvalidUIHandle, elementName, component->componentId);
			return 1;
		}

		int ComponentSetProperty(lua_State* L)
		{
			auto* component = CheckComponent(L, 1);
			const char* propertyName = luaL_checkstring(L, 2);
			VansUIVariant value;
			std::string error;
			if (!VansLuaValueConverter::TryToVariant(L, 3, value, error))
				return luaL_argerror(L, 3, error.c_str());
			if (auto resolvedComponent = ResolveComponent(component->componentId))
				resolvedComponent->SetProperty(propertyName ? propertyName : "", std::move(value));
			return 0;
		}

		int ComponentGetProperty(lua_State* L)
		{
			auto* component = CheckComponent(L, 1);
			const char* propertyName = luaL_checkstring(L, 2);
			auto resolvedComponent = ResolveComponent(component->componentId);
			if (!resolvedComponent || !propertyName)
			{
				lua_pushnil(L);
				return 1;
			}

			std::string error;
			if (!VansLuaValueConverter::TryPushVariant(
				L, resolvedComponent->GetProperty(propertyName), error))
			{
				return luaL_error(L, "%s", error.c_str());
			}
			return 1;
		}

		int ComponentSetState(lua_State* L)
		{
			auto* component = CheckComponent(L, 1);
			const char* state = luaL_checkstring(L, 2);
			if (auto resolvedComponent = ResolveComponent(component->componentId))
				resolvedComponent->SetState(state ? state : "");
			return 0;
		}

		int ComponentPlayAnimation(lua_State* L)
		{
			auto* component = CheckComponent(L, 1);
			const char* animation = luaL_checkstring(L, 2);
			if (auto resolvedComponent = ResolveComponent(component->componentId))
				resolvedComponent->PlayAnimation(animation ? animation : "");
			return 0;
		}

		int ComponentGC(lua_State* L)
		{
			auto* component = static_cast<LuaUIComponentUserdata*>(luaL_checkudata(L, 1, kComponentMeta));
			ReleaseComponentLuaSubscriptions(L, component->componentId);
			VansUIComponentRegistry::Get().CloseComponent(component->componentId);
			component->componentId = kInvalidUIHandle;
			return 0;
		}

		int SubscriptionUnsubscribe(lua_State* L)
		{
			auto* subscription = CheckSubscription(L, 1);
			if (subscription->id == kInvalidLuaUIActionSubscription)
				return 0;

			UnsubscribeLuaAction(L, subscription->id);
			subscription->id = kInvalidLuaUIActionSubscription;
			return 0;
		}

		int SubscriptionGC(lua_State* L)
		{
			auto* subscription = static_cast<LuaUISubscriptionUserdata*>(
				luaL_checkudata(L, 1, kSubscriptionMeta));
			UnsubscribeLuaAction(L, subscription->id);
			subscription->id = kInvalidLuaUIActionSubscription;
			return 0;
		}

		int ElementGC(lua_State* L)
		{
			auto* element = static_cast<LuaUIElementUserdata*>(luaL_checkudata(L, 1, kElementMeta));
			element->~LuaUIElementUserdata();
			return 0;
		}

		int ViewModelGC(lua_State* L)
		{
			auto* vm = static_cast<LuaUIViewModelUserdata*>(luaL_checkudata(L, 1, kViewModelMeta));
			ReleaseViewModelCommands(L, vm->viewModelId);
			if (LuaUIBridgeSession* session = FindSession(L))
				session->viewModels.erase(vm->viewModelId);
			vm->viewModelId = kInvalidUIHandle;
			return 0;
		}

		void RegisterMeta(lua_State* L, const char* name, const luaL_Reg* methods, lua_CFunction gc = nullptr)
		{
			luaL_newmetatable(L, name);
			if (gc)
			{
				lua_pushcfunction(L, gc);
				lua_setfield(L, -2, "__gc");
			}
			lua_newtable(L);
			luaL_setfuncs(L, methods, 0);
			lua_setfield(L, -2, "__index");
			lua_pop(L, 1);
		}
	}

	void VansLuaUIBridge::Register(lua_State* L)
	{
		if (!L)
			return;
		RequireSession(L);
		const luaL_Reg screenMethods[] = {
			{ "is_valid", ScreenIsValid },
			{ "get_name", ScreenGetName },
			{ "show", ScreenShow },
			{ "hide", ScreenHide },
			{ "close", ScreenClose },
			{ "find", ScreenFind },
			{ "bind_vm", ScreenBindViewModel },
			{ nullptr, nullptr }
		};
		RegisterMeta(L, kScreenMeta, screenMethods);

		const luaL_Reg elementMethods[] = {
			{ "is_valid", ElementIsValid },
			{ "set_text", ElementSetText },
			{ "get_text", ElementGetText },
			{ "set_visible", ElementSetVisible },
			{ "is_visible", ElementIsVisible },
			{ "set_property", ElementSetProperty },
			{ "on", ElementOn },
			{ nullptr, nullptr }
		};
		RegisterMeta(L, kElementMeta, elementMethods, ElementGC);

		const luaL_Reg viewModelMethods[] = {
			{ "is_valid", ViewModelIsValid },
			{ "set", ViewModelSet },
			{ "get", ViewModelGet },
			{ "command", ViewModelCommand },
			{ "set_can_execute", ViewModelSetCanExecute },
			{ "run_command", ViewModelRunCommand },
			{ nullptr, nullptr }
		};
		RegisterMeta(L, kViewModelMeta, viewModelMethods, ViewModelGC);

		const luaL_Reg componentMethods[] = {
			{ "is_valid", ComponentIsValid },
			{ "close", ComponentClose },
			{ "find", ComponentFind },
			{ "set_property", ComponentSetProperty },
			{ "get_property", ComponentGetProperty },
			{ "set_state", ComponentSetState },
			{ "play_animation", ComponentPlayAnimation },
			{ nullptr, nullptr }
		};
		RegisterMeta(L, kComponentMeta, componentMethods, ComponentGC);

		const luaL_Reg subscriptionMethods[] = {
			{ "unsubscribe", SubscriptionUnsubscribe },
			{ nullptr, nullptr }
		};
		RegisterMeta(L, kSubscriptionMeta, subscriptionMethods, SubscriptionGC);

		const luaL_Reg uiBindings[] = {
			{ "is_available", UIIsAvailable },
			{ "open_screen", UIOpenScreen },
			{ "push_screen", UIPushScreen },
			{ "replace_screen", UIReplaceScreen },
			{ "pop_screen", UIPopScreen },
			{ "preload_screen", UIPreloadScreen },
			{ "release_screen", UIReleaseScreen },
			{ "reload_screen", UIReloadScreen },
			{ "show_hud", UIShowHUD },
			{ "hide_hud", UIHideHUD },
			{ "show_modal", UIShowModal },
			{ "hide_modal", UIHideModal },
			{ "show_overlay", UIShowOverlay },
			{ "hide_overlay", UIHideOverlay },
			{ "find_screen", UIFindScreen },
			{ "close_all", UICloseAll },
			{ "close_screen", UICloseScreen },
			{ "close_screen_by_name", UICloseScreenByName },
			{ "dispatch", UIDispatch },
			{ "on_action", UIOnAction },
			{ "create_vm", UICreateViewModel },
			{ "bind_vm", UIBindViewModel },
			{ "get_token", UIGetToken },
			{ "localize", UILocalize },
			{ "load_component", UILoadComponent },
			{ nullptr, nullptr }
		};
		lua_newtable(L);
		luaL_setfuncs(L, uiBindings, 0);
		lua_setfield(L, -2, "ui");
	}

	void VansLuaUIBridge::Shutdown(lua_State* L)
	{
		const auto found = g_Sessions.find(L);
		if (found == g_Sessions.end())
			return;
		LuaUIBridgeSession& session = found->second;
		for (const auto& [id, subscription] : session.actionSubscriptions)
		{
			(void)id;
			if (subscription.L == L && subscription.ref != LUA_NOREF)
				luaL_unref(L, LUA_REGISTRYINDEX, subscription.ref);
		}
		for (const auto& [token, callback] : session.commandCallbacks)
		{
			(void)token;
			if (callback.L == L && callback.ref != LUA_NOREF)
				luaL_unref(L, LUA_REGISTRYINDEX, callback.ref);
		}
		g_Sessions.erase(found);
	}
}
