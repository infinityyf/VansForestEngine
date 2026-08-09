#include "VansAnimGraphEditorWindow.h"
#include "../VansAssetDocumentEditService.h"
#include "../VansAssetDocumentRegistry.h"
#include "../VansEditorAssetSaveService.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../../Util/VansLog.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace VansGraphics
{
	using AnimatorAssetData = Vans::EditorAPI::AnimatorDocumentDTO;
	using AnimatorGraphAsset = Vans::EditorAPI::AnimatorGraphDTO;
	using AnimatorParameter = Vans::EditorAPI::AnimatorParameterDTO;
	using AnimatorClipRef = Vans::EditorAPI::AnimatorClipRefDTO;
	using AnimatorState = Vans::EditorAPI::AnimatorStateDTO;
	using AnimatorTransition = Vans::EditorAPI::AnimatorTransitionDTO;
	using TransitionCondition = Vans::EditorAPI::TransitionConditionDTO;
	using AnimatorParamType = Vans::EditorAPI::AnimatorParamType;
	using VansAnimGraph = Vans::EditorAPI::AnimationGraphDTO;
	using VansAnimGraphNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphClipNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphBlendNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphBlend1DNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphIfConditionNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphSwitchNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphAdditiveBlendNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphSpeedScaleNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphStateMachineNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphMotionMatchingNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphSlotNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphIKNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphTwoBoneIKNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphLookAtNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphFootPlacementNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphNodeType = Vans::EditorAPI::AnimGraphNodeType;
	using AnimGraphPin = Vans::EditorAPI::AnimGraphPinDTO;
	using AnimGraphPinKind = Vans::EditorAPI::AnimGraphPinKind;
	using CompareOp = Vans::EditorAPI::CompareOp;
	using VansAnimationLayerDefinition = Vans::EditorAPI::AnimationLayerDTO;
	using VansAnimationSlotDefinition = Vans::EditorAPI::AnimationSlotDTO;
	using VansAnimationLayerKind = Vans::EditorAPI::VansAnimationLayerKind;
	using VansLayerBlendMode = Vans::EditorAPI::VansLayerBlendMode;
	using VansRotationBlendSpace = Vans::EditorAPI::VansRotationBlendSpace;
	using VansAdditiveReferenceMode = Vans::EditorAPI::VansAdditiveReferenceMode;
	using VansLayerRootMotionMode = Vans::EditorAPI::VansLayerRootMotionMode;
	using VansLayerCurveMode = Vans::EditorAPI::VansLayerCurveMode;
	using VansLayerEventMode = Vans::EditorAPI::VansLayerEventMode;
	using VansLayerNodeTrackMode = Vans::EditorAPI::VansLayerNodeTrackMode;
	using VansLayerSyncMode = Vans::EditorAPI::VansLayerSyncMode;
	using VansSlotConcurrency = Vans::EditorAPI::VansSlotConcurrency;
	using IKSolverType = Vans::EditorAPI::IKSolverType;
	using IKProfileType = Vans::EditorAPI::IKProfileType;
	using IKCoordinateSpace = Vans::EditorAPI::IKCoordinateSpace;
	using JointConstraintType = Vans::EditorAPI::JointConstraintType;
	using IKBoneLink = Vans::EditorAPI::IKBoneLinkDTO;
	using FootPlacementSettings = Vans::EditorAPI::FootPlacementSettingsDTO;
}

using namespace VansGraphics;
namespace ne = ax::NodeEditor;
namespace
{
	struct DecodedAnimGraphPin
	{
		int nodeId = -1;
		int pinIndex = -1;
		bool output = false;
	};

	bool DecodeAnimGraphPin(ne::PinId id, DecodedAnimGraphPin& decoded)
	{
		const int raw = static_cast<int>(id.Get());
		if (raw <= 0)
			return false;
		decoded.nodeId = raw / 1000;
		const int local = raw % 1000;
		if (decoded.nodeId <= 0 || local <= 0)
			return false;
		decoded.output = local > 500;
		decoded.pinIndex = decoded.output ? local - 501 : local - 1;
		return decoded.pinIndex >= 0;
	}

	bool EditStringProperty(const char* label, std::string& value)
	{
		char buffer[256]{};
		strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
		if (!ImGui::InputText(label, buffer, sizeof(buffer)))
			return false;
		value = buffer;
		return true;
	}
}
namespace VansGraphics
{
	struct AnimGraphEditState
	{
		std::string name;
		std::vector<AnimatorParameter> parameters;
		std::vector<AnimatorClipRef> clipRefs;
		std::string selectedLayerId;
		std::string selectedSlotId;
		std::unordered_set<std::string> mutedLayerIds;
		std::unordered_set<std::string> soloLayerIds;
		bool isDirty = false;
		bool needsInitialLayout = false;
		int selectedNodeId = -1;
		int selectedLinkId = -1;
	};
}
// ============================================================================
// ============================================================================
VansAnimGraphEditorWindow::VansAnimGraphEditorWindow()
	: m_EditState(std::make_unique<AnimGraphEditState>())
{
}
VansAnimGraphEditorWindow::~VansAnimGraphEditorWindow()
{
	DestroyPreviewSession();
	if (m_NodeEditorCtx)
	{
		ne::DestroyEditor(m_NodeEditorCtx);
		m_NodeEditorCtx = nullptr;
	}
}
// ============================================================================
//  Open / Close
// ============================================================================
void VansAnimGraphEditorWindow::Open(const std::string& animatorFilePath)
{
	if (m_IsOpen && (m_EditState->isDirty || (m_Document && m_Document->IsDirty())))
	{
		m_LastError = "Save or discard the current Animator before opening another asset";
		VANS_LOG_WARN("[AnimGraphEditor] " << m_LastError);
		return;
	}
	CloseImmediately();
	m_AnimatorFilePath = animatorFilePath;
	m_Document = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(m_AnimatorFilePath);
	if (!m_Document || !m_Document->sourceDocument.IsLoaded())
	{
		m_LastError = m_Document ? m_Document->lastError : "Failed to open asset document";
		VANS_LOG_ERROR("[AnimGraphEditor] " << m_LastError);
		m_AssetData.reset();
		m_Document.reset();
		return;
	}
	m_AssetData.reset();
	m_TargetGraph = nullptr;
	m_ActiveGraphId.clear();
	m_NeedsDecode = true;
	m_EditState->name.clear();
	m_EditState->parameters.clear();
	m_EditState->clipRefs.clear();
	m_EditState->selectedLayerId.clear();
	m_EditState->selectedSlotId.clear();
	m_EditState->mutedLayerIds.clear();
	m_EditState->soloLayerIds.clear();
	m_EditState->selectedNodeId = -1;
	m_EditState->selectedLinkId = -1;
	m_SelectedStateIndex = -1;
	m_SelectedTransitionIndex = -1;
	m_SelectedConditionIndex = -1;
	m_EditState->isDirty = false;
	m_EditState->needsInitialLayout = true;
	m_IsOpen = true;
	m_CloseRequested = false;
	if (m_NodeEditorCtx)
		ne::DestroyEditor(m_NodeEditorCtx);
	ne::Config config;
	config.SettingsFile          = nullptr;
	config.NavigateButtonIndex   = 2;
	config.ContextMenuButtonIndex = 1;       // 鍙抽敭鑿滃崟
	m_NodeEditorCtx = ne::CreateEditor(&config);
	m_NavigationStack.clear();
	m_PreviewRevision = 0;
	m_PreviewDocumentStateId = 0;
	m_PreviewCompilePending = true;
	m_PreviewCompileQueuedAt = 0.0;
	m_PreviewPlaying = true;
	m_PreviewSpeed = 1.0f;
	m_PreviewVisualizedLayer = -1;
	m_PreviewRootMotionMode = Vans::EditorAPI::AnimationPreviewPlaybackRequest::RootMotionMode::InPlace;
	m_PreviewFloats.clear();
	m_PreviewBools.clear();
	m_PreviewInts.clear();
	m_PreviewVectors.clear();
	m_PreviewParameterTypes.clear();
}
void VansAnimGraphEditorWindow::Close()
{
	if (m_NodeEditorCtx)
	{
		ne::SetCurrentEditor(m_NodeEditorCtx);
		ReadNodePositions();
		ne::SetCurrentEditor(nullptr);
	}
	if (m_EditState->isDirty || (m_Document && m_Document->IsDirty()))
	{
		m_CloseRequested = true;
		m_IsOpen = true;
		return;
	}
	CloseImmediately();
}

void VansAnimGraphEditorWindow::CloseImmediately()
{
	DestroyPreviewSession();
	if (m_NodeEditorCtx)
	{
		ne::DestroyEditor(m_NodeEditorCtx);
		m_NodeEditorCtx = nullptr;
	}
	m_TargetGraph = nullptr;
	m_ActiveGraphId.clear();
	m_AssetData.reset();
	m_Document.reset();
	m_DocumentStateId = 0;
	m_ActiveAPI = nullptr;
	m_AnimatorFilePath.clear();
	m_NavigationStack.clear();
	m_CloseRequested = false;
	m_NeedsDecode = false;
	m_IsOpen = false;
}
// ============================================================================
// ============================================================================
void VansAnimGraphEditorWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!m_IsOpen) return;
	m_ActiveAPI = &editorAPI;
	if (m_NeedsDecode)
	{
		if (!ReloadWorkingCopyFromDocument())
		{
			VANS_LOG_ERROR("[AnimGraphEditor] " << m_LastError);
			CloseImmediately();
			return;
		}
		m_NeedsDecode = false;
		ResetPreviewParameters();
	}
	if (!m_AssetData || !m_TargetGraph || !m_Document)
	{
		CloseImmediately();
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(1200, 700), ImGuiCond_FirstUseEver);
	char title[256];
	const bool documentDirty = m_Document && m_Document->IsDirty();
	snprintf(title, sizeof(title), "Animation Graph Editor - %s%s###AnimGraphEditor",
	         m_EditState->name.c_str(),
	         (m_EditState->isDirty || documentDirty) ? " *" : "");
	const bool windowVisible = ImGui::Begin(title, &m_IsOpen, ImGuiWindowFlags_MenuBar);
	if (!m_IsOpen)
	{
		m_IsOpen = true;
		Close();
		if (!m_IsOpen)
		{
			ImGui::End();
			return;
		}
	}
	if (!windowVisible)
	{
		ImGui::End();
		return;
	}
	if (!m_EditState->isDirty
		&& m_DocumentStateId != m_Document->sourceDocument.CurrentStateId())
		ReloadWorkingCopyFromDocument();
	EnsurePreviewSession();
	// ============================================================================
	// ?????????????????????
	if (m_CloseRequested)
		ImGui::OpenPopup("UnsavedChanges");
	if (ImGui::BeginPopupModal("UnsavedChanges", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("You have unsaved changes. Save before closing?");
		if (ImGui::Button("Save") && Save()) { ImGui::CloseCurrentPopup(); CloseImmediately(); }
		ImGui::SameLine();
		if (ImGui::Button("Discard"))
		{
			if (!m_Document || Vans::VansAssetDocumentEditService::RevertToSaved(m_Document->sourceDocument))
			{
				ImGui::CloseCurrentPopup();
				CloseImmediately();
			}
			else
				m_LastError = "Unable to discard Animator document edits";
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) { m_CloseRequested = false; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	DrawMenuBar();
	DrawNavigationBar();
	float leftPanelWidth = 220.0f;
	ImGui::BeginChild("LeftPanel", ImVec2(leftPanelWidth, -ImGui::GetFrameHeightWithSpacing()),
	                   ImGuiChildFlags_Borders);
	DrawLeftPanel();
	ImGui::EndChild();
	ImGui::SameLine();
    // 画布区域
	ImGui::BeginChild("AnimationWorkspace", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
	const float previewHeight = 285.0f;
	ImGui::BeginChild("GraphCanvas", ImVec2(0,
		std::max(180.0f, ImGui::GetContentRegionAvail().y - previewHeight)));
	DrawGraphCanvas();
	ImGui::EndChild();
	ImGui::BeginChild("AnimationPreviewPanel", ImVec2(0, previewHeight), ImGuiChildFlags_Borders);
	DrawPreviewPanel();
	ImGui::EndChild();
	ImGui::EndChild();
	DrawStatusBar();
	if (m_EditState->isDirty && !ImGui::IsAnyItemActive())
		CommitWorkingCopyToDocument();
	if (m_Document && m_PreviewDocumentStateId != m_Document->sourceDocument.CurrentStateId())
		QueuePreviewCompile();
	if (m_PreviewCompilePending && ImGui::GetTime() - m_PreviewCompileQueuedAt >= 0.15)
		UpdatePreviewDefinition();
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
	{
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
			Save();
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
			Undo();
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
			Redo();
		if (IsRootGraphView() && ImGui::IsKeyPressed(ImGuiKey_F))
			ne::NavigateToContent();
	}
	ImGui::End();
}
// ============================================================================
// DrawGraphCanvas - imgui-node-editor 即时模式渲染
// ============================================================================
void VansAnimGraphEditorWindow::DrawGraphCanvas()
{
	if (!IsRootGraphView())
	{
		DrawSubgraphPreviewCanvas();
		return;
	}
	ne::SetCurrentEditor(m_NodeEditorCtx);
	ne::Begin("AnimGraphCanvas");
	if (m_EditState->needsInitialLayout)
	{
		ApplyNodePositions();
		m_EditState->needsInitialLayout = false;
		ne::NavigateToContent(0.0f);
	}
	DrawGraphEditorCanvas();
	ne::End();
	ne::SetCurrentEditor(nullptr);
}
// ============================================================================
//  宸︿晶闈㈡澘
// ============================================================================
void VansAnimGraphEditorWindow::DrawLeftPanel()
{
	ImGui::Text("Animator");
	ImGui::Separator();
	if (EditStringProperty("Name", m_EditState->name))
		m_EditState->isDirty = true;
	ImGui::Spacing();
	DrawLayersPanel();
	ImGui::Spacing();
	DrawSlotsPanel();
	ImGui::Spacing();
	DrawParametersPanel();
	ImGui::Spacing();
	ImGui::Spacing();
	DrawClipsPanel();
	ImGui::Spacing();
	ImGui::Spacing();
	DrawPropertiesPanel();
}
void VansAnimGraphEditorWindow::DrawParametersPanel()
{
	ImGui::Text("Parameters");
	ImGui::Separator();
	for (int i = 0; i < (int)m_EditState->parameters.size(); ++i)
	{
		auto& param = m_EditState->parameters[i];
		ImGui::PushID(i);
		const char* typeNames[] = { "float", "bool", "int", "trigger", "vector3", "quaternion" };
		ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "[%s]",
		                   typeNames[(int)param.type]);
		ImGui::SameLine();
		char nameBuf[128];
		strncpy(nameBuf, param.name.c_str(), sizeof(nameBuf));
		nameBuf[sizeof(nameBuf) - 1] = '\0';
		ImGui::SetNextItemWidth(100.0f);
		if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
		{
			param.name = nameBuf;
			m_EditState->isDirty = true;
		}
		// ============================================================================
		// ============================================================================
		// ?????
		switch (param.type)
		{
		case AnimatorParamType::Float:
			ImGui::SetNextItemWidth(60.0f);
			if (ImGui::DragFloat("##val", &param.floatVal, 0.01f))
				m_EditState->isDirty = true;
			break;
		case AnimatorParamType::Bool:
			if (ImGui::Checkbox("##val", &param.boolVal))
				m_EditState->isDirty = true;
			break;
		case AnimatorParamType::Int:
			ImGui::SetNextItemWidth(60.0f);
			if (ImGui::InputInt("##val", &param.intVal))
				m_EditState->isDirty = true;
			break;
		case AnimatorParamType::Trigger:
			ImGui::TextDisabled("(auto-reset)");
			break;
		case AnimatorParamType::Vector3:
			ImGui::SetNextItemWidth(150.0f);
			if (ImGui::DragFloat3("##val", &param.vec3Val.x, 0.01f))
				m_EditState->isDirty = true;
			break;
		case AnimatorParamType::Quaternion:
		{
			float q[4] = { param.quatVal.x, param.quatVal.y, param.quatVal.z, param.quatVal.w };
			ImGui::SetNextItemWidth(190.0f);
			if (ImGui::DragFloat4("##val", q, 0.01f))
			{
				const float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
				if (length > 1.0e-6f)
					param.quatVal = { q[0] / length, q[1] / length, q[2] / length, q[3] / length };
				else
					param.quatVal = {};
				m_EditState->isDirty = true;
			}
			break;
		}
		}
		// 鍒犻櫎鎸夐挳
		ImGui::SameLine();
		if (ImGui::SmallButton("X"))
		{
			m_EditState->parameters.erase(m_EditState->parameters.begin() + i);
			m_EditState->isDirty = true;
			--i;
		}
		ImGui::PopID();
	}
	// ============================================================================
	// ????
	if (ImGui::Button("+ Add Parameter"))
		ImGui::OpenPopup("AddParamPopup");
	if (ImGui::BeginPopup("AddParamPopup"))
	{
		static char newParamName[128] = "";
		static int newParamType = 0;
		ImGui::InputText("Name", newParamName, sizeof(newParamName));
		ImGui::Combo("Type", &newParamType, "Float\0Bool\0Int\0Trigger\0Vector3\0Quaternion\0");
		if (ImGui::Button("Add") && strlen(newParamName) > 0)
		{
			AnimatorParameter p;
			p.name = newParamName;
			p.type = (AnimatorParamType)newParamType;
			m_EditState->parameters.push_back(p);
			m_EditState->isDirty = true;
			newParamName[0] = '\0';
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
void VansAnimGraphEditorWindow::DrawClipsPanel()
{
	ImGui::Text("Clips");
	ImGui::Separator();
	for (int i = 0; i < (int)m_EditState->clipRefs.size(); ++i)
	{
		auto& clip = m_EditState->clipRefs[i];
		ImGui::PushID(i + 1000);
		bool selected = false;
		ImGui::Selectable(clip.name.c_str(), &selected);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s\nGUID: %s", clip.pathHint.c_str(), clip.assetGuid.c_str());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
		if (ImGui::SmallButton("X"))
		{
			m_EditState->clipRefs.erase(m_EditState->clipRefs.begin() + i);
			m_EditState->isDirty = true;
			--i;
		}
		ImGui::PopID();
	}
	if (ImGui::Button("+ Add Clip"))
		ImGui::OpenPopup("AddClipPopup");
	if (ImGui::BeginPopup("AddClipPopup"))
	{
		static char newClipName[128] = "";
		static char newClipPath[256] = "";
		ImGui::InputText("Name", newClipName, sizeof(newClipName));
		ImGui::InputText("Path (.vclip)", newClipPath, sizeof(newClipPath));
		if (ImGui::Button("Add") && strlen(newClipName) > 0 && strlen(newClipPath) > 0)
		{
			const Vans::EditorAPI::AssetDragPayload asset = m_ActiveAPI
				? m_ActiveAPI->CreateAssetDragPayload(newClipPath)
				: Vans::EditorAPI::AssetDragPayload{};
			if (!asset.available || asset.assetType != Vans::EditorAPI::AssetType::AnimationClip
				|| asset.guid.empty())
			{
				m_LastError = asset.error.empty()
					? "Clip path must resolve to a registered Animation Clip asset"
					: asset.error;
			}
			else
			{
				AnimatorClipRef ref;
				ref.name = newClipName;
				ref.assetGuid = asset.guid;
				ref.pathHint = newClipPath;
				m_EditState->clipRefs.push_back(std::move(ref));
				m_EditState->isDirty = true;
				newClipName[0] = '\0';
				newClipPath[0] = '\0';
				m_LastError.clear();
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}
}
// ============================================================================
// ============================================================================
// ============================================================================
void VansAnimGraphEditorWindow::DrawMenuBar()
{
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Save", "Ctrl+S"))
				Save();
			ImGui::Separator();
			if (ImGui::MenuItem("Close"))
				Close();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View"))
		{
			if (!IsRootGraphView())
				ImGui::BeginDisabled();
			if (ImGui::MenuItem("Fit All Nodes"))
				ne::NavigateToContent();
			if (ImGui::MenuItem("Fit Selected"))
				ne::NavigateToSelection(true);
			if (!IsRootGraphView())
				ImGui::EndDisabled();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
			const bool canUndo = m_EditState->isDirty || (m_Document
				&& Vans::VansAssetDocumentEditService::CanUndo(m_Document->sourceDocument));
			const bool canRedo = m_Document
				&& Vans::VansAssetDocumentEditService::CanRedo(m_Document->sourceDocument);
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) Undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo)) Redo();
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
}
// ============================================================================
// ============================================================================
void VansAnimGraphEditorWindow::DrawStatusBar()
{
	ImGui::Separator();
	if (m_EditState->isDirty || (m_Document && m_Document->IsDirty()))
		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Modified");
	else
		ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Saved");
	ImGui::SameLine(200);
	if (m_TargetGraph)
	{
		ImGui::Text("Nodes: %d  Links: %d",
		            (int)m_TargetGraph->GetNodes().size(),
		            (int)m_TargetGraph->GetLinks().size());
	}
	if (!m_LastError.empty())
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s", m_LastError.c_str());
	}
}

void VansAnimGraphEditorWindow::DestroyPreviewSession()
{
	if (m_PreviewSessionId != 0 && m_ActiveAPI)
		m_ActiveAPI->DestroyAnimationPreview(m_PreviewSessionId);
	m_PreviewSessionId = 0;
	m_PreviewSessionModelGuid.clear();
}

void VansAnimGraphEditorWindow::ResetPreviewParameters()
{
	m_PreviewFloats.clear();
	m_PreviewBools.clear();
	m_PreviewInts.clear();
	m_PreviewVectors.clear();
	m_PreviewParameterTypes.clear();
	if (!m_AssetData)
		return;
	for (const AnimatorParameter& parameter : m_AssetData->parameters)
	{
		m_PreviewParameterTypes[parameter.name] = parameter.type;
		switch (parameter.type)
		{
		case AnimatorParamType::Float: m_PreviewFloats[parameter.name] = parameter.floatVal; break;
		case AnimatorParamType::Bool: m_PreviewBools[parameter.name] = parameter.boolVal; break;
		case AnimatorParamType::Int: m_PreviewInts[parameter.name] = parameter.intVal; break;
		case AnimatorParamType::Trigger: break;
		case AnimatorParamType::Vector3:
			m_PreviewVectors[parameter.name] = {
				parameter.vec3Val.x, parameter.vec3Val.y, parameter.vec3Val.z, 0.0f };
			break;
		case AnimatorParamType::Quaternion:
			m_PreviewVectors[parameter.name] = {
				parameter.quatVal.x, parameter.quatVal.y, parameter.quatVal.z, parameter.quatVal.w };
			break;
		}
	}
	m_PreviewSelectedSlotId = m_AssetData->slots.empty() ? "" : m_AssetData->slots.front().id;
	m_PreviewSelectedClipName = m_AssetData->clipRefs.empty() ? "" : m_AssetData->clipRefs.front().name;
}

void VansAnimGraphEditorWindow::ReconcilePreviewParameters()
{
	const auto previousFloats = m_PreviewFloats;
	const auto previousBools = m_PreviewBools;
	const auto previousInts = m_PreviewInts;
	const auto previousVectors = m_PreviewVectors;
	const auto previousTypes = m_PreviewParameterTypes;
	ResetPreviewParameters();
	if (!m_AssetData)
		return;
	for (const AnimatorParameter& parameter : m_AssetData->parameters)
	{
		const auto previousType = previousTypes.find(parameter.name);
		if (previousType == previousTypes.end() || previousType->second != parameter.type)
			continue;
		switch (parameter.type)
		{
		case AnimatorParamType::Float:
			if (const auto found = previousFloats.find(parameter.name); found != previousFloats.end())
				m_PreviewFloats[parameter.name] = found->second;
			break;
		case AnimatorParamType::Bool:
			if (const auto found = previousBools.find(parameter.name); found != previousBools.end())
				m_PreviewBools[parameter.name] = found->second;
			break;
		case AnimatorParamType::Int:
			if (const auto found = previousInts.find(parameter.name); found != previousInts.end())
				m_PreviewInts[parameter.name] = found->second;
			break;
		case AnimatorParamType::Vector3:
		case AnimatorParamType::Quaternion:
			if (const auto found = previousVectors.find(parameter.name); found != previousVectors.end())
				m_PreviewVectors[parameter.name] = found->second;
			break;
		case AnimatorParamType::Trigger:
			break;
		}
	}
}

void VansAnimGraphEditorWindow::ApplyPreviewParameters()
{
	if (!m_ActiveAPI || !m_AssetData || m_PreviewSessionId == 0)
		return;
	for (const AnimatorParameter& parameter : m_AssetData->parameters)
	{
		Vans::EditorAPI::AnimationPreviewParameterValue value;
		value.sessionId = m_PreviewSessionId;
		value.name = parameter.name;
		switch (parameter.type)
		{
		case AnimatorParamType::Float:
			value.type = Vans::EditorAPI::AnimationPreviewParameterType::Float;
			value.floatValue = m_PreviewFloats.at(parameter.name);
			break;
		case AnimatorParamType::Bool:
			value.type = Vans::EditorAPI::AnimationPreviewParameterType::Bool;
			value.boolValue = m_PreviewBools.at(parameter.name);
			break;
		case AnimatorParamType::Int:
			value.type = Vans::EditorAPI::AnimationPreviewParameterType::Int;
			value.intValue = m_PreviewInts.at(parameter.name);
			break;
		case AnimatorParamType::Vector3:
		{
			value.type = Vans::EditorAPI::AnimationPreviewParameterType::Vector3;
			const auto& vector = m_PreviewVectors.at(parameter.name);
			value.vectorValue = { vector[0], vector[1], vector[2] };
			break;
		}
		case AnimatorParamType::Quaternion:
		{
			value.type = Vans::EditorAPI::AnimationPreviewParameterType::Quaternion;
			const auto& vector = m_PreviewVectors.at(parameter.name);
			value.quaternionValue = { vector[0], vector[1], vector[2], vector[3] };
			break;
		}
		case AnimatorParamType::Trigger:
			continue;
		}
		m_ActiveAPI->SetAnimationPreviewParameter(value);
	}
}

void VansAnimGraphEditorWindow::EnsurePreviewSession()
{
	if (!m_ActiveAPI || !m_AssetData)
		return;
	const std::string& modelGuid = m_AssetData->editor.previewModelGuid;
	if (modelGuid.empty())
	{
		DestroyPreviewSession();
		return;
	}
	if (m_PreviewSessionId != 0 && m_PreviewSessionModelGuid == modelGuid)
		return;
	DestroyPreviewSession();
	Vans::EditorAPI::AnimationPreviewCreateRequest request;
	request.previewModelGuid = modelGuid;
	const auto result = m_ActiveAPI->CreateAnimationPreview(request);
	if (!result.success)
	{
		m_LastError = result.message;
		return;
	}
	m_PreviewSessionId = result.sessionId;
	m_PreviewSessionModelGuid = modelGuid;
	m_PreviewDocumentStateId = 0;
	QueuePreviewCompile();
}

void VansAnimGraphEditorWindow::QueuePreviewCompile()
{
	if (m_PreviewCompilePending)
		return;
	m_PreviewCompilePending = true;
	m_PreviewCompileQueuedAt = ImGui::GetTime();
}

void VansAnimGraphEditorWindow::UpdatePreviewDefinition()
{
	if (!m_ActiveAPI || !m_AssetData || m_PreviewSessionId == 0)
	{
		m_PreviewCompilePending = false;
		return;
	}
	const std::uint64_t attemptedDocumentStateId = m_Document
		? m_Document->sourceDocument.CurrentStateId() : 0;
	m_AssetData->name = m_EditState->name;
	m_AssetData->parameters = m_EditState->parameters;
	m_AssetData->clipRefs = m_EditState->clipRefs;
	const auto encoded = m_ActiveAPI->EncodeAnimatorDocument(*m_AssetData);
	if (!encoded.success)
	{
		m_LastError = encoded.message;
		m_PreviewCompilePending = false;
		m_PreviewDocumentStateId = attemptedDocumentStateId;
		return;
	}
	nlohmann::json root = nlohmann::json::parse(encoded.canonicalJson);
	const bool hasSolo = !m_EditState->soloLayerIds.empty();
	for (auto& layer : root["layers"])
	{
		const std::string id = layer.value("id", "");
		const bool base = layer.value("kind", "") == "base";
		const bool authoredEnabled = layer.value("enabled", true);
		const bool muted = m_EditState->mutedLayerIds.count(id) != 0;
		const bool soloVisible = !hasSolo || base || m_EditState->soloLayerIds.count(id) != 0;
		layer["enabled"] = authoredEnabled && !muted && soloVisible;
	}
	Vans::EditorAPI::AnimationPreviewDefinitionUpdate update;
	update.sessionId = m_PreviewSessionId;
	update.revision = ++m_PreviewRevision;
	update.canonicalJson = root.dump();
	const auto result = m_ActiveAPI->UpdateAnimationPreviewDefinition(update);
	if (!result.success)
		m_LastError = result.message;
	else
	{
		m_LastError.clear();
		ReconcilePreviewParameters();
		ApplyPreviewParameters();
	}
	m_PreviewCompilePending = false;
	m_PreviewDocumentStateId = attemptedDocumentStateId;
}

void VansAnimGraphEditorWindow::DrawPreviewPanel()
{
	if (!m_AssetData || !m_ActiveAPI)
		return;
	ImGui::TextUnformatted("Isolated Animation Preview");
	ImGui::SameLine();
	const auto models = m_ActiveAPI->QueryAssets({ Vans::EditorAPI::AssetType::Model, false });
	ImGui::SetNextItemWidth(340.0f);
	const char* modelLabel = m_AssetData->editor.previewModelPathHint.empty()
		? "Choose Preview Model..." : m_AssetData->editor.previewModelPathHint.c_str();
	if (ImGui::BeginCombo("##AnimatorPreviewModel", modelLabel))
	{
		for (const auto& model : models)
		{
			const bool selected = model.guid == m_AssetData->editor.previewModelGuid;
			if (ImGui::Selectable(model.relativePath.c_str(), selected))
			{
				m_AssetData->editor.previewModelGuid = model.guid;
				m_AssetData->editor.previewModelPathHint = model.relativePath;
				m_EditState->isDirty = true;
				DestroyPreviewSession();
				EnsurePreviewSession();
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	if (m_PreviewSessionId == 0)
	{
		ImGui::TextDisabled("Select a skeletal Model asset to compile and preview unsaved Animator revisions.");
		return;
	}

	Vans::EditorAPI::AnimationPreviewViewportRequest viewportRequest;
	viewportRequest.sessionId = m_PreviewSessionId;
	viewportRequest.yaw = m_PreviewYaw;
	viewportRequest.pitch = m_PreviewPitch;
	viewportRequest.zoom = m_PreviewZoom;
	viewportRequest.visualizedLayerIndex = m_PreviewVisualizedLayer;
	m_ActiveAPI->SetAnimationPreviewViewport(viewportRequest);
	m_ActiveAPI->TickAnimationPreview(m_PreviewSessionId, ImGui::GetIO().DeltaTime);
	auto snapshot = m_ActiveAPI->GetAnimationPreviewSnapshot(m_PreviewSessionId);
	if (ImGui::BeginTable("AnimationPreviewLayout", 3,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
		ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
	{
		ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthFixed, 270.0f);
		ImGui::TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Diagnostics", ImGuiTableColumnFlags_WidthFixed, 310.0f);
		ImGui::TableNextColumn();
		ImGui::BeginChild("AnimationPreviewControls");
		if (ImGui::Button(m_PreviewPlaying ? "Pause" : "Play"))
		{
			m_PreviewPlaying = !m_PreviewPlaying;
			Vans::EditorAPI::AnimationPreviewPlaybackRequest playback;
			playback.sessionId = m_PreviewSessionId;
			playback.playing = m_PreviewPlaying;
			playback.speed = m_PreviewSpeed;
			playback.rootMotionMode = m_PreviewRootMotionMode;
			m_ActiveAPI->SetAnimationPreviewPlayback(playback);
		}
		ImGui::SameLine();
		if (ImGui::Button("Step"))
		{
			m_PreviewPlaying = false;
			Vans::EditorAPI::AnimationPreviewPlaybackRequest playback;
			playback.sessionId = m_PreviewSessionId;
			playback.playing = false;
			playback.speed = m_PreviewSpeed;
			playback.rootMotionMode = m_PreviewRootMotionMode;
			playback.seek = true;
			playback.normalizedTime = snapshot.duration > 0.0f
				? std::min(1.0f, snapshot.normalizedTime + (1.0f / 30.0f) / snapshot.duration)
				: snapshot.normalizedTime;
			m_ActiveAPI->SetAnimationPreviewPlayback(playback);
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Params"))
		{
			ResetPreviewParameters();
			QueuePreviewCompile();
		}
		if (ImGui::SliderFloat("Speed", &m_PreviewSpeed, 0.0f, 3.0f))
		{
			Vans::EditorAPI::AnimationPreviewPlaybackRequest playback;
			playback.sessionId = m_PreviewSessionId;
			playback.playing = m_PreviewPlaying;
			playback.speed = m_PreviewSpeed;
			playback.rootMotionMode = m_PreviewRootMotionMode;
			m_ActiveAPI->SetAnimationPreviewPlayback(playback);
		}
		int rootMotionMode = static_cast<int>(m_PreviewRootMotionMode);
		if (ImGui::Combo("Root Motion", &rootMotionMode, "In Place\0Apply To Actor\0Trail Only\0"))
		{
			m_PreviewRootMotionMode = static_cast<
				Vans::EditorAPI::AnimationPreviewPlaybackRequest::RootMotionMode>(rootMotionMode);
			Vans::EditorAPI::AnimationPreviewPlaybackRequest playback;
			playback.sessionId = m_PreviewSessionId;
			playback.playing = m_PreviewPlaying;
			playback.speed = m_PreviewSpeed;
			playback.rootMotionMode = m_PreviewRootMotionMode;
			m_ActiveAPI->SetAnimationPreviewPlayback(playback);
		}
		float normalizedTime = snapshot.normalizedTime;
		if (ImGui::SliderFloat("Timeline", &normalizedTime, 0.0f, 1.0f, "%.3f"))
		{
			Vans::EditorAPI::AnimationPreviewPlaybackRequest playback;
			playback.sessionId = m_PreviewSessionId;
			playback.playing = false;
			playback.speed = m_PreviewSpeed;
			playback.seek = true;
			playback.normalizedTime = normalizedTime;
			playback.rootMotionMode = m_PreviewRootMotionMode;
			m_PreviewPlaying = false;
			m_ActiveAPI->SetAnimationPreviewPlayback(playback);
		}
		ImGui::TextDisabled("%.3f / %.3f sec", snapshot.currentTime, snapshot.duration);
		ImGui::SeparatorText("Parameters");
		for (const AnimatorParameter& parameter : m_AssetData->parameters)
		{
			ImGui::PushID(parameter.name.c_str());
			Vans::EditorAPI::AnimationPreviewParameterValue value;
			value.sessionId = m_PreviewSessionId;
			value.name = parameter.name;
			bool changed = false;
			switch (parameter.type)
			{
			case AnimatorParamType::Float:
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Float;
				changed = ImGui::DragFloat(parameter.name.c_str(), &m_PreviewFloats[parameter.name], 0.02f);
				value.floatValue = m_PreviewFloats[parameter.name];
				break;
			case AnimatorParamType::Bool:
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Bool;
				changed = ImGui::Checkbox(parameter.name.c_str(), &m_PreviewBools[parameter.name]);
				value.boolValue = m_PreviewBools[parameter.name];
				break;
			case AnimatorParamType::Int:
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Int;
				changed = ImGui::InputInt(parameter.name.c_str(), &m_PreviewInts[parameter.name]);
				value.intValue = m_PreviewInts[parameter.name];
				break;
			case AnimatorParamType::Trigger:
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Trigger;
				changed = ImGui::Button(("Trigger " + parameter.name).c_str());
				break;
			case AnimatorParamType::Vector3:
			{
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Vector3;
				auto& vector = m_PreviewVectors[parameter.name];
				changed = ImGui::DragFloat3(parameter.name.c_str(), vector.data(), 0.02f);
				value.vectorValue = { vector[0], vector[1], vector[2] };
				break;
			}
			case AnimatorParamType::Quaternion:
			{
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Quaternion;
				auto& vector = m_PreviewVectors[parameter.name];
				changed = ImGui::DragFloat4(parameter.name.c_str(), vector.data(), 0.01f);
				value.quaternionValue = { vector[0], vector[1], vector[2], vector[3] };
				break;
			}
			}
			if (changed) m_ActiveAPI->SetAnimationPreviewParameter(value);
			ImGui::PopID();
		}
		if (!m_AssetData->slots.empty() && !m_AssetData->clipRefs.empty())
		{
			ImGui::SeparatorText("Slot Sandbox");
			if (ImGui::BeginCombo("Slot", m_PreviewSelectedSlotId.c_str()))
			{
				for (const auto& slot : m_AssetData->slots)
					if (ImGui::Selectable(slot.name.c_str(), slot.id == m_PreviewSelectedSlotId))
						m_PreviewSelectedSlotId = slot.id;
				ImGui::EndCombo();
			}
			if (ImGui::BeginCombo("Clip", m_PreviewSelectedClipName.c_str()))
			{
				for (const auto& clip : m_AssetData->clipRefs)
					if (ImGui::Selectable(clip.name.c_str(), clip.name == m_PreviewSelectedClipName))
						m_PreviewSelectedClipName = clip.name;
				ImGui::EndCombo();
			}
			if (ImGui::Button("Trigger Slot"))
			{
				Vans::EditorAPI::AnimationPreviewSlotRequest request;
				request.sessionId = m_PreviewSessionId;
				request.slotId = m_PreviewSelectedSlotId;
				request.clipName = m_PreviewSelectedClipName;
				m_ActiveAPI->TriggerAnimationPreviewSlot(request);
			}
		}
		ImGui::EndChild();

		ImGui::TableNextColumn();
		const char* visualizationLabel = m_PreviewVisualizedLayer < 0
			? "Final Layer Sources"
			: (m_PreviewVisualizedLayer < static_cast<int>(snapshot.layers.size())
				? snapshot.layers[m_PreviewVisualizedLayer].name.c_str() : "Final Layer Sources");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##PreviewVisualization", visualizationLabel))
		{
			if (ImGui::Selectable("Final Layer Sources", m_PreviewVisualizedLayer < 0))
				m_PreviewVisualizedLayer = -1;
			for (int index = 0; index < static_cast<int>(snapshot.layers.size()); ++index)
				if (ImGui::Selectable(snapshot.layers[index].name.c_str(), m_PreviewVisualizedLayer == index))
					m_PreviewVisualizedLayer = index;
			ImGui::EndCombo();
		}
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImVec2 size = ImGui::GetContentRegionAvail();
		ImGui::InvisibleButton("AnimatorPreviewSkeleton", size,
			ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 end(origin.x + size.x, origin.y + size.y);
		draw->AddRectFilled(origin, end, IM_COL32(18, 21, 28, 255));
		if (snapshot.modelRendered && snapshot.modelTexture)
			draw->AddImage(snapshot.modelTexture, origin, end);
		if (ImGui::IsItemHovered())
		{
			m_PreviewZoom = std::clamp(m_PreviewZoom + ImGui::GetIO().MouseWheel * 0.08f, 0.2f, 3.0f);
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				m_PreviewYaw += ImGui::GetIO().MouseDelta.x * 0.01f;
				m_PreviewPitch = std::clamp(m_PreviewPitch + ImGui::GetIO().MouseDelta.y * 0.01f, -1.45f, 1.45f);
			}
		}
		if (!snapshot.bones.empty())
		{
			struct Point { float x; float y; float z; };
			Point center{};
			if (snapshot.modelRendered)
			{
				center = { snapshot.modelCenter.x, snapshot.modelCenter.y, snapshot.modelCenter.z };
			}
			else
			{
				for (const auto& bone : snapshot.bones)
				{
					center.x += bone.position.x;
					center.y += bone.position.y;
					center.z += bone.position.z;
				}
				const float invCount = 1.0f / static_cast<float>(snapshot.bones.size());
				center.x *= invCount; center.y *= invCount; center.z *= invCount;
			}
			const float cy = std::cos(m_PreviewYaw), sy = std::sin(m_PreviewYaw);
			const float cp = std::cos(m_PreviewPitch), sp = std::sin(m_PreviewPitch);
			std::vector<Point> points;
			float radius = snapshot.modelRendered
				? std::max(snapshot.modelRadius, 0.0001f) : 0.0001f;
			for (const auto& bone : snapshot.bones)
			{
				const float x = bone.position.x - center.x;
				const float y = bone.position.y - center.y;
				const float z = bone.position.z - center.z;
				const float rx = cy * x + sy * z;
				const float rz = -sy * x + cy * z;
				const float ry = cp * y - sp * rz;
				points.push_back({ rx, ry, sp * y + cp * rz });
				if (!snapshot.modelRendered)
					radius = std::max(radius, std::max(std::fabs(rx), std::fabs(ry)));
			}
			const float scaleX = snapshot.modelRendered
				? 0.43f * size.x * m_PreviewZoom / radius
				: 0.43f * std::min(size.x, size.y) * m_PreviewZoom / radius;
			const float scaleY = snapshot.modelRendered
				? 0.43f * size.y * m_PreviewZoom / radius : scaleX;
			const ImVec2 middle(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
			std::vector<ImVec2> screen(points.size());
			for (std::size_t index = 0; index < points.size(); ++index)
				screen[index] = ImVec2(middle.x + points[index].x * scaleX,
					middle.y - points[index].y * scaleY);
			auto weightColor = [](float weight)
			{
				weight = std::clamp(weight, 0.0f, 1.0f);
				return IM_COL32(
					static_cast<int>(65.0f + 190.0f * weight),
					static_cast<int>(125.0f + 45.0f * (1.0f - weight)),
					static_cast<int>(235.0f - 185.0f * weight), 255);
			};
			auto boneColor = [&](std::size_t index)
			{
				if (m_PreviewVisualizedLayer >= 0
					&& m_PreviewVisualizedLayer < static_cast<int>(snapshot.layers.size()))
				{
					const auto& weights = snapshot.layers[m_PreviewVisualizedLayer].boneWeights;
					return weightColor(index < weights.size() ? weights[index] : 0.0f);
				}
				const int layerIndex = snapshot.bones[index].dominantLayerIndex;
				static const ImU32 colors[] = {
					IM_COL32(90, 205, 245, 255), IM_COL32(245, 155, 70, 255),
					IM_COL32(180, 100, 240, 255), IM_COL32(80, 220, 135, 255),
					IM_COL32(245, 90, 125, 255)
				};
				return colors[static_cast<std::size_t>((std::max)(0, layerIndex))
					% (sizeof(colors) / sizeof(colors[0]))];
			};
			if (m_PreviewRootMotionMode != Vans::EditorAPI::AnimationPreviewPlaybackRequest::RootMotionMode::InPlace
				&& snapshot.rootMotionTrail.size() > 1)
			{
				std::vector<ImVec2> trail;
				trail.reserve(snapshot.rootMotionTrail.size());
				for (const auto& value : snapshot.rootMotionTrail)
				{
					const float x = value.x - center.x;
					const float y = value.y - center.y;
					const float z = value.z - center.z;
					const float rx = cy * x + sy * z;
					const float rz = -sy * x + cy * z;
					const float ry = cp * y - sp * rz;
					trail.emplace_back(middle.x + rx * scaleX, middle.y - ry * scaleY);
				}
				for (std::size_t index = 1; index < trail.size(); ++index)
					draw->AddLine(trail[index - 1], trail[index], IM_COL32(120, 240, 150, 210), 2.0f);
			}
			int hoveredBone = -1;
			float hoveredDistance = 64.0f;
			for (std::size_t index = 0; index < points.size(); ++index)
			{
				const int parent = snapshot.bones[index].parentIndex;
				if (parent >= 0 && parent < static_cast<int>(screen.size()))
					draw->AddLine(screen[parent], screen[index], boneColor(index), 2.0f);
				draw->AddCircleFilled(screen[index], 3.5f, boneColor(index));
				const float dx = screen[index].x - ImGui::GetIO().MousePos.x;
				const float dy = screen[index].y - ImGui::GetIO().MousePos.y;
				const float distance = dx * dx + dy * dy;
				if (distance < hoveredDistance) { hoveredDistance = distance; hoveredBone = static_cast<int>(index); }
			}
			if (ImGui::IsItemHovered() && hoveredBone >= 0)
			{
				const auto& bone = snapshot.bones[hoveredBone];
				const char* layerName = bone.dominantLayerIndex >= 0
					&& bone.dominantLayerIndex < static_cast<int>(snapshot.layers.size())
					? snapshot.layers[bone.dominantLayerIndex].name.c_str() : "Base";
				ImGui::SetTooltip("%s\nSource: %s\nWeight: %.3f",
					bone.name.c_str(), layerName, bone.dominantLayerWeight);
			}
		}
		else
			draw->AddText(ImVec2(origin.x + 12.0f, origin.y + 12.0f),
				IM_COL32(170, 175, 185, 255), "Preview is waiting for a valid compiled definition.");
		if (snapshot.usingLastGoodDefinition)
			draw->AddText(ImVec2(origin.x + 12.0f, origin.y + 12.0f),
				IM_COL32(255, 190, 70, 255), "Showing last-good revision");

		ImGui::TableNextColumn();
		ImGui::BeginChild("AnimationPreviewDiagnostics");
		ImGui::Text("Revision: %llu / %llu",
			static_cast<unsigned long long>(snapshot.displayedRevision),
			static_cast<unsigned long long>(snapshot.requestedRevision));
		ImGui::Text("Update: %.3f ms", snapshot.lastUpdateMilliseconds);
		if (snapshot.modelRendered)
		{
			ImGui::Text("Model preview: %.3f ms", snapshot.modelRenderMilliseconds);
			ImGui::TextDisabled("%llu vertices / %llu sampled triangles / %ux%u",
				static_cast<unsigned long long>(snapshot.modelVertexCount),
				static_cast<unsigned long long>(snapshot.modelTriangleCount),
				snapshot.modelTextureWidth, snapshot.modelTextureHeight);
		}
		if (snapshot.frameScratchAllocations == 0)
			ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f),
				"Frame scratch: stable (0 upstream allocations)");
		else
			ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
				"Frame scratch: %llu allocations / %llu bytes",
				static_cast<unsigned long long>(snapshot.frameScratchAllocations),
				static_cast<unsigned long long>(snapshot.frameScratchAllocatedBytes));
		if (!snapshot.diagnostic.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s", snapshot.diagnostic.c_str());
		ImGui::SeparatorText("Layers");
		for (const auto& layer : snapshot.layers)
		{
			ImGui::Text("%s  w=%.3f  %.3f ms",
				layer.name.c_str(), layer.weight, layer.evaluationMilliseconds);
			ImGui::TextDisabled("%s  %s  t=%.3f", layer.state.c_str(), layer.clip.c_str(), layer.normalizedTime);
		}
		ImGui::SeparatorText("Root Motion / Sync");
		ImGui::Text("Delta: %.3f, %.3f, %.3f", snapshot.rootMotionDelta.x,
			snapshot.rootMotionDelta.y, snapshot.rootMotionDelta.z);
		ImGui::Text("Position: %.3f, %.3f, %.3f", snapshot.rootMotionPosition.x,
			snapshot.rootMotionPosition.y, snapshot.rootMotionPosition.z);
		if (snapshot.syncValid)
			ImGui::Text("Markers: %llu -> %llu  phase=%.3f",
				static_cast<unsigned long long>(snapshot.syncMarkerId),
				static_cast<unsigned long long>(snapshot.syncNextMarkerId), snapshot.syncPhase);
		else
			ImGui::TextDisabled("Sync markers: inactive");
		ImGui::SeparatorText("Slots");
		if (snapshot.slots.empty())
			ImGui::TextDisabled("No active or completed preview slot handles.");
		for (const auto& slot : snapshot.slots)
		{
			ImGui::Text("%s / %s", slot.slotId.c_str(), slot.clipName.c_str());
			ImGui::TextDisabled("%s  t=%.3f  w=%.3f", slot.state.c_str(),
				slot.playbackTime, slot.weight);
		}
		for (const auto& event : snapshot.slotEvents)
			ImGui::BulletText("%s: %s / %s", event.type.c_str(),
				event.slotId.c_str(), event.clipName.c_str());
		ImGui::SeparatorText("Events / Curves");
		for (const auto& event : snapshot.events)
			ImGui::BulletText("Event %s @ %.3f  %s", event.name.c_str(), event.time, event.payload.c_str());
		for (const auto& curve : snapshot.curves)
			ImGui::BulletText("Curve %s = %.3f", curve.name.c_str(), curve.value);
		ImGui::EndChild();
		ImGui::EndTable();
	}
}

void VansAnimGraphEditorWindow::DrawLayersPanel()
{
	ImGui::Text("Layers");
	ImGui::Separator();
	if (!m_AssetData)
		return;
	auto activateGraph = [&](const std::string& graphId)
	{
		if (m_NodeEditorCtx)
		{
			ne::SetCurrentEditor(m_NodeEditorCtx);
			ReadNodePositions();
			ne::SetCurrentEditor(nullptr);
		}
		m_ActiveGraphId = graphId;
		m_TargetGraph = m_AssetData->FindGraph(graphId);
		m_EditState->selectedNodeId = -1;
		m_EditState->selectedLinkId = -1;
		m_EditState->needsInitialLayout = true;
		m_NavigationStack.clear();
	};
	auto makeUniqueId = [](const std::string& prefix, const auto& exists)
	{
		std::string id = prefix;
		int suffix = 2;
		while (exists(id))
			id = prefix + "-" + std::to_string(suffix++);
		return id;
	};

	for (std::size_t index = 0; index < m_AssetData->layers.size(); ++index)
	{
		VansAnimationLayerDefinition& layer = m_AssetData->layers[index];
		ImGui::PushID(static_cast<int>(index) + 4000);
		const bool selected = layer.id == m_EditState->selectedLayerId;
		const std::string label = (layer.kind == VansAnimationLayerKind::Base ? "[Base] " : "[Overlay] ") + layer.name;
		if (ImGui::Selectable(label.c_str(), selected))
		{
			m_EditState->selectedLayerId = layer.id;
			activateGraph(layer.graphId);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Graph: %s\nMask: %s\nPreview: %s%s",
				layer.graphId.c_str(), layer.maskPathHint.c_str(),
				m_EditState->mutedLayerIds.count(layer.id) ? "Muted " : "",
				m_EditState->soloLayerIds.count(layer.id) ? "Solo" : "");
		ImGui::PopID();
	}

	std::size_t selectedIndex = m_AssetData->layers.size();
	for (std::size_t index = 0; index < m_AssetData->layers.size(); ++index)
		if (m_AssetData->layers[index].id == m_EditState->selectedLayerId)
		{
			selectedIndex = index;
			break;
		}

	if (ImGui::SmallButton("+ Overlay"))
	{
		const std::string graphId = makeUniqueId("graph-overlay", [&](const std::string& id)
			{ return m_AssetData->FindGraph(id) != nullptr; });
		const std::string layerId = makeUniqueId("layer-overlay", [&](const std::string& id)
			{
				return std::any_of(m_AssetData->layers.begin(), m_AssetData->layers.end(),
					[&](const VansAnimationLayerDefinition& layer) { return layer.id == id; });
			});
		AnimatorGraphAsset graphAsset;
		graphAsset.id = graphId;
		graphAsset.name = "Overlay Graph";
		graphAsset.role = AnimatorGraphAsset::Role::Pose;
		graphAsset.graph = std::make_unique<VansAnimGraph>();
		auto clipNode = VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Clip);
		if (!m_EditState->clipRefs.empty())
			clipNode->m_ClipName = m_EditState->clipRefs.front().name;
		const int clipId = graphAsset.graph->AddNode(std::move(clipNode));
		const int outputId = graphAsset.graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
		graphAsset.graph->GetNode(clipId)->m_EditorPosX = 40.0f;
		graphAsset.graph->GetNode(outputId)->m_EditorPosX = 360.0f;
		graphAsset.graph->AddLink(clipId, 0, outputId, 0);
		m_AssetData->graphs.push_back(std::move(graphAsset));
		VansAnimationLayerDefinition layer;
		layer.id = layerId;
		layer.name = "Overlay";
		layer.graphId = graphId;
		layer.kind = VansAnimationLayerKind::Overlay;
		m_AssetData->layers.push_back(std::move(layer));
		m_EditState->selectedLayerId = layerId;
		activateGraph(graphId);
		m_EditState->isDirty = true;
		selectedIndex = m_AssetData->layers.size() - 1;
	}
	if (selectedIndex < m_AssetData->layers.size())
	{
		VansAnimationLayerDefinition& selectedLayer = m_AssetData->layers[selectedIndex];
		const bool overlay = selectedLayer.kind == VansAnimationLayerKind::Overlay;
		ImGui::SameLine();
		if (!overlay) ImGui::BeginDisabled();
		bool duplicatedLayer = false;
		if (ImGui::SmallButton("Duplicate"))
		{
			const VansAnimGraph* sourceGraph = m_AssetData->FindGraph(selectedLayer.graphId);
			auto graphCopy = sourceGraph ? VansAnimGraph::Clone(*sourceGraph) : nullptr;
			if (graphCopy)
			{
				const std::string graphId = makeUniqueId(selectedLayer.graphId + "-copy", [&](const std::string& id)
					{ return m_AssetData->FindGraph(id) != nullptr; });
				const std::string layerId = makeUniqueId(selectedLayer.id + "-copy", [&](const std::string& id)
					{
						return std::any_of(m_AssetData->layers.begin(), m_AssetData->layers.end(),
							[&](const VansAnimationLayerDefinition& layer) { return layer.id == id; });
					});
				AnimatorGraphAsset graphAsset;
				graphAsset.id = graphId;
				graphAsset.name = selectedLayer.name + " Copy Graph";
				graphAsset.role = AnimatorGraphAsset::Role::Pose;
				graphAsset.graph = std::move(graphCopy);
				m_AssetData->graphs.push_back(std::move(graphAsset));
				VansAnimationLayerDefinition copy = selectedLayer;
				copy.id = layerId;
				copy.name += " Copy";
				copy.graphId = graphId;
				m_AssetData->layers.insert(m_AssetData->layers.begin() + selectedIndex + 1, copy);
				m_EditState->selectedLayerId = layerId;
				activateGraph(graphId);
				m_EditState->isDirty = true;
				selectedIndex += 1;
				duplicatedLayer = true;
			}
		}
		if (duplicatedLayer)
		{
			if (!overlay) ImGui::EndDisabled();
			return;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Delete"))
		{
			const std::string removedLayerId = selectedLayer.id;
			const std::string removedGraphId = selectedLayer.graphId;
			m_AssetData->layers.erase(m_AssetData->layers.begin() + selectedIndex);
			m_AssetData->slots.erase(std::remove_if(m_AssetData->slots.begin(), m_AssetData->slots.end(),
				[&](const VansAnimationSlotDefinition& slot) { return slot.layerId == removedLayerId; }), m_AssetData->slots.end());
			for (VansAnimationLayerDefinition& layer : m_AssetData->layers)
				if (layer.syncLeaderLayerId == removedLayerId)
				{
					layer.sync = VansLayerSyncMode::Independent;
					layer.syncLeaderLayerId.clear();
				}
			const bool graphStillUsed = std::any_of(m_AssetData->layers.begin(), m_AssetData->layers.end(),
				[&](const VansAnimationLayerDefinition& layer) { return layer.graphId == removedGraphId; });
			if (!graphStillUsed)
				m_AssetData->graphs.erase(std::remove_if(m_AssetData->graphs.begin(), m_AssetData->graphs.end(),
					[&](const AnimatorGraphAsset& graph) { return graph.id == removedGraphId; }), m_AssetData->graphs.end());
			m_EditState->mutedLayerIds.erase(removedLayerId);
			m_EditState->soloLayerIds.erase(removedLayerId);
			m_EditState->selectedLayerId = m_AssetData->layers.front().id;
			activateGraph(m_AssetData->layers.front().graphId);
			m_EditState->isDirty = true;
			selectedIndex = 0;
		}
		if (!overlay) ImGui::EndDisabled();

		if (selectedIndex < m_AssetData->layers.size())
		{
			VansAnimationLayerDefinition& layer = m_AssetData->layers[selectedIndex];
			const bool canMoveUp = selectedIndex > 1;
			const bool canMoveDown = selectedIndex > 0 && selectedIndex + 1 < m_AssetData->layers.size();
			bool reorderedLayer = false;
			if (!canMoveUp) ImGui::BeginDisabled();
			if (ImGui::SmallButton("Move Up"))
			{
				std::swap(m_AssetData->layers[selectedIndex], m_AssetData->layers[selectedIndex - 1]);
				--selectedIndex;
				m_EditState->isDirty = true;
				reorderedLayer = true;
			}
			if (!canMoveUp) ImGui::EndDisabled();
			ImGui::SameLine();
			if (!canMoveDown) ImGui::BeginDisabled();
			if (ImGui::SmallButton("Move Down"))
			{
				std::swap(m_AssetData->layers[selectedIndex], m_AssetData->layers[selectedIndex + 1]);
				++selectedIndex;
				m_EditState->isDirty = true;
				reorderedLayer = true;
			}
			if (!canMoveDown) ImGui::EndDisabled();
			if (reorderedLayer)
				return;

			ImGui::SeparatorText("Layer Inspector");
			if (EditStringProperty("Layer Name", layer.name)) m_EditState->isDirty = true;
			if (ImGui::Checkbox("Enabled", &layer.enabled)) m_EditState->isDirty = true;
			bool muted = m_EditState->mutedLayerIds.count(layer.id) != 0;
			if (ImGui::Checkbox("Preview Mute", &muted))
			{
				if (muted) m_EditState->mutedLayerIds.insert(layer.id); else m_EditState->mutedLayerIds.erase(layer.id);
				QueuePreviewCompile();
			}
			bool solo = m_EditState->soloLayerIds.count(layer.id) != 0;
			if (ImGui::Checkbox("Preview Solo", &solo))
			{
				if (solo) m_EditState->soloLayerIds.insert(layer.id); else m_EditState->soloLayerIds.erase(layer.id);
				QueuePreviewCompile();
			}

			if (layer.kind == VansAnimationLayerKind::Overlay)
			{
				if (EditStringProperty("Mask Path", layer.maskPathHint)) m_EditState->isDirty = true;
				if (ImGui::SmallButton("Assign Mask From Path"))
				{
					const Vans::EditorAPI::AssetDragPayload asset = m_ActiveAPI
						? m_ActiveAPI->CreateAssetDragPayload(layer.maskPathHint)
						: Vans::EditorAPI::AssetDragPayload{};
					if (asset.available && asset.assetType == Vans::EditorAPI::AssetType::BoneMask && !asset.guid.empty())
					{
						layer.maskGuid = asset.guid;
						m_LastError.clear();
						m_EditState->isDirty = true;
					}
					else
						m_LastError = asset.error.empty() ? "Mask path must resolve to a Bone Mask asset" : asset.error;
				}
				ImGui::TextDisabled("Mask GUID: %s", layer.maskGuid.empty() ? "(required)" : layer.maskGuid.c_str());

				int blendMode = static_cast<int>(layer.blendMode);
				if (ImGui::Combo("Blend Mode", &blendMode, "Override\0Additive\0")) { layer.blendMode = static_cast<VansLayerBlendMode>(blendMode); m_EditState->isDirty = true; }
				int rotationSpace = static_cast<int>(layer.rotationSpace);
				if (ImGui::Combo("Rotation Space", &rotationSpace, "Local\0Mesh\0")) { layer.rotationSpace = static_cast<VansRotationBlendSpace>(rotationSpace); m_EditState->isDirty = true; }
				if (layer.blendMode == VansLayerBlendMode::Additive)
				{
					int referenceMode = static_cast<int>(layer.additiveReference);
					if (ImGui::Combo("Reference", &referenceMode, "Bind Pose\0First Frame\0Clip Time\0Reference Clip\0")) { layer.additiveReference = static_cast<VansAdditiveReferenceMode>(referenceMode); m_EditState->isDirty = true; }
					if (layer.additiveReference == VansAdditiveReferenceMode::ReferenceClip && EditStringProperty("Reference Clip", layer.referenceClipName)) m_EditState->isDirty = true;
					if ((layer.additiveReference == VansAdditiveReferenceMode::ClipTime || layer.additiveReference == VansAdditiveReferenceMode::ReferenceClip)
						&& ImGui::DragFloat("Reference Time", &layer.referenceTime, 0.01f, 0.0f)) m_EditState->isDirty = true;
				}
				if (ImGui::Checkbox("Weight Parameter", &layer.useWeightParameter)) m_EditState->isDirty = true;
				if (layer.useWeightParameter)
				{
					if (EditStringProperty("Weight Param", layer.weightParameter)) m_EditState->isDirty = true;
				}
				else if (ImGui::SliderFloat("Weight", &layer.fixedWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
				if (ImGui::DragFloat("Weight Smoothing", &layer.weightSmoothingTime, 0.01f, 0.0f)) m_EditState->isDirty = true;
			}

			int rootMotion = static_cast<int>(layer.rootMotion);
			if (ImGui::Combo("Root Motion", &rootMotion, "Ignore\0Base\0Blend By Root Weight\0Override\0")) { layer.rootMotion = static_cast<VansLayerRootMotionMode>(rootMotion); m_EditState->isDirty = true; }
			int curves = static_cast<int>(layer.curves);
			if (ImGui::Combo("Curves", &curves, "Base Only\0Override\0Blend\0Normalize\0Min\0Max\0")) { layer.curves = static_cast<VansLayerCurveMode>(curves); m_EditState->isDirty = true; }
			int events = static_cast<int>(layer.events);
			if (ImGui::Combo("Events", &events, "Ignore\0Active Only\0Always\0")) { layer.events = static_cast<VansLayerEventMode>(events); m_EditState->isDirty = true; }
			if (ImGui::SliderFloat("Event Threshold", &layer.eventWeightThreshold, 0.0f, 1.0f)) m_EditState->isDirty = true;
			int nodeTracks = static_cast<int>(layer.nodeTracks);
			if (ImGui::Combo("Node Tracks", &nodeTracks, "Ignore\0Override\0")) { layer.nodeTracks = static_cast<VansLayerNodeTrackMode>(nodeTracks); m_EditState->isDirty = true; }
			if (ImGui::Checkbox("Update At Zero Weight", &layer.updateWhenWeightIsZero)) m_EditState->isDirty = true;
			int syncMode = static_cast<int>(layer.sync);
			if (ImGui::Combo("Sync Mode", &syncMode, "Independent\0Normalized Time\0Marker Sync\0Synced Graph\0"))
			{
				layer.sync = static_cast<VansLayerSyncMode>(syncMode);
				if (layer.sync == VansLayerSyncMode::Independent) layer.syncLeaderLayerId.clear();
				m_EditState->isDirty = true;
			}
			if (layer.sync != VansLayerSyncMode::Independent && EditStringProperty("Sync Leader", layer.syncLeaderLayerId)) m_EditState->isDirty = true;
		}
	}

	AnimatorGraphAsset* targetPostProcess = nullptr;
	for (AnimatorGraphAsset& graph : m_AssetData->graphs)
	{
		if (graph.role == AnimatorGraphAsset::Role::TargetPostProcess)
		{
			targetPostProcess = &graph;
			break;
		}
	}
	if (targetPostProcess)
	{
		const bool selected = targetPostProcess->id == m_ActiveGraphId;
		if (ImGui::Selectable(("[Target Post Process] " + targetPostProcess->name).c_str(), selected))
		{
			m_EditState->selectedLayerId.clear();
			activateGraph(targetPostProcess->id);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Runs after Layer composition and after Retarget on the target skeleton");
	}
	else if (ImGui::Button("+ Target Post Process"))
	{
		std::string graphId = "graph-target-post-process";
		int suffix = 2;
		while (m_AssetData->FindGraph(graphId))
			graphId = "graph-target-post-process-" + std::to_string(suffix++);

		AnimatorGraphAsset graphAsset;
		graphAsset.id = graphId;
		graphAsset.name = "Target Post Process";
		graphAsset.role = AnimatorGraphAsset::Role::TargetPostProcess;
		graphAsset.graph = std::make_unique<VansAnimGraph>();
		const int inputId = graphAsset.graph->AddNode(
			VansAnimGraph::CreateNodeByType(AnimGraphNodeType::TargetPoseInput));
		const int outputId = graphAsset.graph->AddNode(
			VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
		graphAsset.graph->GetNode(inputId)->m_EditorPosX = 40.0f;
		graphAsset.graph->GetNode(outputId)->m_EditorPosX = 360.0f;
		graphAsset.graph->AddLink(inputId, 0, outputId, 0);
		m_AssetData->graphs.push_back(std::move(graphAsset));
		m_ActiveGraphId = graphId;
		m_TargetGraph = m_AssetData->FindGraph(graphId);
		m_EditState->selectedNodeId = -1;
		m_EditState->selectedLinkId = -1;
		m_EditState->needsInitialLayout = true;
		m_EditState->isDirty = true;
		m_NavigationStack.clear();
	}
}

void VansAnimGraphEditorWindow::DrawSlotsPanel()
{
	ImGui::Text("Slots");
	ImGui::Separator();
	if (!m_AssetData)
		return;
	for (std::size_t index = 0; index < m_AssetData->slots.size(); ++index)
	{
		VansAnimationSlotDefinition& slot = m_AssetData->slots[index];
		ImGui::PushID(static_cast<int>(index) + 6000);
		if (ImGui::Selectable(slot.name.c_str(), slot.id == m_EditState->selectedSlotId))
			m_EditState->selectedSlotId = slot.id;
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("ID: %s\nLayer: %s\nNode: %d", slot.id.c_str(), slot.layerId.c_str(), slot.slotNodeId);
		ImGui::PopID();
	}

	if (ImGui::SmallButton("Bind Selected Slot Node"))
	{
		VansAnimGraphNode* selectedNode = m_TargetGraph
			? m_TargetGraph->GetNode(m_EditState->selectedNodeId) : nullptr;
		if (!selectedNode || selectedNode->GetType() != AnimGraphNodeType::Slot)
		{
			m_LastError = "Select a Slot node in a Layer Graph before binding a Slot definition";
		}
		else
		{
			auto* slotNode = static_cast<AnimGraphSlotNode*>(selectedNode);
			if (slotNode->m_SlotId.empty())
				slotNode->m_SlotId = "slot-" + std::to_string(selectedNode->GetNodeId());
			const auto duplicate = std::find_if(m_AssetData->slots.begin(), m_AssetData->slots.end(),
				[&](const VansAnimationSlotDefinition& slot)
				{
					return slot.id == slotNode->m_SlotId
						|| (slot.layerId == m_EditState->selectedLayerId && slot.slotNodeId == selectedNode->GetNodeId());
				});
			if (duplicate != m_AssetData->slots.end())
			{
				m_EditState->selectedSlotId = duplicate->id;
				m_LastError = "Selected Slot node is already bound";
			}
			else
			{
				VansAnimationSlotDefinition slot;
				slot.id = slotNode->m_SlotId;
				slot.name = slotNode->m_SlotId;
				slot.layerId = m_EditState->selectedLayerId;
				slot.slotNodeId = selectedNode->GetNodeId();
				m_AssetData->slots.push_back(slot);
				m_EditState->selectedSlotId = slot.id;
				m_EditState->isDirty = true;
				m_LastError.clear();
			}
		}
	}

	auto selected = std::find_if(m_AssetData->slots.begin(), m_AssetData->slots.end(),
		[&](const VansAnimationSlotDefinition& slot) { return slot.id == m_EditState->selectedSlotId; });
	if (selected == m_AssetData->slots.end())
		return;
	ImGui::SeparatorText("Slot Inspector");
	if (EditStringProperty("Slot Name", selected->name)) m_EditState->isDirty = true;
	ImGui::TextDisabled("ID: %s", selected->id.c_str());
	ImGui::TextDisabled("Layer: %s  Node: %d", selected->layerId.c_str(), selected->slotNodeId);
	int concurrency = static_cast<int>(selected->concurrency);
	if (ImGui::Combo("Concurrency", &concurrency, "Replace\0Queue\0Reject\0"))
	{
		selected->concurrency = static_cast<VansSlotConcurrency>(concurrency);
		m_EditState->isDirty = true;
	}
	int queueDepth = static_cast<int>(selected->maxQueueDepth);
	if (ImGui::InputInt("Max Queue", &queueDepth))
	{
		selected->maxQueueDepth = static_cast<std::uint32_t>((std::max)(0, queueDepth));
		m_EditState->isDirty = true;
	}
	if (ImGui::DragFloat("Blend In", &selected->defaultBlendIn, 0.01f, 0.0f)) m_EditState->isDirty = true;
	if (ImGui::DragFloat("Blend Out", &selected->defaultBlendOut, 0.01f, 0.0f)) m_EditState->isDirty = true;
	if (ImGui::Checkbox("Interruptible", &selected->interruptible)) m_EditState->isDirty = true;
	if (ImGui::SmallButton("Remove Slot Binding"))
	{
		m_AssetData->slots.erase(selected);
		m_EditState->selectedSlotId.clear();
		m_EditState->isDirty = true;
	}
}

bool VansAnimGraphEditorWindow::IsEditingTargetPostProcessGraph() const
{
	if (!m_AssetData)
		return false;
	for (const AnimatorGraphAsset& graph : m_AssetData->graphs)
		if (graph.id == m_ActiveGraphId)
			return graph.role == AnimatorGraphAsset::Role::TargetPostProcess;
	return false;
}
// ============================================================================
//  AnimGraph 娓叉煋
// ============================================================================
void VansAnimGraphEditorWindow::DrawNavigationBar()
{
    ImGui::Separator();
    if (IsRootGraphView())
        ImGui::BeginDisabled();
    if (ImGui::Button("< Back"))
        NavigateBack();
    if (IsRootGraphView())
        ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted("Graph");
    for (const auto& frame : m_NavigationStack)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
        ImGui::TextUnformatted(frame.title.c_str());
    }
    ImGui::Separator();
}
bool VansAnimGraphEditorWindow::CanOpenNodeSubgraph(VansAnimGraphNode* node) const
{
    if (!node)
        return false;
    return node->GetType() == AnimGraphNodeType::StateMachine ||
           node->GetType() == AnimGraphNodeType::MotionMatching;
}
void VansAnimGraphEditorWindow::OpenNodeSubgraph(int nodeId)
{
    if (!m_TargetGraph)
        return;
    VansAnimGraphNode* node = m_TargetGraph->GetNode(nodeId);
    if (!CanOpenNodeSubgraph(node))
        return;
    NavigationFrame frame;
    frame.nodeId = nodeId;
    frame.title = std::string(VansAnimGraphNode::TypeToString(node->GetType())) + ": " + node->GetName();
    m_NavigationStack.push_back(frame);
    m_EditState->selectedNodeId = nodeId;
    m_EditState->selectedLinkId = -1;
}
void VansAnimGraphEditorWindow::NavigateBack()
{
    if (!m_NavigationStack.empty())
        m_NavigationStack.pop_back();
}
void VansAnimGraphEditorWindow::DrawSubgraphPreviewCanvas()
{
    if (m_NavigationStack.empty() || !m_TargetGraph)
        return;
    VansAnimGraphNode* node = m_TargetGraph->GetNode(m_NavigationStack.back().nodeId);
    if (!node)
    {
        ImGui::TextDisabled("Missing node.");
        return;
    }
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "[%s] %s", VansAnimGraphNode::TypeToString(node->GetType()), node->GetName().c_str());
    ImGui::Separator();
    if (node->GetType() == AnimGraphNodeType::StateMachine)
        DrawStateMachineSubgraphPreview(node);
    else if (node->GetType() == AnimGraphNodeType::MotionMatching)
        DrawMotionMatchingSubgraphPreview(node);
}
void VansAnimGraphEditorWindow::DrawStateMachineSubgraphPreview(VansAnimGraphNode* node)
{
    auto* sm = static_cast<AnimGraphStateMachineNode*>(node);
    ImGui::Text("Default State: %s", sm->m_DefaultStateName.empty() ? "(none)" : sm->m_DefaultStateName.c_str());
    ImGui::Separator();
    ImGui::Text("States");
    for (const auto& state : sm->m_States)
    {
        ImGui::BulletText("%s  Clip=%s  Speed=%.2f  Loop=%s  RootMotion=%s", state.name.c_str(), state.clipName.c_str(), state.speed, state.loop ? "true" : "false", state.rootMotion ? "true" : "false");
        ImGui::TextDisabled("    Range %.2f -> %.2f", state.startTime, state.endTime);
    }
    ImGui::Separator();
    ImGui::Text("Transitions");
    for (const auto& transition : sm->m_Transitions)
    {
        ImGui::BulletText("%s -> %s  Blend=%.2f  Exit=%s %.2f  Conditions=%d", transition.fromState.c_str(), transition.toState.c_str(), transition.blendDuration, transition.hasExitTime ? "true" : "false", transition.exitTime, (int)transition.conditions.size());
    }
}
void VansAnimGraphEditorWindow::DrawMotionMatchingSubgraphPreview(VansAnimGraphNode* node)
{
    auto* mm = static_cast<AnimGraphMotionMatchingNode*>(node);
    ImGui::Text("Fallback Input: %s", mm->m_EnableFallbackInput ? "enabled" : "disabled");
    ImGui::Text("Runtime Param: UseMotionMatching");
    ImGui::Separator();
	ImGui::TextDisabled("Scene runtime state is isolated from this asset document.");
    ImGui::Separator();
    ImGui::Text("Pipeline");
    ImGui::BulletText("Query Parameters");
    ImGui::BulletText("Trajectory And Pose Feature Extraction");
    ImGui::BulletText("Database Search");
    ImGui::BulletText("Candidate Ranking");
    ImGui::BulletText("Pose Output");
    ImGui::BulletText("Fallback Pose");
}
void VansAnimGraphEditorWindow::DrawGraphEditorCanvas()
{
	if (!m_TargetGraph) return;
	for (auto& [id, node] : m_TargetGraph->GetNodes())
		DrawGraphNode(node.get());
	DrawGraphLinks();

	if (ne::BeginCreate())
	{
		ne::PinId startPinId;
		ne::PinId endPinId;
		if (ne::QueryNewLink(&startPinId, &endPinId))
		{
			DecodedAnimGraphPin startPin;
			DecodedAnimGraphPin endPin;
			if (DecodeAnimGraphPin(startPinId, startPin)
			    && DecodeAnimGraphPin(endPinId, endPin))
			{
				if (!startPin.output && endPin.output)
					std::swap(startPin, endPin);
				if (startPin.output && !endPin.output)
				{
					if (ne::AcceptNewItem()
					    && m_TargetGraph->AddLink(startPin.nodeId, startPin.pinIndex,
					                              endPin.nodeId, endPin.pinIndex) >= 0)
						m_EditState->isDirty = true;
				}
				else
					ne::RejectNewItem();
			}
			else
				ne::RejectNewItem();
		}
	}
	ne::EndCreate();

	if (ne::BeginDelete())
	{
		ne::LinkId deletedLink;
		while (ne::QueryDeletedLink(&deletedLink))
		{
			if (ne::AcceptDeletedItem())
			{
				m_TargetGraph->RemoveLink(static_cast<int>(deletedLink.Get()) - 1000000);
				m_EditState->isDirty = true;
			}
		}
		ne::NodeId deletedNode;
		while (ne::QueryDeletedNode(&deletedNode))
		{
			const int nodeId = static_cast<int>(deletedNode.Get());
			VansAnimGraphNode* node = m_TargetGraph->GetNode(nodeId);
			if (node && node->GetType() != AnimGraphNodeType::Output
				&& node->GetType() != AnimGraphNodeType::TargetPoseInput
				&& ne::AcceptDeletedItem())
			{
				m_TargetGraph->RemoveNode(nodeId);
				m_EditState->isDirty = true;
			}
			else
				ne::RejectDeletedItem();
		}
	}
	ne::EndDelete();

	static ImVec2 createPosition(0.0f, 0.0f);
	if (ne::ShowBackgroundContextMenu())
	{
		createPosition = ne::ScreenToCanvas(ImGui::GetMousePos());
		ImGui::OpenPopup("CreateAnimGraphNode");
	}
	if (ImGui::BeginPopup("CreateAnimGraphNode"))
	{
		auto addNode = [&](AnimGraphNodeType type)
		{
			auto node = VansAnimGraph::CreateNodeByType(type);
			const int nodeId = m_TargetGraph->AddNode(std::move(node));
			if (nodeId >= 0)
			{
				ne::SetNodePosition(AnimGraphIds::MakeNodeId(nodeId), createPosition);
				m_EditState->isDirty = true;
			}
			ImGui::CloseCurrentPopup();
		};
		const bool targetPostProcess = IsEditingTargetPostProcessGraph();
		if (!targetPostProcess && ImGui::MenuItem("Clip")) addNode(AnimGraphNodeType::Clip);
		if (ImGui::MenuItem("Blend")) addNode(AnimGraphNodeType::Blend);
		if (ImGui::MenuItem("Blend 1D")) addNode(AnimGraphNodeType::Blend1D);
		if (ImGui::MenuItem("If Condition")) addNode(AnimGraphNodeType::IfCondition);
		if (ImGui::MenuItem("Switch")) addNode(AnimGraphNodeType::Switch);
		if (ImGui::MenuItem("Additive Blend")) addNode(AnimGraphNodeType::AdditiveBlend);
		if (!targetPostProcess && ImGui::MenuItem("Speed Scale")) addNode(AnimGraphNodeType::SpeedScale);
		if (!targetPostProcess && ImGui::MenuItem("State Machine")) addNode(AnimGraphNodeType::StateMachine);
		if (!targetPostProcess && ImGui::MenuItem("Motion Matching")) addNode(AnimGraphNodeType::MotionMatching);
		if (!targetPostProcess && ImGui::MenuItem("Slot")) addNode(AnimGraphNodeType::Slot);
		if (ImGui::MenuItem("IK")) addNode(AnimGraphNodeType::IK);
		if (ImGui::MenuItem("Two Bone IK")) addNode(AnimGraphNodeType::TwoBoneIK);
		if (ImGui::MenuItem("Look At")) addNode(AnimGraphNodeType::LookAt);
		if (ImGui::MenuItem("Foot Placement")) addNode(AnimGraphNodeType::FootPlacement);
		ImGui::EndPopup();
	}

	ne::NodeId doubleClickedNode = ne::GetDoubleClickedNode();
	if (doubleClickedNode)
		OpenNodeSubgraph(static_cast<int>(doubleClickedNode.Get()));
	// ============================================================================
	// ????
	SyncSelection();
}
static ImU32 GetNodeHeaderColor(AnimGraphNodeType type)
{
	switch (type)
	{
    case AnimGraphNodeType::Entry:         return IM_COL32(80,  200, 120, 255);  // 绿
    case AnimGraphNodeType::Output:        return IM_COL32(220, 80,  80,  255);  // 红
    case AnimGraphNodeType::Clip:          return IM_COL32(80,  140, 220, 255);  // 蓝
    case AnimGraphNodeType::Blend:         return IM_COL32(160, 100, 220, 255);  // 紫
	case AnimGraphNodeType::Blend1D:       return IM_COL32(140, 110, 200, 255);
	case AnimGraphNodeType::IfCondition:   return IM_COL32(230, 160, 50,  255);  // ?
	case AnimGraphNodeType::Switch:        return IM_COL32(210, 200, 60,  255);  // ?
	case AnimGraphNodeType::AdditiveBlend: return IM_COL32(100, 180, 180, 255);  // ?
	case AnimGraphNodeType::SpeedScale:    return IM_COL32(180, 140, 100, 255);  // ?
	case AnimGraphNodeType::StateMachine:  return IM_COL32(180, 180, 180, 255);  // ?
	case AnimGraphNodeType::MotionMatching:return IM_COL32(90,  190, 150, 255);
	case AnimGraphNodeType::Slot:          return IM_COL32(205, 105, 145, 255);
	case AnimGraphNodeType::TargetPoseInput:return IM_COL32(70, 190, 210, 255);
	}
	return IM_COL32(150, 150, 150, 255);
}
static const char* GetNodeSubtitle(VansAnimGraphNode* node)
{
	switch (node->GetType())
	{
	case AnimGraphNodeType::Clip:
	{
		auto* n = static_cast<AnimGraphClipNode*>(node);
		return n->m_ClipName.c_str();
	}
	case AnimGraphNodeType::Blend:
	{
		auto* n = static_cast<AnimGraphBlendNode*>(node);
		return n->m_UseParam ? n->m_ParamName.c_str() : "(fixed)";
	}
	case AnimGraphNodeType::Blend1D:
	{
		auto* n = static_cast<AnimGraphBlend1DNode*>(node);
		return n->m_ParamName.c_str();
	}
	case AnimGraphNodeType::IfCondition:
	{
		auto* n = static_cast<AnimGraphIfConditionNode*>(node);
		return n->m_ParamName.c_str();
	}
	case AnimGraphNodeType::Switch:
	{
		auto* n = static_cast<AnimGraphSwitchNode*>(node);
		return n->m_ParamName.c_str();
	}
	case AnimGraphNodeType::MotionMatching:
		return "UseMotionMatching";
	case AnimGraphNodeType::Slot:
		return static_cast<AnimGraphSlotNode*>(node)->m_SlotId.c_str();
	default:
		return "";
	}
}
void VansAnimGraphEditorWindow::DrawGraphNode(VansAnimGraphNode* node)
{
	if (!node) return;
	int nodeId = node->GetNodeId();
	ImU32 headerColor = GetNodeHeaderColor(node->GetType());
	ne::BeginNode(AnimGraphIds::MakeNodeId(nodeId));
	// ============================================================================
	// ???????? + ????
	ImGui::PushStyleColor(ImGuiCol_Text, headerColor);
	ImGui::Text("[%s]", VansAnimGraphNode::TypeToString(node->GetType()));
	ImGui::PopStyleColor();
	ImGui::SameLine();
	ImGui::Text("%s", node->GetName().c_str());
	const char* subtitle = GetNodeSubtitle(node);
	if (subtitle && subtitle[0] != '\0')
	{
		ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "%s", subtitle);
	}
	ImGui::Dummy(ImVec2(100, 0));
	auto pins = node->GetPins();
	// 鏀堕泦 Input / Output pin 鍒楄〃
	std::vector<const AnimGraphPin*> inputPins, outputPins;
	for (auto& pin : pins)
	{
		if (pin.kind == AnimGraphPinKind::Input)
			inputPins.push_back(&pin);
		else
			outputPins.push_back(&pin);
	}
	size_t rowCount = (std::max)(inputPins.size(), outputPins.size());
	for (size_t row = 0; row < rowCount; ++row)
	{
		// Input pin
		if (row < inputPins.size())
		{
			ne::BeginPin(AnimGraphIds::MakeInputPin(nodeId, inputPins[row]->pinIndex),
			             ne::PinKind::Input);
			ImGui::Text(">> %s", inputPins[row]->name.c_str());
			ne::EndPin();
		}
		else
		{
			ImGui::Text("");  // 鍗犱綅
		}
		if (row < outputPins.size())
		{
			ImGui::SameLine(120);
			ne::BeginPin(AnimGraphIds::MakeOutputPin(nodeId, outputPins[row]->pinIndex),
			             ne::PinKind::Output);
			ImGui::Text("%s >>", outputPins[row]->name.c_str());
			ne::EndPin();
		}
	}
	ne::EndNode();
}
void VansAnimGraphEditorWindow::DrawGraphLinks()
{
	if (!m_TargetGraph) return;
	for (auto& link : m_TargetGraph->GetLinks())
	{
		ne::PinId startPin = AnimGraphIds::MakeOutputPin(link.fromNodeId, link.fromPinIndex);
		ne::PinId endPin   = AnimGraphIds::MakeInputPin(link.toNodeId, link.toPinIndex);
		ne::LinkId linkId  = AnimGraphIds::MakeLinkId(link.linkId);
		ne::Link(linkId, startPin, endPin, ImVec4(0.9f, 0.9f, 0.9f, 1.0f), 2.0f);
	}
}
void VansAnimGraphEditorWindow::DrawPropertiesPanel()
{
	ImGui::Text("Properties");
	ImGui::Separator();
	if (!m_TargetGraph || m_EditState->selectedNodeId < 0)
	{
		ImGui::TextDisabled("Select a node to view properties.");
		return;
	}
	VansAnimGraphNode* node = m_TargetGraph->GetNode(m_EditState->selectedNodeId);
	if (!node) return;
	ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "[%s]",
	                   VansAnimGraphNode::TypeToString(node->GetType()));
	std::string nodeName = node->GetName();
	if (EditStringProperty("Name", nodeName))
	{
		node->SetName(nodeName);
		m_EditState->isDirty = true;
	}
	ImGui::Text("ID:   %d", node->GetNodeId());
	ImGui::Separator();
	auto editParameterBinding = [&](const char* label, std::string& value,
		AnimatorParamType requiredType, bool optional = true)
	{
		bool changed = false;
		if (ImGui::BeginCombo(label, value.empty() ? "(none)" : value.c_str()))
		{
			if (optional && ImGui::Selectable("(none)", value.empty()))
			{
				value.clear();
				changed = true;
			}
			for (const AnimatorParameter& parameter : m_EditState->parameters)
				if (parameter.type == requiredType
					&& ImGui::Selectable(parameter.name.c_str(), parameter.name == value))
				{
					value = parameter.name;
					changed = true;
				}
			ImGui::EndCombo();
		}
		return changed;
	};
	auto editCoordinateSpace = [&](const char* label, IKCoordinateSpace& value)
	{
		int selected = static_cast<int>(value);
		if (!ImGui::Combo(label, &selected, "Model\0World\0Bone\0Parent Bone\0"))
			return false;
		value = static_cast<IKCoordinateSpace>(selected);
		return true;
	};
	auto editTargetReference = [&](IKCoordinateSpace space, const char* label, std::string& value)
	{
		return (space == IKCoordinateSpace::Bone || space == IKCoordinateSpace::ParentBone)
			&& EditStringProperty(label, value);
	};
	switch (node->GetType())
	{
	case AnimGraphNodeType::Clip:
	{
		auto* n = static_cast<AnimGraphClipNode*>(node);
		if (EditStringProperty("Clip", n->m_ClipName)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Speed", &n->m_Speed, 0.01f)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Loop", &n->m_Loop)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::Blend:
	{
		auto* n = static_cast<AnimGraphBlendNode*>(node);
		if (EditStringProperty("Parameter", n->m_ParamName)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Fixed Alpha", &n->m_FixedAlpha, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Use Parameter", &n->m_UseParam)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::Blend1D:
	{
		auto* n = static_cast<AnimGraphBlend1DNode*>(node);
		if (EditStringProperty("Parameter", n->m_ParamName)) m_EditState->isDirty = true;
		ImGui::Text("Thresholds: %d", (int)n->m_Thresholds.size());
		for (int index = 0; index < static_cast<int>(n->m_Thresholds.size()); ++index)
		{
			ImGui::PushID(index);
			if (ImGui::DragFloat("##Threshold", &n->m_Thresholds[index], 0.01f))
				m_EditState->isDirty = true;
			ImGui::PopID();
		}
		break;
	}
	case AnimGraphNodeType::IfCondition:
	{
		auto* n = static_cast<AnimGraphIfConditionNode*>(node);
		const char* opStr[] = { ">", "<", "==", "!=", ">=", "<=" };
		if (EditStringProperty("Parameter", n->m_ParamName)) m_EditState->isDirty = true;
		int operation = static_cast<int>(n->m_CompareOp);
		if (ImGui::Combo("Operation", &operation, opStr, 6))
		{
			n->m_CompareOp = static_cast<CompareOp>(operation);
			m_EditState->isDirty = true;
		}
		if (ImGui::DragFloat("Float Value", &n->m_FloatVal, 0.01f)) m_EditState->isDirty = true;
		if (ImGui::InputInt("Int Value", &n->m_IntVal)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Bool Value", &n->m_BoolVal)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::Switch:
	{
		auto* n = static_cast<AnimGraphSwitchNode*>(node);
		if (EditStringProperty("Parameter", n->m_ParamName)) m_EditState->isDirty = true;
		if (ImGui::InputInt("Case Count", &n->m_CaseCount))
		{
			n->m_CaseCount = (std::max)(1, n->m_CaseCount);
			m_EditState->isDirty = true;
		}
		break;
	}
	case AnimGraphNodeType::AdditiveBlend:
	{
		auto* n = static_cast<AnimGraphAdditiveBlendNode*>(node);
		if (EditStringProperty("Parameter", n->m_ParamName)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Weight", &n->m_FixedWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Use Parameter", &n->m_UseParam)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::SpeedScale:
	{
		auto* n = static_cast<AnimGraphSpeedScaleNode*>(node);
		if (EditStringProperty("Parameter", n->m_ParamName)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Speed", &n->m_FixedSpeed, 0.01f)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Use Parameter", &n->m_UseParam)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::StateMachine:
	{
		auto* n = static_cast<AnimGraphStateMachineNode*>(node);
		auto chooseState = [&](const char* label, std::string& value, bool allowAny)
		{
			bool changed = false;
			if (ImGui::BeginCombo(label, value.empty() ? "(none)" : value.c_str()))
			{
				if (allowAny && ImGui::Selectable("Any State (*)", value == "*"))
				{
					value = "*";
					changed = true;
				}
				for (const AnimatorState& state : n->m_States)
					if (ImGui::Selectable(state.name.c_str(), state.name == value))
					{
						value = state.name;
						changed = true;
					}
				ImGui::EndCombo();
			}
			return changed;
		};
		auto chooseClip = [&](const char* label, std::string& value)
		{
			bool changed = false;
			if (ImGui::BeginCombo(label, value.empty() ? "(none)" : value.c_str()))
			{
				for (const AnimatorClipRef& clip : m_EditState->clipRefs)
					if (ImGui::Selectable(clip.name.c_str(), clip.name == value))
					{
						value = clip.name;
						changed = true;
					}
				ImGui::EndCombo();
			}
			return changed;
		};

		ImGui::SeparatorText("States");
		for (int index = 0; index < static_cast<int>(n->m_States.size()); ++index)
		{
			ImGui::PushID(index + 8100);
			if (ImGui::Selectable(n->m_States[index].name.c_str(), m_SelectedStateIndex == index))
				m_SelectedStateIndex = index;
			ImGui::PopID();
		}
		if (ImGui::SmallButton("+ State"))
		{
			std::string name = "State";
			int suffix = 2;
			while (std::any_of(n->m_States.begin(), n->m_States.end(),
				[&](const AnimatorState& state) { return state.name == name; }))
				name = "State " + std::to_string(suffix++);
			AnimatorState state;
			state.name = name;
			if (!m_EditState->clipRefs.empty()) state.clipName = m_EditState->clipRefs.front().name;
			n->m_States.push_back(std::move(state));
			m_SelectedStateIndex = static_cast<int>(n->m_States.size()) - 1;
			if (n->m_DefaultStateName.empty()) n->m_DefaultStateName = name;
			m_EditState->isDirty = true;
		}
		m_SelectedStateIndex = std::clamp(m_SelectedStateIndex, -1,
			static_cast<int>(n->m_States.size()) - 1);
		if (m_SelectedStateIndex >= 0)
		{
			AnimatorState& state = n->m_States[m_SelectedStateIndex];
			ImGui::PushID("StateInspector");
			const std::string oldName = state.name;
			if (EditStringProperty("Name", state.name))
			{
				const bool duplicate = state.name.empty() || std::any_of(n->m_States.begin(), n->m_States.end(),
					[&](const AnimatorState& candidate) { return &candidate != &state && candidate.name == state.name; });
				if (duplicate)
				{
					state.name = oldName;
					m_LastError = "State names must be unique and non-empty";
				}
				else
				{
					if (n->m_DefaultStateName == oldName) n->m_DefaultStateName = state.name;
					for (AnimatorTransition& transition : n->m_Transitions)
					{
						if (transition.fromState == oldName) transition.fromState = state.name;
						if (transition.toState == oldName) transition.toState = state.name;
					}
					m_EditState->isDirty = true;
					m_LastError.clear();
				}
			}
			if (chooseClip("Clip", state.clipName)) m_EditState->isDirty = true;
			if (ImGui::DragFloat("Speed", &state.speed, 0.01f)) m_EditState->isDirty = true;
			if (ImGui::Checkbox("Loop", &state.loop)) m_EditState->isDirty = true;
			if (ImGui::Checkbox("Root Motion", &state.rootMotion)) m_EditState->isDirty = true;
			if (ImGui::DragFloat("Start Time", &state.startTime, 0.01f, 0.0f)) m_EditState->isDirty = true;
			if (ImGui::DragFloat("End Time (-1 = Clip End)", &state.endTime, 0.01f, -1.0f)) m_EditState->isDirty = true;
			if (ImGui::RadioButton("Default State", n->m_DefaultStateName == state.name))
			{
				n->m_DefaultStateName = state.name;
				m_EditState->isDirty = true;
			}
			if (ImGui::SmallButton("Delete State"))
			{
				const std::string removed = state.name;
				n->m_States.erase(n->m_States.begin() + m_SelectedStateIndex);
				n->m_Transitions.erase(std::remove_if(n->m_Transitions.begin(), n->m_Transitions.end(),
					[&](const AnimatorTransition& transition)
					{ return transition.fromState == removed || transition.toState == removed; }), n->m_Transitions.end());
				if (n->m_DefaultStateName == removed)
					n->m_DefaultStateName = n->m_States.empty() ? "" : n->m_States.front().name;
				m_SelectedStateIndex = n->m_States.empty() ? -1
					: std::min(m_SelectedStateIndex, static_cast<int>(n->m_States.size()) - 1);
				m_SelectedTransitionIndex = -1;
				m_EditState->isDirty = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}

		ImGui::SeparatorText("Transitions");
		for (int index = 0; index < static_cast<int>(n->m_Transitions.size()); ++index)
		{
			const AnimatorTransition& transition = n->m_Transitions[index];
			const std::string label = transition.fromState + " -> " + transition.toState;
			ImGui::PushID(index + 9100);
			if (ImGui::Selectable(label.c_str(), m_SelectedTransitionIndex == index))
			{
				m_SelectedTransitionIndex = index;
				m_SelectedConditionIndex = -1;
			}
			ImGui::PopID();
		}
		if (!n->m_States.empty() && ImGui::SmallButton("+ Transition"))
		{
			AnimatorTransition transition;
			transition.fromState = n->m_States.front().name;
			transition.toState = n->m_States.size() > 1 ? n->m_States[1].name : n->m_States.front().name;
			n->m_Transitions.push_back(std::move(transition));
			m_SelectedTransitionIndex = static_cast<int>(n->m_Transitions.size()) - 1;
			m_EditState->isDirty = true;
		}
		m_SelectedTransitionIndex = std::clamp(m_SelectedTransitionIndex, -1,
			static_cast<int>(n->m_Transitions.size()) - 1);
		if (m_SelectedTransitionIndex >= 0)
		{
			AnimatorTransition& transition = n->m_Transitions[m_SelectedTransitionIndex];
			ImGui::PushID("TransitionInspector");
			if (chooseState("From", transition.fromState, true)) m_EditState->isDirty = true;
			if (chooseState("To", transition.toState, false)) m_EditState->isDirty = true;
			if (ImGui::DragFloat("Blend Duration", &transition.blendDuration, 0.01f, 0.0f)) m_EditState->isDirty = true;
			if (ImGui::Checkbox("Has Exit Time", &transition.hasExitTime)) m_EditState->isDirty = true;
			if (transition.hasExitTime && ImGui::SliderFloat("Exit Time", &transition.exitTime, 0.0f, 1.0f)) m_EditState->isDirty = true;
			ImGui::SeparatorText("Conditions (AND)");
			for (int index = 0; index < static_cast<int>(transition.conditions.size()); ++index)
			{
				ImGui::PushID(index + 10100);
				const std::string label = transition.conditions[index].paramName.empty()
					? "(unbound)" : transition.conditions[index].paramName;
				if (ImGui::Selectable(label.c_str(), m_SelectedConditionIndex == index))
					m_SelectedConditionIndex = index;
				ImGui::PopID();
			}
			if (ImGui::SmallButton("+ Condition"))
			{
				TransitionCondition condition;
				for (const AnimatorParameter& parameter : m_EditState->parameters)
					if (parameter.type != AnimatorParamType::Vector3 && parameter.type != AnimatorParamType::Quaternion)
					{
						condition.paramName = parameter.name;
						break;
					}
				transition.conditions.push_back(std::move(condition));
				m_SelectedConditionIndex = static_cast<int>(transition.conditions.size()) - 1;
				m_EditState->isDirty = true;
			}
			m_SelectedConditionIndex = std::clamp(m_SelectedConditionIndex, -1,
				static_cast<int>(transition.conditions.size()) - 1);
			if (m_SelectedConditionIndex >= 0)
			{
				TransitionCondition& condition = transition.conditions[m_SelectedConditionIndex];
				if (ImGui::BeginCombo("Parameter", condition.paramName.empty() ? "(none)" : condition.paramName.c_str()))
				{
					for (const AnimatorParameter& parameter : m_EditState->parameters)
					{
						if (parameter.type == AnimatorParamType::Vector3 || parameter.type == AnimatorParamType::Quaternion) continue;
						if (ImGui::Selectable(parameter.name.c_str(), parameter.name == condition.paramName))
						{
							condition.paramName = parameter.name;
							condition.op = CompareOp::Equal;
							m_EditState->isDirty = true;
						}
					}
					ImGui::EndCombo();
				}
				const auto parameter = std::find_if(m_EditState->parameters.begin(), m_EditState->parameters.end(),
					[&](const AnimatorParameter& value) { return value.name == condition.paramName; });
				const bool boolean = parameter != m_EditState->parameters.end()
					&& (parameter->type == AnimatorParamType::Bool || parameter->type == AnimatorParamType::Trigger);
				int operation = static_cast<int>(condition.op);
				if (boolean)
				{
					operation = condition.op == CompareOp::NotEqual ? 1 : 0;
					if (ImGui::Combo("Operation", &operation, "Equal\0Not Equal\0"))
					{
						condition.op = operation == 0 ? CompareOp::Equal : CompareOp::NotEqual;
						m_EditState->isDirty = true;
					}
					if (ImGui::Checkbox("Value", &condition.boolVal)) m_EditState->isDirty = true;
				}
				else
				{
					if (ImGui::Combo("Operation", &operation, ">\0<\0==\0!=\0>=\0<=\0"))
					{
						condition.op = static_cast<CompareOp>(operation);
						m_EditState->isDirty = true;
					}
					if (parameter != m_EditState->parameters.end() && parameter->type == AnimatorParamType::Int)
					{
						if (ImGui::InputInt("Value", &condition.intVal)) m_EditState->isDirty = true;
					}
					else if (ImGui::DragFloat("Value", &condition.floatVal, 0.01f)) m_EditState->isDirty = true;
				}
				if (ImGui::SmallButton("Delete Condition"))
				{
					transition.conditions.erase(transition.conditions.begin() + m_SelectedConditionIndex);
					m_SelectedConditionIndex = -1;
					m_EditState->isDirty = true;
				}
			}
			if (ImGui::SmallButton("Delete Transition"))
			{
				n->m_Transitions.erase(n->m_Transitions.begin() + m_SelectedTransitionIndex);
				m_SelectedTransitionIndex = -1;
				m_SelectedConditionIndex = -1;
				m_EditState->isDirty = true;
			}
			ImGui::PopID();
		}
		break;
	}
	case AnimGraphNodeType::MotionMatching:
	{
		auto* n = static_cast<AnimGraphMotionMatchingNode*>(node);
		if (ImGui::Checkbox("Fallback Input", &n->m_EnableFallbackInput)) m_EditState->isDirty = true;
		ImGui::Text("Runtime Param: UseMotionMatching");
		break;
	}
	case AnimGraphNodeType::Slot:
	{
		auto* n = static_cast<AnimGraphSlotNode*>(node);
		const std::string previousSlotId = n->m_SlotId;
		if (EditStringProperty("Slot ID", n->m_SlotId))
		{
			for (VansAnimationSlotDefinition& slot : m_AssetData->slots)
				if (slot.id == previousSlotId && slot.layerId == m_EditState->selectedLayerId
					&& slot.slotNodeId == node->GetNodeId())
				{
					slot.id = n->m_SlotId;
					m_EditState->selectedSlotId = n->m_SlotId;
					break;
				}
			m_EditState->isDirty = true;
		}
		if (ImGui::Checkbox("Fallback Input", &n->m_EnableFallbackInput)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::IK:
	{
		auto* n = static_cast<AnimGraphIKNode*>(node);
		if (EditStringProperty("Chain Name", n->m_Chain.chainName)) m_EditState->isDirty = true;
		int solver = static_cast<int>(n->m_Chain.solverType);
		if (ImGui::Combo("Solver", &solver, "Two Bone\0CCD\0FABRIK\0Look At\0"))
		{
			n->m_Chain.solverType = static_cast<IKSolverType>(solver);
			m_EditState->isDirty = true;
		}
		int profile = static_cast<int>(n->m_Chain.profileType);
		if (ImGui::Combo("Profile", &profile,
			"Custom\0Humanoid Arm\0Humanoid Leg\0Humanoid Spine\0Humanoid Head\0Tail\0Tentacle\0Rope\0"))
		{
			n->m_Chain.profileType = static_cast<IKProfileType>(profile);
			m_EditState->isDirty = true;
		}
		if (ImGui::InputInt("Max Iterations", &n->m_Chain.maxIterations)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Position Tolerance", &n->m_Chain.positionTolerance, 0.0001f, 0.000001f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Rotation Tolerance", &n->m_Chain.rotationTolerance, 0.001f, 0.0001f)) m_EditState->isDirty = true;
		if (ImGui::InputInt("Solve Priority", &n->m_Chain.solvePriority)) m_EditState->isDirty = true;

		ImGui::SeparatorText("Ordered Chain");
		for (int index = 0; index < static_cast<int>(n->m_Chain.bones.size()); ++index)
		{
			IKBoneLink& link = n->m_Chain.bones[index];
			ImGui::PushID(index + 12100);
			const std::string header = std::to_string(index) + ": "
				+ (link.boneName.empty() ? "(unbound)" : link.boneName);
			if (ImGui::TreeNodeEx("##IKBone", ImGuiTreeNodeFlags_DefaultOpen, "%s", header.c_str()))
			{
				if (EditStringProperty("Bone", link.boneName))
				{
					link.boneIndex = -1;
					m_EditState->isDirty = true;
				}
				if (ImGui::SliderFloat("Stiffness Weight", &link.stiffnessWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
				if (ImGui::Checkbox("Effector", &link.isEffector)) m_EditState->isDirty = true;
				int constraint = static_cast<int>(link.constraint.type);
				if (ImGui::Combo("Constraint", &constraint,
					"None\0Ball Socket\0Hinge\0Angle Limit\0Twist Limit\0Locked\0"))
				{
					link.constraint.type = static_cast<JointConstraintType>(constraint);
					m_EditState->isDirty = true;
				}
				if (ImGui::DragFloat3("Local X Axis", &link.constraint.localXAxis.x, 0.01f)) m_EditState->isDirty = true;
				if (ImGui::DragFloat3("Local Y Axis", &link.constraint.localYAxis.x, 0.01f)) m_EditState->isDirty = true;
				if (ImGui::DragFloat3("Local Z Axis", &link.constraint.localZAxis.x, 0.01f)) m_EditState->isDirty = true;
				if (ImGui::DragFloat2("X Min / Max", &link.constraint.minAngleX, 0.5f, -180.0f, 180.0f)) m_EditState->isDirty = true;
				if (ImGui::DragFloat2("Y Min / Max", &link.constraint.minAngleY, 0.5f, -180.0f, 180.0f)) m_EditState->isDirty = true;
				if (ImGui::DragFloat2("Z Min / Max", &link.constraint.minAngleZ, 0.5f, -180.0f, 180.0f)) m_EditState->isDirty = true;
				if (ImGui::SliderFloat("Cone Angle", &link.constraint.coneAngleDeg, 0.0f, 180.0f)) m_EditState->isDirty = true;
				if (ImGui::SliderFloat("Constraint Stiffness", &link.constraint.stiffness, 0.0f, 1.0f)) m_EditState->isDirty = true;
				if (ImGui::SmallButton("Remove Bone"))
				{
					n->m_Chain.bones.erase(n->m_Chain.bones.begin() + index);
					m_EditState->isDirty = true;
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (ImGui::SmallButton("+ Chain Bone"))
		{
			IKBoneLink link;
			link.isEffector = n->m_Chain.bones.empty();
			if (!n->m_Chain.bones.empty()) n->m_Chain.bones.back().isEffector = false;
			n->m_Chain.bones.push_back(std::move(link));
			m_EditState->isDirty = true;
		}

		ImGui::SeparatorText("Target");
		if (ImGui::Checkbox("Use Fixed Target", &n->m_UseFixedTarget)) m_EditState->isDirty = true;
		if (n->m_UseFixedTarget)
		{
			if (ImGui::DragFloat3("Position", &n->m_FixedTargetPos.x, 0.01f)) m_EditState->isDirty = true;
			if (ImGui::DragFloat4("Rotation (xyzw)", &n->m_FixedTargetRot.x, 0.01f)) m_EditState->isDirty = true;
			if (ImGui::SliderFloat("Weight", &n->m_FixedWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		}
		else
		{
			if (editParameterBinding("Position Parameter", n->m_TargetPosParamName, AnimatorParamType::Vector3, false)) m_EditState->isDirty = true;
			if (editParameterBinding("Rotation Parameter", n->m_TargetRotParamName, AnimatorParamType::Quaternion)) m_EditState->isDirty = true;
			if (editParameterBinding("Weight Parameter", n->m_WeightParamName, AnimatorParamType::Float)) m_EditState->isDirty = true;
		}
		if (editCoordinateSpace("Position Space", n->m_TargetPositionSpace)) m_EditState->isDirty = true;
		if (editCoordinateSpace("Rotation Space", n->m_TargetRotationSpace)) m_EditState->isDirty = true;
		if (editTargetReference(n->m_TargetPositionSpace, "Target Reference Bone", n->m_TargetReferenceBoneName)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Rotation Target", &n->m_Chain.enableRotationTarget)) m_EditState->isDirty = true;
		if (n->m_Chain.enableRotationTarget
			&& ImGui::SliderFloat("Rotation Weight", &n->m_Chain.rotationWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Maintain Effector Rotation", &n->m_Chain.maintainEffectorGlobalRotation)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Allow Stretch", &n->m_Chain.allowStretch)) m_EditState->isDirty = true;
		if (n->m_Chain.allowStretch)
		{
			if (ImGui::DragFloat("Start Stretch Ratio", &n->m_Chain.startStretchRatio, 0.01f, 0.0f)) m_EditState->isDirty = true;
			if (ImGui::DragFloat("Max Stretch Scale", &n->m_Chain.maxStretchScale, 0.01f, 1.0f)) m_EditState->isDirty = true;
		}
		if (ImGui::DragFloat3("Pole Vector", &n->m_Chain.poleVector.x, 0.01f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Pole Weight", &n->m_Chain.poleWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (editCoordinateSpace("Pole Space", n->m_Chain.poleSpace)) m_EditState->isDirty = true;
		if (editTargetReference(n->m_Chain.poleSpace, "Pole Reference Bone", n->m_Chain.poleReferenceBoneName)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::TwoBoneIK:
	{
		auto* n = static_cast<AnimGraphTwoBoneIKNode*>(node);
		if (ImGui::Checkbox("Leg Profile", &n->m_UseLegProfile)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Right Side", &n->m_IsRightSide)) m_EditState->isDirty = true;
		if (EditStringProperty("Root Bone", n->m_RootBoneName)) m_EditState->isDirty = true;
		if (EditStringProperty("Mid Bone", n->m_MidBoneName)) m_EditState->isDirty = true;
		if (EditStringProperty("Tip Bone", n->m_TipBoneName)) m_EditState->isDirty = true;
		if (ImGui::DragFloat2("Hinge Min / Max", &n->m_HingeMinAngle, 0.5f, -180.0f, 180.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Cone Angle", &n->m_ConeAngle, 0.0f, 180.0f)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Use Pole Vector", &n->m_UsePoleVector)) m_EditState->isDirty = true;
		if (n->m_UsePoleVector)
		{
			if (ImGui::DragFloat3("Pole Vector", &n->m_PoleVector.x, 0.01f)) m_EditState->isDirty = true;
			if (ImGui::SliderFloat("Pole Weight", &n->m_PoleWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
			if (editCoordinateSpace("Pole Space", n->m_PoleSpace)) m_EditState->isDirty = true;
			if (editTargetReference(n->m_PoleSpace, "Pole Reference Bone", n->m_PoleReferenceBoneName)) m_EditState->isDirty = true;
		}
		if (ImGui::Checkbox("Use Fixed Target", &n->m_UseFixedTarget)) m_EditState->isDirty = true;
		if (n->m_UseFixedTarget)
		{
			if (ImGui::DragFloat3("Position", &n->m_FixedTargetPos.x, 0.01f)) m_EditState->isDirty = true;
			if (ImGui::DragFloat4("Rotation (xyzw)", &n->m_FixedTargetRot.x, 0.01f)) m_EditState->isDirty = true;
			if (ImGui::SliderFloat("Weight", &n->m_FixedWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		}
		else
		{
			if (editParameterBinding("Position Parameter", n->m_TargetPosParamName, AnimatorParamType::Vector3, false)) m_EditState->isDirty = true;
			if (editParameterBinding("Rotation Parameter", n->m_TargetRotParamName, AnimatorParamType::Quaternion)) m_EditState->isDirty = true;
			if (editParameterBinding("Weight Parameter", n->m_WeightParamName, AnimatorParamType::Float)) m_EditState->isDirty = true;
		}
		if (editCoordinateSpace("Position Space", n->m_TargetPositionSpace)) m_EditState->isDirty = true;
		if (editCoordinateSpace("Rotation Space", n->m_TargetRotationSpace)) m_EditState->isDirty = true;
		if (editTargetReference(n->m_TargetPositionSpace, "Target Reference Bone", n->m_TargetReferenceBoneName)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Rotation Target", &n->m_EnableRotationTarget)) m_EditState->isDirty = true;
		if (n->m_EnableRotationTarget
			&& ImGui::SliderFloat("Rotation Weight", &n->m_RotationWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Maintain Effector Rotation", &n->m_MaintainEffectorGlobalRotation)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Allow Stretch", &n->m_AllowStretch)) m_EditState->isDirty = true;
		if (n->m_AllowStretch)
		{
			if (ImGui::DragFloat("Start Stretch Ratio", &n->m_StartStretchRatio, 0.01f, 0.0f)) m_EditState->isDirty = true;
			if (ImGui::DragFloat("Max Stretch Scale", &n->m_MaxStretchScale, 0.01f, 1.0f)) m_EditState->isDirty = true;
		}
		break;
	}
	case AnimGraphNodeType::LookAt:
	{
		auto* n = static_cast<AnimGraphLookAtNode*>(node);
		ImGui::SeparatorText("Ordered Bone Chain");
		for (int index = 0; index < static_cast<int>(n->m_BoneNames.size()); ++index)
		{
			ImGui::PushID(index + 13100);
			if (EditStringProperty("Bone", n->m_BoneNames[index])) m_EditState->isDirty = true;
			if (index >= static_cast<int>(n->m_BoneWeights.size())) n->m_BoneWeights.resize(n->m_BoneNames.size(), 1.0f);
			if (ImGui::SliderFloat("Weight", &n->m_BoneWeights[index], 0.0f, 1.0f)) m_EditState->isDirty = true;
			if (ImGui::SmallButton("Remove"))
			{
				n->m_BoneNames.erase(n->m_BoneNames.begin() + index);
				if (index < static_cast<int>(n->m_BoneWeights.size())) n->m_BoneWeights.erase(n->m_BoneWeights.begin() + index);
				m_EditState->isDirty = true;
				ImGui::PopID();
				break;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (ImGui::SmallButton("+ LookAt Bone"))
		{
			n->m_BoneNames.emplace_back();
			n->m_BoneWeights.push_back(1.0f);
			m_EditState->isDirty = true;
		}
		if (ImGui::SliderFloat("Max Angle Per Bone", &n->m_MaxAnglePerBoneDeg, 0.0f, 180.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat3("Forward Axis", &n->m_ForwardAxis.x, 0.01f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat3("World Forward (optional)", &n->m_WorldForward.x, 0.01f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat3("Model Up", &n->m_ModelUp.x, 0.01f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Up Weight", &n->m_UpWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Use Fixed Target", &n->m_UseFixedTarget)) m_EditState->isDirty = true;
		if (n->m_UseFixedTarget)
		{
			if (ImGui::DragFloat3("Position", &n->m_FixedTargetPos.x, 0.01f)) m_EditState->isDirty = true;
			if (ImGui::SliderFloat("Weight", &n->m_FixedWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		}
		else
		{
			if (editParameterBinding("Position Parameter", n->m_TargetPosParamName, AnimatorParamType::Vector3, false)) m_EditState->isDirty = true;
			if (editParameterBinding("Weight Parameter", n->m_WeightParamName, AnimatorParamType::Float)) m_EditState->isDirty = true;
		}
		if (editCoordinateSpace("Position Space", n->m_TargetPositionSpace)) m_EditState->isDirty = true;
		if (editTargetReference(n->m_TargetPositionSpace, "Target Reference Bone", n->m_TargetReferenceBoneName)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::FootPlacement:
	{
		auto* n = static_cast<AnimGraphFootPlacementNode*>(node);
		FootPlacementSettings& settings = n->m_Settings;
		if (ImGui::Checkbox("Enabled", &settings.enabled)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Probe Origin Height", &settings.probeOriginHeight, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Probe Length", &settings.probeLength, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Foot Half Length", &settings.footHalfLength, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Foot Half Width", &settings.footHalfWidth, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Ankle Height", &settings.ankleHeight, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Full Contact Height", &settings.fullContactHeight, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Contact Fade Height", &settings.contactFadeHeight, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Max Step Up", &settings.maxStepUp, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Max Step Down", &settings.maxStepDown, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Max Slope", &settings.maxSlopeDeg, 0.0f, 89.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Pelvis Max Drop", &settings.pelvisMaxDrop, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Pelvis Smooth Time", &settings.pelvisSmoothTime, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Offset Smooth Time", &settings.offsetSmoothTime, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Normal Smooth Time", &settings.normalSmoothTime, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Weight Smooth Time", &settings.weightSmoothTime, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Global Weight Smooth", &settings.globalWeightSmoothTime, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("IK Weight", &settings.ikWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Rotation Weight", &settings.rotationWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Max Leg Extension Ratio", &settings.maxLegExtensionRatio, 0.0f, 1.5f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat3("Knee Pole Model Direction", &settings.kneePoleModelDir.x, 0.01f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Knee Pole Weight", &settings.kneePoleModelWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Debug Visualization", &settings.debugVisualization)) m_EditState->isDirty = true;
		int collisionMask = static_cast<int>(settings.collisionMask);
		if (ImGui::InputInt("Collision Mask", &collisionMask))
		{
			settings.collisionMask = static_cast<std::uint32_t>(collisionMask);
			m_EditState->isDirty = true;
		}
		if (editParameterBinding("Airborne Parameter", settings.airborneParameter, AnimatorParamType::Bool)) m_EditState->isDirty = true;
		ImGui::SeparatorText("Humanoid Bones");
		if (EditStringProperty("Pelvis", settings.bones.pelvis)) m_EditState->isDirty = true;
		if (EditStringProperty("Left Hip", settings.bones.leftHip)) m_EditState->isDirty = true;
		if (EditStringProperty("Left Knee", settings.bones.leftKnee)) m_EditState->isDirty = true;
		if (EditStringProperty("Left Foot", settings.bones.leftFoot)) m_EditState->isDirty = true;
		if (EditStringProperty("Right Hip", settings.bones.rightHip)) m_EditState->isDirty = true;
		if (EditStringProperty("Right Knee", settings.bones.rightKnee)) m_EditState->isDirty = true;
		if (EditStringProperty("Right Foot", settings.bones.rightFoot)) m_EditState->isDirty = true;
		break;
	}
	default:
		ImGui::TextDisabled("No editable properties.");
		break;
	}
}
void VansAnimGraphEditorWindow::SyncSelection()
{
	if (!ne::HasSelectionChanged()) return;
	m_EditState->selectedNodeId = -1;
	m_EditState->selectedLinkId = -1;
	int selCount = ne::GetSelectedObjectCount();
	if (selCount > 0)
	{
		std::vector<ne::NodeId> selectedNodes(selCount);
		int count = ne::GetSelectedNodes(selectedNodes.data(), selCount);
		if (count > 0)
			m_EditState->selectedNodeId = (int)selectedNodes[0].Get();
		std::vector<ne::LinkId> selectedLinks(selCount);
		int linkCount = ne::GetSelectedLinks(selectedLinks.data(), selCount);
		if (linkCount > 0)
			m_EditState->selectedLinkId = (int)selectedLinks[0].Get() - 1000000;
	}
}
// ============================================================================
//  AnimGraph layout
// ============================================================================
void VansAnimGraphEditorWindow::ApplyNodePositions()
{
	if (!m_TargetGraph) return;
	for (auto& [id, node] : m_TargetGraph->GetNodes())
	{
		ne::SetNodePosition(
			AnimGraphIds::MakeNodeId(id),
			ImVec2(node->m_EditorPosX, node->m_EditorPosY));
	}
}
void VansAnimGraphEditorWindow::ReadNodePositions()
{
	if (!m_TargetGraph) return;
	for (auto& [id, node] : m_TargetGraph->GetNodes())
	{
		ImVec2 pos = ne::GetNodePosition(AnimGraphIds::MakeNodeId(id));
		if (std::fabs(node->m_EditorPosX - pos.x) > 0.01f
		    || std::fabs(node->m_EditorPosY - pos.y) > 0.01f)
			m_EditState->isDirty = true;
		node->m_EditorPosX = pos.x;
		node->m_EditorPosY = pos.y;
	}
}
// ============================================================================
// ============================================================================
// ============================================================================
bool VansAnimGraphEditorWindow::CommitWorkingCopyToDocument()
{
	if (!m_EditState->isDirty)
		return true;
	if (!m_AssetData || !m_Document || !m_ActiveAPI)
	{
		m_LastError = "Animator authoring API is not available";
		return false;
	}
	m_AssetData->name = m_EditState->name;
	m_AssetData->parameters = m_EditState->parameters;
	m_AssetData->clipRefs = m_EditState->clipRefs;

	const auto encoded = m_ActiveAPI->EncodeAnimatorDocument(*m_AssetData);
	if (!encoded.success)
	{
		m_LastError = encoded.message;
		return false;
	}
	const nlohmann::json canonicalRoot = nlohmann::json::parse(encoded.canonicalJson);
	const Vans::AssetDocumentEditResult editResult = Vans::VansAssetDocumentEditService::ReplaceRoot(
		m_Document->sourceDocument,
		Vans::DecodeSerializedValueJson(canonicalRoot));
	if (!editResult && editResult.message != "Asset property is unchanged")
	{
		m_LastError = editResult.message;
		return false;
	}
	m_EditState->isDirty = false;
	m_DocumentStateId = m_Document->sourceDocument.CurrentStateId();
	m_LastError.clear();
	return true;
}

bool VansAnimGraphEditorWindow::ReloadWorkingCopyFromDocument()
{
	if (!m_Document || !m_Document->sourceDocument.IsLoaded() || !m_ActiveAPI)
		return false;
	const nlohmann::json root = Vans::EncodeSerializedValueJson<nlohmann::json>(
		m_Document->sourceDocument.SerializedRootSnapshot());
	auto decoded = m_ActiveAPI->DecodeAnimatorDocument(root.dump());
	if (!decoded.success || !decoded.document)
	{
		m_LastError = decoded.message;
		return false;
	}
	auto reloaded = std::move(decoded.document);
	if (reloaded->layers.empty())
	{
		m_LastError = "Animator has no Base Layer";
		return false;
	}
	const std::string previousGraphId = m_ActiveGraphId;
	const std::string previousLayerId = m_EditState->selectedLayerId;
	m_AssetData = std::move(reloaded);
	m_EditState->name = m_AssetData->name;
	m_EditState->parameters = m_AssetData->parameters;
	m_EditState->clipRefs = m_AssetData->clipRefs;
	m_EditState->selectedLayerId = previousLayerId.empty()
		? m_AssetData->layers.front().id : previousLayerId;
	if (std::none_of(m_AssetData->layers.begin(), m_AssetData->layers.end(),
		[&](const VansAnimationLayerDefinition& layer) { return layer.id == previousLayerId; }))
		m_EditState->selectedLayerId = m_AssetData->layers.front().id;
	if (std::none_of(m_AssetData->slots.begin(), m_AssetData->slots.end(),
		[&](const VansAnimationSlotDefinition& slot) { return slot.id == m_EditState->selectedSlotId; }))
		m_EditState->selectedSlotId.clear();
	m_ActiveGraphId = previousGraphId.empty()
		? m_AssetData->layers.front().graphId : previousGraphId;
	m_TargetGraph = m_AssetData->FindGraph(m_ActiveGraphId);
	if (!m_TargetGraph)
	{
		m_ActiveGraphId = m_AssetData->layers.front().graphId;
		m_TargetGraph = m_AssetData->FindGraph(m_ActiveGraphId);
	}
	m_EditState->selectedNodeId = -1;
	m_EditState->selectedLinkId = -1;
	m_EditState->needsInitialLayout = true;
	m_EditState->isDirty = false;
	m_DocumentStateId = m_Document->sourceDocument.CurrentStateId();
	m_NavigationStack.clear();
	m_LastError.clear();
	return m_TargetGraph != nullptr;
}

bool VansAnimGraphEditorWindow::Undo()
{
	if (!m_Document)
		return false;
	if (m_EditState->isDirty && !CommitWorkingCopyToDocument())
	{
		// 无效的瞬时工作副本尚未进入共享文档，Undo 先恢复最后一份有效快照。
		return ReloadWorkingCopyFromDocument();
	}
	const Vans::AssetDocumentEditResult result =
		Vans::VansAssetDocumentEditService::Undo(m_Document->sourceDocument);
	if (!result)
	{
		m_LastError = result.message;
		return false;
	}
	return ReloadWorkingCopyFromDocument();
}

bool VansAnimGraphEditorWindow::Redo()
{
	if (!m_Document)
		return false;
	if (m_EditState->isDirty && !CommitWorkingCopyToDocument())
		return false;
	const Vans::AssetDocumentEditResult result =
		Vans::VansAssetDocumentEditService::Redo(m_Document->sourceDocument);
	if (!result)
	{
		m_LastError = result.message;
		return false;
	}
	return ReloadWorkingCopyFromDocument();
}

bool VansAnimGraphEditorWindow::Save()
{
	if (!m_EditState->isDirty)
	{
		if (!m_Document || !m_Document->IsDirty())
			return true;
		if (!m_ActiveAPI)
		{
			m_LastError = "Animator save API is not available";
			return false;
		}
		const Vans::VansAssetSaveResult pendingSave =
			Vans::VansEditorAssetSaveService::Get().SaveAsset(*m_ActiveAPI, m_Document);
		if (!pendingSave)
		{
			m_LastError = pendingSave.message.empty() ? "Animator save failed" : pendingSave.message;
			return false;
		}
		m_LastError.clear();
		return true;
	}
	if (!m_AssetData || !m_TargetGraph || !m_Document || !m_ActiveAPI)
	{
		m_LastError = "Animator document is not available";
		return false;
	}
	if (m_NodeEditorCtx)
	{
		ne::SetCurrentEditor(m_NodeEditorCtx);
		ReadNodePositions();
		ne::SetCurrentEditor(nullptr);
	}

	if (!CommitWorkingCopyToDocument())
	{
		VANS_LOG_ERROR("[AnimGraphEditor] Validation failed: " << m_LastError);
		return false;
	}

	const Vans::VansAssetSaveResult saveResult =
		Vans::VansEditorAssetSaveService::Get().SaveAsset(*m_ActiveAPI, m_Document);
	if (!saveResult)
	{
		m_LastError = saveResult.message.empty() ? "Animator save failed" : saveResult.message;
		VANS_LOG_ERROR("[AnimGraphEditor] Save failed: " << m_LastError);
		return false;
	}

	m_EditState->isDirty = false;
	m_LastError.clear();
	return true;
}
