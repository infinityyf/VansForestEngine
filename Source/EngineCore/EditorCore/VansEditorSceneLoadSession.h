#pragma once

#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <string>
#include <utility>

namespace VansGraphics
{
	class VansEditorSceneLoadSession final
	{
	public:
		const std::string& CurrentScenePath() const { return m_CurrentScenePath; }
		const std::string& PendingPath() const { return m_PendingPath; }
		Vans::EditorAPI::RuntimeSceneLoadMode PendingMode() const { return m_PendingMode; }
		bool HasPendingRequest() const { return !m_PendingPath.empty(); }

		void Request(std::string scenePath)
		{
			m_PendingPath = std::move(scenePath);
		}

		void Request(Vans::EditorAPI::RuntimeSceneLoadMode mode, std::string scenePath)
		{
			m_PendingMode = mode;
			m_PendingPath = std::move(scenePath);
		}

		void MarkLoaded(std::string scenePath)
		{
			m_CurrentScenePath = std::move(scenePath);
		}

		void ClearPending() { m_PendingPath.clear(); }

		void Reset()
		{
			m_CurrentScenePath.clear();
			m_PendingPath.clear();
			m_PendingMode = Vans::EditorAPI::RuntimeSceneLoadMode::Editor;
		}

	private:
		std::string m_CurrentScenePath;
		std::string m_PendingPath;
		Vans::EditorAPI::RuntimeSceneLoadMode m_PendingMode =
			Vans::EditorAPI::RuntimeSceneLoadMode::Editor;
	};
}
