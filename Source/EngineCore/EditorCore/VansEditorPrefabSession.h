#pragma once

#include "VansEditorSceneDocumentSession.h"
#include "VansEditorSelectionService.h"
#include "../AuthoringCore/VansAssetDocumentRegistry.h"
#include "../EngineAPILayer/Public/EngineDTOs.h"
#include "../SceneCore/VansSceneDocument.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace VansGraphics
{
	struct VansPrefabRequest
	{
		enum class Kind
		{
			Create,
			Place,
			Open,
			Close,
			Discard,
			Unpack,
			Revert,
			Apply,
			Duplicate,
			Delete
		} kind;

		std::string target;
		std::string path;
		std::string token;
		std::string parent;
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct VansEditorPrefabStage
	{
		std::shared_ptr<Vans::VansOpenAssetDocument> asset;
		std::string root;
		VansEditorSceneDocumentState previousSceneState;
		Vans::EditorSelectionSnapshot previousSelection;
		Vans::EditorAPI::EditorViewportCameraState previousCamera;
		Vans::SceneStateId savedState = 0;
	};

	struct VansEditorPrefabToolbarCache
	{
		const Vans::VansSceneDocument* document = nullptr;
		std::shared_ptr<const Vans::VansSerializedValue> authoringRoot;
		Vans::SceneStateId documentState = 0;
		std::uint64_t selectionRevision = 0;
		std::string selectedEntity;
		std::string sourceAsset;
		bool valid = false;
	};

	class VansEditorPrefabSession final
	{
	public:
		void Queue(VansPrefabRequest request)
		{
			m_Requests.push_back(std::move(request));
		}

		bool HasPendingRequests() const { return !m_Requests.empty(); }

		std::vector<VansPrefabRequest> TakePendingRequests()
		{
			auto pending = std::move(m_Requests);
			m_Requests.clear();
			return pending;
		}

		bool HasStage() const { return m_Stage != nullptr; }
		VansEditorPrefabStage* Stage() { return m_Stage.get(); }
		const VansEditorPrefabStage* Stage() const { return m_Stage.get(); }

		void Begin(std::unique_ptr<VansEditorPrefabStage> stage)
		{
			m_Stage = std::move(stage);
		}

		std::unique_ptr<VansEditorPrefabStage> End()
		{
			return std::move(m_Stage);
		}

		std::string& Status() { return m_Status; }
		const std::string& Status() const { return m_Status; }
		VansEditorPrefabToolbarCache& ToolbarCache() { return m_ToolbarCache; }

		void Reset()
		{
			m_Requests.clear();
			m_Stage.reset();
			m_Status.clear();
			m_ToolbarCache = {};
		}

	private:
		std::vector<VansPrefabRequest> m_Requests;
		std::unique_ptr<VansEditorPrefabStage> m_Stage;
		std::string m_Status;
		VansEditorPrefabToolbarCache m_ToolbarCache;
	};
}
