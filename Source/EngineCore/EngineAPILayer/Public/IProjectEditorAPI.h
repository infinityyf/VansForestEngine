#pragma once

#include "EngineDTOs.h"

#include <string>
#include <vector>

namespace Vans::EditorAPI
{
	class IProjectEditorAPI
	{
	public:
		virtual ~IProjectEditorAPI() = default;
		virtual std::vector<RecentProjectEntry> GetRecentProjects() const = 0;
		virtual ProjectOpenResult OpenProject(const ProjectOpenRequest& request) = 0;
		virtual void CloseProject() = 0;
		virtual ProjectConfigSnapshot GetProjectConfigSnapshot() const = 0;
		virtual ProjectConfigEditResult SetProjectDefaultScene(
			const std::string& sceneRelativePath) = 0;
		virtual ProjectConfigEditResult SetProjectPathField(
			ProjectPathField field, const std::string& relativePath) = 0;
		virtual ProjectConfigEditResult SetProjectScriptSearchPaths(
			const std::vector<std::string>& paths) = 0;
		virtual ProjectConfigEditResult SetProjectAssetDirectory(
			const std::string& key, const std::string& relativePath) = 0;
		virtual ProjectConfigEditResult SaveProjectDocuments() = 0;
		virtual VansProjectPhysicsTiming GetProjectPhysicsTiming() const = 0;
		virtual ProjectConfigEditResult SetProjectPhysicsTiming(
			const VansProjectPhysicsTiming& timing) = 0;
		virtual bool SetCurrentProjectScenePath(const std::string& scenePath) = 0;
		virtual std::string GetProjectRootPath() const = 0;
	};
}
