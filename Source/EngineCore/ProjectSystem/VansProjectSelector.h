#pragma once
// -----------------------------------------------------------------------
// VansProjectSelector  –  ImGui-based project selection overlay
//
// Renders a full-screen ImGui overlay for picking / creating a project.
// This runs inside the normal editor main loop after ImGui + Vulkan are
// already initialised — no native Win32 dialogs needed.
//
// Depends on: VansProjectConfig (RecentProjectEntry), VansLog, ImGui.
// -----------------------------------------------------------------------

#include "VansProjectConfig.h"

#include <string>
#include <vector>

namespace Vans {

enum class ProjectSelectorResult
{
	None,           // User has not yet made a choice
	OpenExisting,   // Open an existing project
	CreateNew,      // Create a new project
	Cancelled       // Window was closed / user pressed Quit
};

class VansProjectSelector
{
public:
	VansProjectSelector();

	/// Render the full-screen project selector overlay.
	/// Call each frame while no project is loaded.
	/// Returns None while the user is still browsing.
	ProjectSelectorResult Render();

	/// Refresh the recent projects list (e.g. after opening a project).
	void RefreshRecentProjects();

	const std::string& GetSelectedProjectPath() const { return m_SelectedPath; }
	const std::string& GetNewProjectName()      const { return m_NewProjectName; }

private:
	// ImGui helpers
	void RenderRecentProjectsList();
	void RenderOpenProjectPanel();
	void RenderCreateProjectPanel();

	// Native folder browser (Win32 IFileDialog)
	static void OpenFolderBrowseDialog(std::string& outPath);

	// State
	ProjectSelectorResult              m_Result = ProjectSelectorResult::None;
	std::vector<RecentProjectEntry>    m_RecentProjects;
	std::string                        m_SelectedPath;
	std::string                        m_NewProjectName;
	int                                m_SelectedRecentIndex = -1;

	// Error / status message
	std::string                        m_StatusMessage;
	bool                               m_StatusIsError = false;

	// ImGui text buffers
	char  m_BufBrowsePath[512]  = {};
	char  m_BufNewName[256]     = {};
	char  m_BufNewLocation[512] = {};
	int   m_ActiveTab           = 0;   // 0 = Open, 1 = Create
};

} // namespace Vans
