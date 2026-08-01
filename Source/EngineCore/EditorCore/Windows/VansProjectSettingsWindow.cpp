#include "VansProjectSettingsWindow.h"

#include "../VansEditorWindow.h"

#include "imgui.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace VansGraphics
{
namespace
{
	template <std::size_t Size>
	void CopyToBuffer(std::array<char, Size>& buffer, const std::string& value)
	{
		buffer.fill('\0');
		std::strncpy(buffer.data(), value.c_str(), Size - 1);
	}

	const char* SeverityLabel(Vans::EditorAPI::ProjectConfigDiagnosticSeverity severity)
	{
		switch (severity)
		{
		case Vans::EditorAPI::ProjectConfigDiagnosticSeverity::Warning:
			return "Warning";
		case Vans::EditorAPI::ProjectConfigDiagnosticSeverity::Error:
			return "Error";
		case Vans::EditorAPI::ProjectConfigDiagnosticSeverity::Info:
		default:
			return "Info";
		}
	}

	void DrawEditResult(const Vans::EditorAPI::ProjectConfigEditResult& result)
	{
		if (result.message.empty())
			return;

		if (result.success)
			ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "%s", result.message.c_str());
		else
			ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", result.message.c_str());
	}
}

void VansProjectSettingsWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!VansEditorWindow::m_ProjectSettingsWindowOpen)
		return;

	if (!ImGui::Begin("Project Settings", &VansEditorWindow::m_ProjectSettingsWindowOpen))
	{
		ImGui::End();
		return;
	}

	const Vans::EditorAPI::ProjectConfigSnapshot snapshot =
		editorAPI.GetProjectConfigSnapshot();
	if (!snapshot.projectLoaded)
	{
		ImGui::TextDisabled("No project loaded");
		ImGui::End();
		return;
	}

	static std::string s_LastProjectRoot;
	static std::array<char, 260> s_DefaultScene{};
	static std::array<char, 260> s_AssetsRoot{};
	static std::array<char, 260> s_ArtifactRoot{};
	static std::array<char, 260> s_RenderSettings{};
	static std::array<char, 260> s_PhysicsSettings{};
	static std::array<char, 260> s_CollisionLayers{};
	static float s_FixedTimeStep = 1.0f / 60.0f;
	static Vans::EditorAPI::ProjectConfigEditResult s_LastEditResult;

	if (s_LastProjectRoot != snapshot.projectRootPath)
	{
		s_LastProjectRoot = snapshot.projectRootPath;
		CopyToBuffer(s_DefaultScene, snapshot.defaultScene);
		CopyToBuffer(s_AssetsRoot, snapshot.assetsRoot);
		CopyToBuffer(s_ArtifactRoot, snapshot.importedArtifactRoot);
		CopyToBuffer(s_RenderSettings, snapshot.renderSettingsPath);
		CopyToBuffer(s_PhysicsSettings, snapshot.physicsSettingsPath);
		CopyToBuffer(s_CollisionLayers, snapshot.collisionLayerSettingsPath);
		s_FixedTimeStep = editorAPI.GetProjectPhysicsFixedTimeStep();
		if (s_FixedTimeStep <= 0.0f)
			s_FixedTimeStep = 1.0f / 60.0f;
		s_LastEditResult = {};
	}

	if (ImGui::BeginTabBar("ProjectSettingsTabs"))
	{
		if (ImGui::BeginTabItem("General"))
		{
			ImGui::Text("Name: %s", snapshot.projectName.c_str());
			ImGui::Text("Root: %s", snapshot.projectRootPath.c_str());
			ImGui::Text("Engine: %s", snapshot.engineVersion.c_str());
			ImGui::Separator();

			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("Default Scene", s_DefaultScene.data(), s_DefaultScene.size());
			if (ImGui::Button("Apply Default Scene"))
			{
				s_LastEditResult = editorAPI.SetProjectDefaultScene(s_DefaultScene.data());
				if (s_LastEditResult.success)
					s_LastEditResult = editorAPI.SaveProjectConfig();
			}
			DrawEditResult(s_LastEditResult);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Paths"))
		{
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("Assets Root", s_AssetsRoot.data(), s_AssetsRoot.size());
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("Artifact Root", s_ArtifactRoot.data(), s_ArtifactRoot.size());
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("Render Settings", s_RenderSettings.data(), s_RenderSettings.size());
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("Physics Settings", s_PhysicsSettings.data(), s_PhysicsSettings.size());
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("Collision Layers", s_CollisionLayers.data(), s_CollisionLayers.size());

			if (ImGui::Button("Apply Paths"))
			{
				s_LastEditResult = editorAPI.SetProjectPathField(
					Vans::EditorAPI::ProjectPathField::AssetsRoot,
					s_AssetsRoot.data());
				if (s_LastEditResult.success)
					s_LastEditResult = editorAPI.SetProjectPathField(
						Vans::EditorAPI::ProjectPathField::ImportedArtifactRoot,
						s_ArtifactRoot.data());
				if (s_LastEditResult.success)
					s_LastEditResult = editorAPI.SetProjectPathField(
						Vans::EditorAPI::ProjectPathField::RenderSettings,
						s_RenderSettings.data());
				if (s_LastEditResult.success)
					s_LastEditResult = editorAPI.SetProjectPathField(
						Vans::EditorAPI::ProjectPathField::PhysicsSettings,
						s_PhysicsSettings.data());
				if (s_LastEditResult.success)
					s_LastEditResult = editorAPI.SetProjectPathField(
						Vans::EditorAPI::ProjectPathField::CollisionLayerSettings,
						s_CollisionLayers.data());
				if (s_LastEditResult.success)
					s_LastEditResult = editorAPI.SaveProjectConfig();
			}
			DrawEditResult(s_LastEditResult);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Rendering"))
		{
			Vans::EditorAPI::FSRSettingsSnapshot fsr = editorAPI.GetFSRSettings();
			const char* fsrModeNames[] = { "Viewport", "Native AA", "Quality 1.5x", "Performance 2x" };
			int fsrMode = static_cast<int>(fsr.mode);
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::Combo("FSR Mode", &fsrMode, fsrModeNames, IM_ARRAYSIZE(fsrModeNames)))
			{
				fsr.mode = static_cast<Vans::EditorAPI::FSRUpscaleMode>(fsrMode);
				editorAPI.SetFSRSettings(fsr.mode, fsr.sharpness);
			}
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::SliderFloat("Sharpness", &fsr.sharpness, 0.0f, 1.0f, "%.2f"))
				editorAPI.SetFSRSettings(fsr.mode, fsr.sharpness);
			ImGui::TextDisabled("%ux%u -> %ux%u  bias %.2f",
				fsr.renderWidth,
				fsr.renderHeight,
				fsr.outputWidth,
				fsr.outputHeight,
				fsr.mipBias);

			ImGui::Separator();
			Vans::EditorAPI::CommandRecordingSettingsSnapshot commandRecording =
				editorAPI.GetCommandRecordingSettings();
			bool parallelRecording = commandRecording.parallelEnabled;
			if (ImGui::Checkbox("Parallel Command Recording", &parallelRecording))
			{
				commandRecording.parallelEnabled = parallelRecording;
				editorAPI.SetCommandRecordingSettings(commandRecording);
			}
			bool frameContextRing = commandRecording.frameContextRingEnabled;
			if (ImGui::Checkbox("Frame Context Ring", &frameContextRing))
			{
				commandRecording.frameContextRingEnabled = frameContextRing;
				if (commandRecording.framesInFlight < 2)
					commandRecording.framesInFlight = 2;
				editorAPI.SetCommandRecordingSettings(commandRecording);
			}
			int framesInFlight = static_cast<int>(commandRecording.framesInFlight);
			ImGui::BeginDisabled(!commandRecording.frameContextRingEnabled);
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::SliderInt("Frames In Flight", &framesInFlight, 1, 2))
			{
				commandRecording.framesInFlight = static_cast<std::uint32_t>(framesInFlight);
				editorAPI.SetCommandRecordingSettings(commandRecording);
			}
			ImGui::EndDisabled();
			ImGui::TextDisabled("关闭 Frame Context Ring 时会回到旧的 CPU 等待提交路径。");
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Physics"))
		{
			ImGui::SetNextItemWidth(160.0f);
			ImGui::InputFloat("Fixed Timestep", &s_FixedTimeStep, 0.001f, 0.008333f, "%.6f");
			const float simulationHz = s_FixedTimeStep > 0.0f ? 1.0f / s_FixedTimeStep : 0.0f;
			ImGui::TextDisabled("%.2f Hz", simulationHz);
			if (ImGui::Button("Apply Physics"))
			{
				s_LastEditResult = editorAPI.SetProjectPhysicsFixedTimeStep(s_FixedTimeStep);
				if (s_LastEditResult.success)
					s_FixedTimeStep = editorAPI.GetProjectPhysicsFixedTimeStep();
			}
			DrawEditResult(s_LastEditResult);

			ImGui::Separator();
			const std::vector<std::string> layerNames = editorAPI.GetRuntimeCollisionLayerNames();
			if (ImGui::BeginTable("ProjectCollisionLayers", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("Index");
				ImGui::TableSetupColumn("Layer");
				ImGui::TableHeadersRow();
				for (std::size_t index = 0; index < layerNames.size(); ++index)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("%zu", index);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(layerNames[index].c_str());
				}
				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Diagnostics"))
		{
			if (snapshot.diagnostics.empty())
			{
				ImGui::TextDisabled("No project config diagnostics");
			}
			else if (ImGui::BeginTable("ProjectConfigDiagnostics", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("Severity");
				ImGui::TableSetupColumn("Field");
				ImGui::TableSetupColumn("Message");
				ImGui::TableHeadersRow();
				for (const Vans::EditorAPI::ProjectConfigDiagnostic& diagnostic : snapshot.diagnostics)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(SeverityLabel(diagnostic.severity));
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(diagnostic.propertyPointer.c_str());
					ImGui::TableNextColumn();
					ImGui::TextWrapped("%s", diagnostic.message.c_str());
				}
				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
}
}
