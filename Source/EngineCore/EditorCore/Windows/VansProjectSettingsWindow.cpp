#include "VansProjectSettingsWindow.h"

#include "../VansEditorWindow.h"

#include "imgui.h"

#include <algorithm>
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

	bool DrawStringList(const char* id, std::vector<std::string>& values, const char* newValue)
	{
		bool changed = false;
		ImGui::PushID(id);
		for (std::size_t index = 0; index < values.size();)
		{
			ImGui::PushID(static_cast<int>(index));
			char buffer[512]{};
			std::snprintf(buffer, sizeof(buffer), "%s", values[index].c_str());
			ImGui::SetNextItemWidth(-96.0f);
			if (ImGui::InputText("##value", buffer, sizeof(buffer)))
			{
				values[index] = buffer;
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Up") && index > 0)
			{
				std::swap(values[index], values[index - 1]);
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("X"))
			{
				values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
				changed = true;
				ImGui::PopID();
				continue;
			}
			ImGui::PopID();
			++index;
		}
		if (ImGui::SmallButton("+"))
		{
			values.emplace_back(newValue);
			changed = true;
		}
		ImGui::PopID();
		return changed;
	}
}

void VansProjectSettingsWindow::SyncTemplateBuffer()
{
	if (m_GAFConfiguration.templates.empty() ||
		m_GAFTemplateIndex >= m_GAFConfiguration.templates.size() ||
		m_GAFTemplateBuffer.empty()) return;
	auto& document = m_GAFConfiguration.templates[m_GAFTemplateIndex].document;
	document.kind = Vans::EditorAPI::GAFEditorValueKind::Json;
	document.canonicalJson = m_GAFTemplateBuffer.data();
}

void VansProjectSettingsWindow::SelectTemplate(std::size_t index)
{
	SyncTemplateBuffer();
	if (m_GAFConfiguration.templates.empty())
	{
		m_GAFTemplateIndex = 0;
		m_GAFTemplateBuffer.clear();
		return;
	}
	m_GAFTemplateIndex = (std::min)(index, m_GAFConfiguration.templates.size() - 1);
	const std::string& json = m_GAFConfiguration.templates[m_GAFTemplateIndex].document.canonicalJson;
	const std::size_t capacity = (std::max<std::size_t>)(262144, json.size() + 4096);
	m_GAFTemplateBuffer.assign(capacity, '\0');
	std::memcpy(m_GAFTemplateBuffer.data(), json.data(), (std::min)(json.size(), capacity - 1));
}

void VansProjectSettingsWindow::ReloadGAF(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI,
	const std::string& projectRoot)
{
	m_GAFProjectRoot = projectRoot;
	m_GAFConfiguration = editorAPI.GetGAFProjectConfiguration();
	m_GAFResult = {};
	m_GAFTemplateIndex = 0;
	m_GAFTemplateBuffer.clear();
	SelectTemplate(0);
}

void VansProjectSettingsWindow::DrawGAFSettings(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!m_GAFConfiguration.available)
	{
		ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s",
			m_GAFConfiguration.message.c_str());
		if (ImGui::Button("Reload")) ReloadGAF(editorAPI, m_GAFProjectRoot);
		return;
	}
	if (ImGui::Button("Save GAF Configuration"))
	{
		SyncTemplateBuffer();
		m_GAFResult = editorAPI.ApplyGAFProjectConfiguration(m_GAFConfiguration);
		if (m_GAFResult.success)
		{
			m_GAFConfiguration = m_GAFResult.configuration;
			m_GAFTemplateBuffer.clear();
			SelectTemplate((std::min)(m_GAFTemplateIndex,
				m_GAFConfiguration.templates.empty() ? std::size_t{ 0 } :
				m_GAFConfiguration.templates.size() - 1));
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload GAF Configuration")) ReloadGAF(editorAPI, m_GAFProjectRoot);
	if (!m_GAFResult.message.empty())
		ImGui::TextColored(m_GAFResult.success
			? ImVec4(0.45f, 0.85f, 0.55f, 1.0f)
			: ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", m_GAFResult.message.c_str());
	ImGui::TextDisabled("%s", m_GAFConfiguration.settingsDirectory.c_str());
	ImGui::Separator();

	if (!ImGui::BeginTabBar("GAFProjectConfigurationTabs")) return;
	if (ImGui::BeginTabItem("Runtime"))
	{
		ImGui::Checkbox("Deterministic Cook", &m_GAFConfiguration.deterministicCook);
		ImGui::Checkbox("Strip Editor Metadata", &m_GAFConfiguration.stripEditorMetadata);
		ImGui::Checkbox("Treat Cook Warnings As Errors", &m_GAFConfiguration.treatCookWarningsAsErrors);
		char directory[512]{};
		std::snprintf(directory, sizeof(directory), "%s", m_GAFConfiguration.templateDirectory.c_str());
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText("Template Directory", directory, sizeof(directory)))
			m_GAFConfiguration.templateDirectory = directory;
		ImGui::SeparatorText("Performance Budgets");
		const auto budget = [](const char* label, std::uint32_t& value)
		{
			const std::uint32_t step = 1;
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::InputScalar(label, ImGuiDataType_U32, &value, &step) && value == 0) value = 1;
		};
		budget("Active Actions Per Host", m_GAFConfiguration.maximumActiveActionsPerHost);
		budget("Tasks Per Action", m_GAFConfiguration.maximumTasksPerAction);
		budget("Graph Transitions Per Tick", m_GAFConfiguration.maximumGraphTransitionsPerTick);
		budget("Effects Per Host", m_GAFConfiguration.maximumEffectsPerHost);
		budget("Payload Bytes", m_GAFConfiguration.maximumPayloadBytes);
		ImGui::SeparatorText("Default Tag Roots");
		DrawStringList("TagRoots", m_GAFConfiguration.defaultTagRoots, "Gameplay");
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Registries"))
	{
		ImGui::SeparatorText("Node Types");
		DrawStringList("NodeTypes", m_GAFConfiguration.allowedNodeTypes, "Action.Graph.NewNode");
		ImGui::SeparatorText("Modules");
		DrawStringList("Modules", m_GAFConfiguration.allowedModules, "Project.Module");
		ImGui::SeparatorText("Capabilities");
		DrawStringList("Capabilities", m_GAFConfiguration.allowedCapabilities, "Project.Capability");
		ImGui::SeparatorText("Policies");
		DrawStringList("Policies", m_GAFConfiguration.allowedPolicies, "Core.Policy");
		ImGui::SeparatorText("Guards");
		DrawStringList("Guards", m_GAFConfiguration.allowedGuards, "Project.Guard");
		ImGui::SeparatorText("Drivers");
		DrawStringList("Drivers", m_GAFConfiguration.allowedDrivers, "Project.Driver");
		ImGui::SeparatorText("Signals");
		DrawStringList("Signals", m_GAFConfiguration.allowedSignals, "Project.Signal");
		ImGui::SeparatorText("Value Types");
		DrawStringList("ValueTypes", m_GAFConfiguration.allowedValueTypes, "Project.Value");
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Validation"))
	{
		ImGui::SeparatorText("Severity Overrides");
		for (std::size_t index = 0; index < m_GAFConfiguration.severityOverrides.size();)
		{
			auto& entry = m_GAFConfiguration.severityOverrides[index];
			ImGui::PushID(static_cast<int>(index));
			char code[256]{};
			std::snprintf(code, sizeof(code), "%s", entry.name.c_str());
			ImGui::SetNextItemWidth(260.0f);
			if (ImGui::InputText("##code", code, sizeof(code))) entry.name = code;
			ImGui::SameLine();
			if (ImGui::BeginCombo("##severity", entry.value.c_str()))
			{
				for (const char* severity : { "Info", "Warning", "Error", "Fatal" })
					if (ImGui::Selectable(severity, entry.value == severity)) entry.value = severity;
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("X"))
			{
				m_GAFConfiguration.severityOverrides.erase(
					m_GAFConfiguration.severityOverrides.begin() + static_cast<std::ptrdiff_t>(index));
				ImGui::PopID();
				continue;
			}
			ImGui::PopID();
			++index;
		}
		if (ImGui::SmallButton("+##Severity"))
			m_GAFConfiguration.severityOverrides.push_back({ "GAF-NEW-RULE", "Warning" });
		ImGui::SeparatorText("Save Blocking Codes");
		DrawStringList("SaveCodes", m_GAFConfiguration.saveBlockingCodes, "GAF-NEW-SAVE-RULE");
		ImGui::SeparatorText("Cook Blocking Codes");
		DrawStringList("CookCodes", m_GAFConfiguration.cookBlockingCodes, "GAF-NEW-COOK-RULE");
		ImGui::SeparatorText("CI Blocking Codes");
		DrawStringList("CICodes", m_GAFConfiguration.ciBlockingCodes, "GAF-NEW-CI-RULE");
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Templates"))
	{
		if (m_GAFConfiguration.templates.empty()) ImGui::TextDisabled("No templates");
		else
		{
			const char* preview = m_GAFConfiguration.templates[m_GAFTemplateIndex].assetKind.c_str();
			if (ImGui::BeginCombo("Template", preview))
			{
				for (std::size_t index = 0; index < m_GAFConfiguration.templates.size(); ++index)
					if (ImGui::Selectable(m_GAFConfiguration.templates[index].assetKind.c_str(),
						index == m_GAFTemplateIndex)) SelectTemplate(index);
				ImGui::EndCombo();
			}
			ImGui::InputTextMultiline("##GAFTemplateJson", m_GAFTemplateBuffer.data(),
				m_GAFTemplateBuffer.size(), ImVec2(-1.0f, 420.0f), ImGuiInputTextFlags_AllowTabInput);
		}
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
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
		ReloadGAF(editorAPI, snapshot.projectRootPath);
	}

	ImGui::BeginDisabled(!snapshot.dirty);
	if (ImGui::Button("Save Project Documents"))
		s_LastEditResult = editorAPI.SaveProjectDocuments();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled(snapshot.dirty
		? "Unsaved project configuration changes"
		: "Project configuration is saved");
	DrawEditResult(s_LastEditResult);
	ImGui::Separator();

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
			}
			DrawEditResult(s_LastEditResult);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Rendering"))
		{
			const Vans::EditorAPI::UpscalerSettingsSnapshot liveUpscaler =
				editorAPI.GetUpscalerSettings();
			if (!m_UpscalerEditInitialized || !m_UpscalerEditDirty)
			{
				m_UpscalerEdit = liveUpscaler;
				m_UpscalerEditInitialized = true;
			}
			const std::vector<Vans::EditorAPI::UpscalerCapabilitiesSnapshot> capabilities =
				editorAPI.GetUpscalerCapabilities();
			const char* backendNames[] = { "Off", "FSR", "DLSS" };
			const char* qualityNames[] = {
				"Native AA", "Quality", "Balanced", "Performance", "Ultra Performance" };

			int backend = static_cast<int>(m_UpscalerEdit.desiredBackend);
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::Combo("Upscaler Backend", &backend, backendNames, IM_ARRAYSIZE(backendNames)))
			{
				m_UpscalerEdit.desiredBackend =
					static_cast<Vans::EditorAPI::UpscalerBackend>(backend);
				if (m_UpscalerEdit.desiredBackend == Vans::EditorAPI::UpscalerBackend::Off)
					m_UpscalerEdit.desiredQuality =
						Vans::EditorAPI::UpscaleQualityMode::NativeAA;
				m_UpscalerEditDirty = true;
			}
			int quality = static_cast<int>(m_UpscalerEdit.desiredQuality);
			ImGui::SetNextItemWidth(180.0f);
			ImGui::BeginDisabled(
				m_UpscalerEdit.desiredBackend == Vans::EditorAPI::UpscalerBackend::Off);
			if (ImGui::Combo("Quality", &quality, qualityNames, IM_ARRAYSIZE(qualityNames)))
			{
				m_UpscalerEdit.desiredQuality =
					static_cast<Vans::EditorAPI::UpscaleQualityMode>(quality);
				m_UpscalerEditDirty = true;
			}
			ImGui::EndDisabled();

			const bool fsrSelected =
				m_UpscalerEdit.desiredBackend == Vans::EditorAPI::UpscalerBackend::FSR;
			ImGui::BeginDisabled(!fsrSelected);
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::SliderFloat(
				"FSR RCAS Sharpness",
				&m_UpscalerEdit.fsrSharpness,
				0.0f,
				1.0f,
				"%.2f"))
			{
				m_UpscalerEditDirty = true;
			}
			if (ImGui::Checkbox("FSR SDK Debug View", &m_UpscalerEdit.fsrDebugView))
				m_UpscalerEditDirty = true;
			ImGui::EndDisabled();

			int outputResolution[2] = {
				static_cast<int>(m_UpscalerEdit.outputWidth),
				static_cast<int>(m_UpscalerEdit.outputHeight)
			};
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::InputInt2("Final Output Resolution", outputResolution))
			{
				m_UpscalerEdit.outputWidth = static_cast<std::uint32_t>(
					std::max(outputResolution[0], 0));
				m_UpscalerEdit.outputHeight = static_cast<std::uint32_t>(
					std::max(outputResolution[1], 0));
				m_UpscalerEditDirty = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("4K"))
			{
				m_UpscalerEdit.outputWidth = 3840u;
				m_UpscalerEdit.outputHeight = 2160u;
				m_UpscalerEditDirty = true;
			}
			ImGui::TextDisabled(
				"The upscaler quality mode derives the internal base resolution from this target.");

			if (ImGui::Button("Apply Upscaler Settings"))
			{
				const Vans::EditorAPI::ApplyUpscalerSettingsResult apply =
					editorAPI.ApplyUpscalerSettings(m_UpscalerEdit);
				m_UpscalerApplySucceeded = apply.accepted;
				m_UpscalerApplyMessage = apply.message;
				if (apply.accepted)
					m_UpscalerEditDirty = false;
			}
			if (!m_UpscalerApplyMessage.empty())
			{
				ImGui::TextColored(
					m_UpscalerApplySucceeded
						? ImVec4(0.45f, 0.85f, 0.55f, 1.0f)
						: ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
					"%s",
					m_UpscalerApplyMessage.c_str());
			}

			ImGui::TextDisabled(
				"Desired: %s / %s",
				backendNames[static_cast<int>(liveUpscaler.desiredBackend)],
				qualityNames[static_cast<int>(liveUpscaler.desiredQuality)]);
			ImGui::TextDisabled(
				"Effective: %s / %s",
				backendNames[static_cast<int>(liveUpscaler.effectiveBackend)],
				qualityNames[static_cast<int>(liveUpscaler.effectiveQuality)]);
			if (!liveUpscaler.fallbackMessage.empty())
				ImGui::TextWrapped("Fallback: %s", liveUpscaler.fallbackMessage.c_str());
			ImGui::TextDisabled("%ux%u -> %ux%u  bias %.2f",
				liveUpscaler.renderWidth,
				liveUpscaler.renderHeight,
				liveUpscaler.outputWidth,
				liveUpscaler.outputHeight,
				liveUpscaler.mipBias);
			ImGui::TextDisabled("Context: %s, jitter phases %d, dispatch ok/fail %llu/%llu, pending reset 0x%X",
				liveUpscaler.contextReady ? "ready" : "not ready",
				liveUpscaler.jitterPhaseCount,
				static_cast<unsigned long long>(liveUpscaler.successfulDispatchCount),
				static_cast<unsigned long long>(liveUpscaler.failedDispatchCount),
				liveUpscaler.pendingResetReasons);
			ImGui::TextDisabled("Auxiliary %llu, GPU %.2f MiB (aliasable %.2f MiB)",
				static_cast<unsigned long long>(liveUpscaler.auxiliaryDispatchCount),
				static_cast<double>(liveUpscaler.gpuMemoryUsageBytes) / (1024.0 * 1024.0),
				static_cast<double>(liveUpscaler.gpuMemoryAliasableBytes) / (1024.0 * 1024.0));
			ImGui::TextDisabled("Codes: create %u, query %u, dispatch %u, reactive %u",
				liveUpscaler.backendCreateCode,
				liveUpscaler.backendQueryCode,
				liveUpscaler.backendDispatchCode,
				liveUpscaler.backendAuxiliaryCode);
			if (!liveUpscaler.lastError.empty())
				ImGui::TextWrapped("Last upscaler error: %s (code %u)",
					liveUpscaler.lastError.c_str(), liveUpscaler.backendDispatchCode);

			for (const auto& capability : capabilities)
			{
				if (capability.backend == m_UpscalerEdit.desiredBackend &&
					!capability.unavailableReason.empty())
				ImGui::TextWrapped("Availability: %s", capability.unavailableReason.c_str());
			}

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
			bool asyncCompute = commandRecording.asyncComputeRequested;
			if (ImGui::Checkbox("Async Compute", &asyncCompute))
			{
				commandRecording.asyncComputeRequested = asyncCompute;
				editorAPI.SetCommandRecordingSettings(commandRecording);
			}
			ImGui::TextDisabled(
				commandRecording.asyncComputeEnabled
					? "独立 Compute Queue 已启用，GI 等计算任务将自动并行。"
					: (commandRecording.hasDedicatedAsyncComputeQueue
						? "设备支持独立 Compute Queue，当前未启用。"
						: (commandRecording.asyncComputeRequested
							? "设备没有独立 Compute Queue，已自动使用兼容渲染路径。"
							: "设备没有独立 Compute Queue。")));
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

		if (ImGui::BeginTabItem("GAF"))
		{
			DrawGAFSettings(editorAPI);
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
