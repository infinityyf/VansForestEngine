#pragma once

#include "EngineIds.h"

#include <string>

namespace Vans::EditorAPI
{
	enum class EnginePlayState;

	class IEngineEventListener
	{
	public:
		virtual ~IEngineEventListener() = default;

		virtual void OnEntityCreated(EntityId id) {}
		virtual void OnEntityDestroyed(EntityId id) {}
		virtual void OnEntityRenamed(EntityId id, const std::string& newName) {}
		virtual void OnComponentChanged(EntityId entityId, ComponentId componentId) {}
		virtual void OnSceneLoaded(const std::string& sceneName) {}
		virtual void OnSceneUnloaded() {}
		virtual void OnAssetImported(AssetId id) {}
		virtual void OnPlayStateChanged(EnginePlayState state) {}
	};
}
