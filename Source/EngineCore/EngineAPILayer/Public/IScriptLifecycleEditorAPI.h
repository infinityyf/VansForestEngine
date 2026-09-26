#pragma once

#include <string>

namespace Vans::EditorAPI
{
	class IScriptLifecycleEditorAPI
	{
	public:
		virtual ~IScriptLifecycleEditorAPI() = default;
		virtual void InitializeRuntimeScripts() = 0;
		virtual void SetupRuntimeScriptProjectVenv(const std::string& projectRootPath) = 0;
		virtual void ReloadRuntimeScripts() = 0;
	};
}
