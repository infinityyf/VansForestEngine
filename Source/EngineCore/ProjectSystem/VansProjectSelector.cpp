#include "VansProjectSelector.h"
#include "VansProjectManager.h"
#include "../Util/VansLog.h"

#include "imgui.h"

#include <filesystem>
#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#endif

namespace fs = std::filesystem;

namespace Vans {

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------
VansProjectSelector::VansProjectSelector()
{
	RefreshRecentProjects();
}

void VansProjectSelector::RefreshRecentProjects()
{
	m_RecentProjects = RecentProjects::Load();
}

// -----------------------------------------------------------------------
// Main full-screen overlay – called each frame while no project is loaded
// -----------------------------------------------------------------------
ProjectSelectorResult VansProjectSelector::Render()
{
	m_Result = ProjectSelectorResult::None;

	// Full viewport overlay
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoScrollbar;

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
	ImGui::Begin("##ProjectSelector", nullptr, flags);
	ImGui::PopStyleColor();

	float windowW = ImGui::GetWindowWidth();
	float windowH = ImGui::GetWindowHeight();

	// ── Title ──────────────────────────────────────────────────
	ImGui::SetCursorPosY(30.0f);
	{
		const char* title = "ForestEngine";
		float titleW = ImGui::CalcTextSize(title).x;
		ImGui::SetCursorPosX((windowW - titleW) * 0.5f);
		ImGui::PushFont(nullptr);
		ImGui::TextColored(ImVec4(0.45f, 0.75f, 0.35f, 1.0f), "%s", title);
		ImGui::PopFont();
	}
	{
		const char* subtitle = "Project Selector";
		float subW = ImGui::CalcTextSize(subtitle).x;
		ImGui::SetCursorPosX((windowW - subW) * 0.5f);
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", subtitle);
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// ── Two panels side by side ────────────────────────────────
	float panelWidth = windowW * 0.5f - 20.0f;
	float panelHeight = windowH - 150.0f;

	// Left: Recent Projects
	ImGui::BeginChild("##RecentPanel", ImVec2(panelWidth, panelHeight), ImGuiChildFlags_Borders);
	RenderRecentProjectsList();
	ImGui::EndChild();

	ImGui::SameLine(0.0f, 20.0f);

	// Right: Open / Create tabs
	ImGui::BeginChild("##ActionPanel", ImVec2(panelWidth, panelHeight), ImGuiChildFlags_Borders);
	{
		// Tab bar
		if (ImGui::BeginTabBar("##ProjectTabs"))
		{
			if (ImGui::BeginTabItem("Open Project"))
			{
				m_ActiveTab = 0;
				ImGui::Spacing();
				RenderOpenProjectPanel();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Create Project"))
			{
				m_ActiveTab = 1;
				ImGui::Spacing();
				RenderCreateProjectPanel();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::EndChild();

	// ── Status message ─────────────────────────────────────────
	if (!m_StatusMessage.empty())
	{
		ImGui::Spacing();
		ImVec4 color = m_StatusIsError
			? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
			: ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
		ImGui::TextColored(color, "%s", m_StatusMessage.c_str());
	}

	// ── Bottom bar ─────────────────────────────────────────────
	ImGui::Spacing();
	if (ImGui::Button("Quit", ImVec2(100, 30)))
	{
		m_Result = ProjectSelectorResult::Cancelled;
	}

	ImGui::End();

	return m_Result;
}

// -----------------------------------------------------------------------
// Recent projects list (left panel)
// -----------------------------------------------------------------------
void VansProjectSelector::RenderRecentProjectsList()
{
	ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Recent Projects");
	ImGui::Separator();
	ImGui::Spacing();

	if (m_RecentProjects.empty())
	{
		ImGui::TextDisabled("No recent projects found.");
		ImGui::TextDisabled("Open or create a project to get started.");
		return;
	}

	for (size_t i = 0; i < m_RecentProjects.size(); ++i)
	{
		auto& entry = m_RecentProjects[i];
		ImGui::PushID(static_cast<int>(i));

		// Check if project still exists
		std::string configPath = entry.path;
		std::replace(configPath.begin(), configPath.end(), '\\', '/');
		if (!configPath.empty() && configPath.back() != '/')
			configPath += '/';
		bool exists = fs::exists(configPath + "ForestProject.json");

		if (!exists)
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

		bool isSelected = (m_SelectedRecentIndex == static_cast<int>(i));

		// Selectable row — single click selects, double click opens
		if (ImGui::Selectable(("##entry" + std::to_string(i)).c_str(), isSelected,
			ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 52)))
		{
			if (exists)
			{
				m_SelectedRecentIndex = static_cast<int>(i);
				m_SelectedPath = configPath;
				m_StatusMessage.clear();

				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					m_Result = ProjectSelectorResult::OpenExisting;
				}
			}
			else
			{
				m_StatusMessage = "Project folder no longer exists: " + entry.path;
				m_StatusIsError = true;
			}
		}

		// Draw name + path on the selectable
		ImVec2 itemMin = ImGui::GetItemRectMin();
		ImGui::SameLine();
		ImGui::SetCursorScreenPos(ImVec2(itemMin.x + 8.0f, itemMin.y + 4.0f));
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", entry.name.c_str());
		ImGui::SetCursorScreenPos(ImVec2(itemMin.x + 8.0f, itemMin.y + 24.0f));
		ImGui::TextDisabled("%s", entry.path.c_str());
		ImGui::SetCursorScreenPos(ImVec2(itemMin.x + 8.0f, itemMin.y + 40.0f));
		ImGui::TextDisabled("%s", entry.lastOpened.c_str());

		if (!exists)
			ImGui::PopStyleColor();

		ImGui::PopID();
	}

	// Open selected button
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	bool hasSelection = m_SelectedRecentIndex >= 0 && m_SelectedRecentIndex < static_cast<int>(m_RecentProjects.size());
	if (!hasSelection) ImGui::BeginDisabled();
	if (ImGui::Button("Open Selected", ImVec2(180, 36)))
	{
		auto& entry = m_RecentProjects[m_SelectedRecentIndex];
		std::string path = entry.path;
		std::replace(path.begin(), path.end(), '\\', '/');
		if (!path.empty() && path.back() != '/')
			path += '/';

		if (fs::exists(path + "ForestProject.json"))
		{
			m_SelectedPath = path;
			m_Result = ProjectSelectorResult::OpenExisting;
		}
		else
		{
			m_StatusMessage = "Project folder no longer exists.";
			m_StatusIsError = true;
		}
	}
	if (!hasSelection) ImGui::EndDisabled();
}

// -----------------------------------------------------------------------
// Open existing project panel (right panel, tab 0)
// -----------------------------------------------------------------------
void VansProjectSelector::RenderOpenProjectPanel()
{
	ImGui::Text("Browse for an existing project folder:");
	ImGui::Text("(Must contain a ForestProject.json)");
	ImGui::Spacing();

	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f);
	ImGui::InputText("##BrowsePath", m_BufBrowsePath, sizeof(m_BufBrowsePath));
	ImGui::SameLine();
	if (ImGui::Button("Browse...##Open", ImVec2(90, 0)))
	{
		std::string path;
		OpenFolderBrowseDialog(path);
		if (!path.empty())
		{
#ifdef _WIN32
			strncpy_s(m_BufBrowsePath, path.c_str(), sizeof(m_BufBrowsePath) - 1);
#else
			strncpy(m_BufBrowsePath, path.c_str(), sizeof(m_BufBrowsePath) - 1);
#endif
			m_StatusMessage.clear();
		}
	}

	ImGui::Spacing();
	ImGui::Spacing();

	bool canOpen = (strlen(m_BufBrowsePath) > 0);
	if (!canOpen) ImGui::BeginDisabled();
	if (ImGui::Button("Open Project", ImVec2(200, 40)))
	{
		std::string path(m_BufBrowsePath);
		std::replace(path.begin(), path.end(), '\\', '/');
		if (!path.empty() && path.back() != '/')
			path += '/';

		if (fs::exists(path + "ForestProject.json"))
		{
			m_SelectedPath = path;
			m_Result = ProjectSelectorResult::OpenExisting;
		}
		else
		{
			m_StatusMessage = "ForestProject.json not found in: " + path;
			m_StatusIsError = true;
		}
	}
	if (!canOpen) ImGui::EndDisabled();
}

// -----------------------------------------------------------------------
// Create project panel (right panel, tab 1)
// -----------------------------------------------------------------------
void VansProjectSelector::RenderCreateProjectPanel()
{
	ImGui::Text("Project Name:");
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
	ImGui::InputText("##NewName", m_BufNewName, sizeof(m_BufNewName));

	ImGui::Spacing();
	ImGui::Text("Location:");
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f);
	ImGui::InputText("##NewLocation", m_BufNewLocation, sizeof(m_BufNewLocation));
	ImGui::SameLine();
	if (ImGui::Button("Browse...##Create", ImVec2(90, 0)))
	{
		std::string path;
		OpenFolderBrowseDialog(path);
		if (!path.empty())
		{
#ifdef _WIN32
			strncpy_s(m_BufNewLocation, path.c_str(), sizeof(m_BufNewLocation) - 1);
#else
			strncpy(m_BufNewLocation, path.c_str(), sizeof(m_BufNewLocation) - 1);
#endif
		}
	}

	// Preview the full path
	std::string name(m_BufNewName);
	std::string location(m_BufNewLocation);
	if (!name.empty() && !location.empty())
	{
		std::replace(location.begin(), location.end(), '\\', '/');
		if (!location.empty() && location.back() != '/')
			location += '/';
		std::string fullPath = location + name;
		ImGui::Spacing();
		ImGui::TextDisabled("Project will be created at:");
		ImGui::TextWrapped("%s", fullPath.c_str());
	}

	ImGui::Spacing();
	ImGui::Spacing();

	bool canCreate = (strlen(m_BufNewName) > 0) && (strlen(m_BufNewLocation) > 0);
	if (!canCreate) ImGui::BeginDisabled();
	if (ImGui::Button("Create Project", ImVec2(200, 40)))
	{
		std::string loc(m_BufNewLocation);
		std::replace(loc.begin(), loc.end(), '\\', '/');
		if (!loc.empty() && loc.back() != '/')
			loc += '/';

		m_SelectedPath = loc + std::string(m_BufNewName) + "/";
		m_NewProjectName = std::string(m_BufNewName);
		m_Result = ProjectSelectorResult::CreateNew;
	}
	if (!canCreate) ImGui::EndDisabled();
}

// -----------------------------------------------------------------------
// Native folder browser (Win32 IFileDialog)
// -----------------------------------------------------------------------
void VansProjectSelector::OpenFolderBrowseDialog(std::string& outPath)
{
	outPath.clear();

#ifdef _WIN32
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	IFileDialog* pDialog = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
		IID_IFileDialog, (void**)&pDialog);
	if (SUCCEEDED(hr))
	{
		DWORD options = 0;
		pDialog->GetOptions(&options);
		pDialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
		pDialog->SetTitle(L"Select Project Folder");

		hr = pDialog->Show(NULL);
		if (SUCCEEDED(hr))
		{
			IShellItem* pItem = nullptr;
			hr = pDialog->GetResult(&pItem);
			if (SUCCEEDED(hr))
			{
				PWSTR wPath = nullptr;
				pItem->GetDisplayName(SIGDN_FILESYSPATH, &wPath);
				if (wPath)
				{
					int size = WideCharToMultiByte(CP_UTF8, 0, wPath, -1, NULL, 0, NULL, NULL);
					if (size > 0)
					{
						std::string result(size - 1, '\0');
						WideCharToMultiByte(CP_UTF8, 0, wPath, -1, &result[0], size, NULL, NULL);
						outPath = result;
					}
					CoTaskMemFree(wPath);
				}
				pItem->Release();
			}
		}
		pDialog->Release();
	}

	CoUninitialize();
#else
	VANS_LOG_WARN("[ProjectSelector] Folder browser not implemented on this platform");
#endif
}

} // namespace Vans
