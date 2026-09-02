#include "VansAnimGraphEditorWindow.h"
#include "../VansAssetDocumentEditService.h"
#include "../VansAssetDocumentRegistry.h"
#include "../VansEditorAssetSaveService.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../../Util/VansLog.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
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
	using AnimGraphGoalNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphAimConstraintNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphGroundingNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphLimbIKNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphChainIKNode = Vans::EditorAPI::AnimationNodeDTO;
	using AnimGraphNodeType = Vans::EditorAPI::AnimGraphNodeType;
	using AnimGraphPin = Vans::EditorAPI::AnimGraphPinDTO;
	using AnimGraphPinKind = Vans::EditorAPI::AnimGraphPinKind;
	using CompareOp = Vans::EditorAPI::CompareOp;
	using VansAnimationLayerDefinition = Vans::EditorAPI::AnimationLayerDTO;
	using VansAnimationGraphSetDefinition = Vans::EditorAPI::AnimationGraphSetDTO;
	using VansAnimationGraphBindingDefinition = Vans::EditorAPI::AnimationGraphBindingDTO;
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
	using AnimationGoalSource = Vans::EditorAPI::AnimationGoalSource;
	using AnimationGoalDefinition = Vans::EditorAPI::AnimationGoalDefinitionDTO;
	using AnimationPlantPivot = Vans::EditorAPI::AnimationPlantPivot;
	using AnimationLimbTipRotationMode = Vans::EditorAPI::AnimationLimbTipRotationMode;
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
		std::string selectedGraphSetId;
		std::string selectedSlotId;
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
	m_EditState->selectedGraphSetId.clear();
	m_EditState->selectedSlotId.clear();
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
	// ============================================================================
	// ?????????????????????
	if (m_CloseRequested)
		ImGui::OpenPopup("UnsavedChanges");
	bool closeAfterPopup = false;
	if (ImGui::BeginPopupModal("UnsavedChanges", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("You have unsaved changes. Save before closing?");
		if (ImGui::Button("Save") && Save())
		{
			ImGui::CloseCurrentPopup();
			closeAfterPopup = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard"))
		{
			if (!m_Document || Vans::VansAssetDocumentEditService::RevertToSaved(m_Document->sourceDocument))
			{
				ImGui::CloseCurrentPopup();
				closeAfterPopup = true;
			}
			else
				m_LastError = "Unable to discard Animator document edits";
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) { m_CloseRequested = false; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	if (closeAfterPopup)
	{
		CloseImmediately();
		ImGui::End();
		return;
	}
	DrawMenuBar();
	DrawNavigationBar();
	float leftPanelWidth = 220.0f;
	ImGui::BeginChild("LeftPanel", ImVec2(leftPanelWidth, -ImGui::GetFrameHeightWithSpacing()),
	                   ImGuiChildFlags_Borders);
	DrawLeftPanel();
	ImGui::EndChild();
	ImGui::SameLine();
	// Animation Graph Editor只负责Animator资产的图编辑。场景动画预览由
	// Animation/Scene Animation Preview独立窗口持有，避免编辑文档与场景
	// 临时状态共享生命周期。
	ImGui::BeginChild("AnimationWorkspace", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
	ImGui::BeginChild("GraphCanvas", ImVec2(0, 0));
	DrawGraphCanvas();
	ImGui::EndChild();
	ImGui::EndChild();
	DrawStatusBar();
	if (m_EditState->isDirty && !ImGui::IsAnyItemActive())
		CommitWorkingCopyToDocument();
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
	auto findBinding = [&](VansAnimationGraphSetDefinition& graphSet,
		const std::string& layerId) -> VansAnimationGraphBindingDefinition*
	{
		for (auto& binding : graphSet.bindings)
			if (binding.layerId == layerId) return &binding;
		return nullptr;
	};
	auto editTransitionPolicy = [](const char* id, auto& transition)
	{
		bool changed = false;
		ImGui::PushID(id);
		changed |= ImGui::DragFloat("Duration", &transition.duration, 0.01f, 0.0f, 10.0f);
		int curve = static_cast<int>(transition.curve);
		if (ImGui::Combo("Curve", &curve, "Linear\0Smooth Step\0"))
		{
			transition.curve = static_cast<Vans::EditorAPI::VansGraphSetBlendCurve>(curve);
			changed = true;
		}
		int phase = static_cast<int>(transition.phase);
		if (ImGui::Combo("Phase Handoff", &phase, "Restart\0Normalized Time\0Marker Sync\0"))
		{
			transition.phase = static_cast<Vans::EditorAPI::VansGraphSetPhasePolicy>(phase);
			changed = true;
		}
		int events = static_cast<int>(transition.events);
		if (ImGui::Combo("Transition Events", &events, "Dominant Source\0Weighted Both\0"))
		{
			transition.events = static_cast<Vans::EditorAPI::VansGraphSetEventPolicy>(events);
			changed = true;
		}
		int rootMotion = static_cast<int>(transition.rootMotion);
		if (ImGui::Combo("Transition Root Motion", &rootMotion,
			"Blend\0Dominant Source\0Incoming Only\0"))
		{
			transition.rootMotion = static_cast<Vans::EditorAPI::VansGraphSetRootMotionPolicy>(rootMotion);
			changed = true;
		}
		int interruption = static_cast<int>(transition.interruption);
		if (ImGui::Combo("Interruption", &interruption, "Queue Latest\0Reject\0Force\0"))
		{
			transition.interruption = static_cast<Vans::EditorAPI::VansGraphSetInterruptionPolicy>(interruption);
			changed = true;
		}
		changed |= ImGui::Checkbox("Require State Match", &transition.requireStateMatch);
		ImGui::PopID();
		return changed;
	};
	if (m_EditState->selectedGraphSetId.empty())
		m_EditState->selectedGraphSetId = m_AssetData->defaultGraphSetId;
	auto selectedGraphSet = [&]()
	{
		return std::find_if(m_AssetData->graphSets.begin(), m_AssetData->graphSets.end(),
			[&](const VansAnimationGraphSetDefinition& graphSet)
			{ return graphSet.id == m_EditState->selectedGraphSetId; });
	};
	if (selectedGraphSet() == m_AssetData->graphSets.end() && !m_AssetData->graphSets.empty())
		m_EditState->selectedGraphSetId = m_AssetData->graphSets.front().id;

	ImGui::SeparatorText("Graph Sets");
	for (std::size_t index = 0; index < m_AssetData->graphSets.size(); ++index)
	{
		auto& graphSet = m_AssetData->graphSets[index];
		ImGui::PushID(static_cast<int>(index) + 3000);
		const std::string label = (graphSet.id == m_AssetData->defaultGraphSetId ? "[Default] " : "")
			+ graphSet.name;
		if (ImGui::Selectable(label.c_str(), graphSet.id == m_EditState->selectedGraphSetId))
		{
			m_EditState->selectedGraphSetId = graphSet.id;
			if (auto* binding = findBinding(graphSet, m_EditState->selectedLayerId);
				binding && binding->enabled) activateGraph(binding->graphId);
		}
		ImGui::PopID();
	}
	if (ImGui::SmallButton("+ Graph Set"))
	{
		VansAnimationGraphSetDefinition graphSet;
		graphSet.id = makeUniqueId("graph-set", [&](const std::string& id)
			{ return std::any_of(m_AssetData->graphSets.begin(), m_AssetData->graphSets.end(),
				[&](const auto& value) { return value.id == id; }); });
		graphSet.name = "Graph Set";
		if (auto source = selectedGraphSet(); source != m_AssetData->graphSets.end())
			graphSet.bindings = source->bindings;
		else
			for (const auto& layer : m_AssetData->layers)
				graphSet.bindings.push_back({ layer.id, {}, false });
		m_EditState->selectedGraphSetId = graphSet.id;
		if (m_AssetData->graphSets.empty()) m_AssetData->defaultGraphSetId = graphSet.id;
		m_AssetData->graphSets.push_back(std::move(graphSet));
		m_EditState->isDirty = true;
	}
	auto activeSet = selectedGraphSet();
	if (activeSet != m_AssetData->graphSets.end())
	{
		ImGui::SameLine();
		if (ImGui::SmallButton("Make Default"))
		{
			m_AssetData->defaultGraphSetId = activeSet->id;
			m_EditState->isDirty = true;
		}
		ImGui::SameLine();
		if (m_AssetData->graphSets.size() <= 1) ImGui::BeginDisabled();
		if (ImGui::SmallButton("Delete Graph Set"))
		{
			const std::string removedId = activeSet->id;
			m_AssetData->graphSets.erase(activeSet);
			m_AssetData->graphSetTransitionRules.erase(
				std::remove_if(m_AssetData->graphSetTransitionRules.begin(),
					m_AssetData->graphSetTransitionRules.end(), [&](const auto& rule)
					{ return rule.fromGraphSetId == removedId || rule.toGraphSetId == removedId; }),
				m_AssetData->graphSetTransitionRules.end());
			if (m_AssetData->defaultGraphSetId == removedId)
				m_AssetData->defaultGraphSetId = m_AssetData->graphSets.front().id;
			m_EditState->selectedGraphSetId = m_AssetData->defaultGraphSetId;
			m_EditState->isDirty = true;
			return;
		}
		if (m_AssetData->graphSets.size() <= 1) ImGui::EndDisabled();
		if (EditStringProperty("Graph Set Name", activeSet->name)) m_EditState->isDirty = true;
		ImGui::SeparatorText("Default Transition");
		if (editTransitionPolicy("default", m_AssetData->defaultGraphSetTransition))
			m_EditState->isDirty = true;

		ImGui::SeparatorText("Transition Rules");
		for (const auto& targetSet : m_AssetData->graphSets)
		{
			if (targetSet.id == activeSet->id)
				continue;
			const bool exists = std::any_of(
				m_AssetData->graphSetTransitionRules.begin(),
				m_AssetData->graphSetTransitionRules.end(),
				[&](const auto& rule)
				{ return rule.fromGraphSetId == activeSet->id && rule.toGraphSetId == targetSet.id; });
			if (exists)
				continue;
			ImGui::PushID(targetSet.id.c_str());
			const std::string label = "+ Rule -> " + targetSet.name;
			if (ImGui::SmallButton(label.c_str()))
			{
				Vans::EditorAPI::GraphSetTransitionRuleDTO rule;
				rule.fromGraphSetId = activeSet->id;
				rule.toGraphSetId = targetSet.id;
				rule.policy = m_AssetData->defaultGraphSetTransition;
				m_AssetData->graphSetTransitionRules.push_back(std::move(rule));
				m_EditState->isDirty = true;
			}
			ImGui::PopID();
		}
		for (std::size_t ruleIndex = 0;
			ruleIndex < m_AssetData->graphSetTransitionRules.size();)
		{
			auto& rule = m_AssetData->graphSetTransitionRules[ruleIndex];
			if (rule.fromGraphSetId != activeSet->id)
			{
				++ruleIndex;
				continue;
			}
			const auto target = std::find_if(m_AssetData->graphSets.begin(),
				m_AssetData->graphSets.end(), [&](const auto& graphSet)
				{ return graphSet.id == rule.toGraphSetId; });
			const std::string label = "To " + (target != m_AssetData->graphSets.end()
				? target->name : rule.toGraphSetId);
			ImGui::PushID(static_cast<int>(ruleIndex) + 5000);
			bool removed = false;
			if (ImGui::TreeNode(label.c_str()))
			{
				if (editTransitionPolicy("rule", rule.policy))
					m_EditState->isDirty = true;
				if (ImGui::SmallButton("Delete Rule"))
				{
					m_AssetData->graphSetTransitionRules.erase(
						m_AssetData->graphSetTransitionRules.begin() + ruleIndex);
					m_EditState->isDirty = true;
					removed = true;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
			if (!removed)
				++ruleIndex;
		}
	}
	ImGui::SeparatorText("Layer Stack");

	for (std::size_t index = 0; index < m_AssetData->layers.size(); ++index)
	{
		VansAnimationLayerDefinition& layer = m_AssetData->layers[index];
		ImGui::PushID(static_cast<int>(index) + 4000);
		const bool selected = layer.id == m_EditState->selectedLayerId;
		const std::string label = (layer.kind == VansAnimationLayerKind::Base ? "[Base] " : "[Overlay] ") + layer.name;
		if (ImGui::Selectable(label.c_str(), selected))
		{
			m_EditState->selectedLayerId = layer.id;
			if (auto graphSet = selectedGraphSet(); graphSet != m_AssetData->graphSets.end())
				if (auto* binding = findBinding(*graphSet, layer.id); binding && binding->enabled)
					activateGraph(binding->graphId);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Mask: %s", layer.maskPathHint.c_str());
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
		layer.kind = VansAnimationLayerKind::Overlay;
		m_AssetData->layers.push_back(std::move(layer));
		for (auto& graphSet : m_AssetData->graphSets)
		{
			const bool enabled = graphSet.id == m_EditState->selectedGraphSetId;
			graphSet.bindings.push_back({ layerId, enabled ? graphId : std::string{}, enabled });
		}
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
			const std::string layerId = makeUniqueId(selectedLayer.id + "-copy", [&](const std::string& id)
			{
				return std::any_of(m_AssetData->layers.begin(), m_AssetData->layers.end(),
					[&](const VansAnimationLayerDefinition& layer) { return layer.id == id; });
			});
			struct PendingDuplicate
			{
				VansAnimationGraphBindingDefinition binding;
				std::optional<AnimatorGraphAsset> graph;
			};
			std::vector<PendingDuplicate> pending;
			pending.reserve(m_AssetData->graphSets.size());
			std::unordered_set<std::string> reservedGraphIds;
			std::string graphToActivate;
			for (const auto& graphSet : m_AssetData->graphSets)
			{
				PendingDuplicate duplicate;
				duplicate.binding = { layerId, {}, false };
				const VansAnimationGraphBindingDefinition sourceBinding = graphSet.bindings[selectedIndex];
				if (sourceBinding.enabled)
				{
					const VansAnimGraph* sourceGraph = m_AssetData->FindGraph(sourceBinding.graphId);
					auto graphCopy = sourceGraph ? VansAnimGraph::Clone(*sourceGraph) : nullptr;
					if (!graphCopy)
					{
						m_LastError = "Cannot duplicate Layer: one of its Graph Set bindings is invalid";
						return;
					}
					duplicate.binding.enabled = true;
					duplicate.binding.graphId = makeUniqueId(sourceBinding.graphId + "-copy", [&](const std::string& id)
						{ return m_AssetData->FindGraph(id) != nullptr || reservedGraphIds.count(id) != 0; });
					reservedGraphIds.insert(duplicate.binding.graphId);
					AnimatorGraphAsset graphAsset;
					graphAsset.id = duplicate.binding.graphId;
					graphAsset.name = selectedLayer.name + " Copy Graph";
					graphAsset.role = AnimatorGraphAsset::Role::Pose;
					graphAsset.graph = std::move(graphCopy);
					duplicate.graph = std::move(graphAsset);
					if (graphSet.id == m_EditState->selectedGraphSetId)
						graphToActivate = duplicate.binding.graphId;
				}
				pending.push_back(std::move(duplicate));
			}
			for (PendingDuplicate& duplicate : pending)
				if (duplicate.graph)
					m_AssetData->graphs.push_back(std::move(*duplicate.graph));
			for (std::size_t graphSetIndex = 0; graphSetIndex < m_AssetData->graphSets.size(); ++graphSetIndex)
				m_AssetData->graphSets[graphSetIndex].bindings.insert(
					m_AssetData->graphSets[graphSetIndex].bindings.begin() + selectedIndex + 1,
					std::move(pending[graphSetIndex].binding));
			VansAnimationLayerDefinition copy = selectedLayer;
			copy.id = layerId;
			copy.name += " Copy";
			m_AssetData->layers.insert(m_AssetData->layers.begin() + selectedIndex + 1, copy);
			m_EditState->selectedLayerId = layerId;
			if (!graphToActivate.empty()) activateGraph(graphToActivate);
			m_EditState->isDirty = true;
			selectedIndex += 1;
			duplicatedLayer = true;
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
			std::vector<std::string> removedGraphIds;
			for (auto& graphSet : m_AssetData->graphSets)
			{
				if (selectedIndex < graphSet.bindings.size())
				{
					if (graphSet.bindings[selectedIndex].enabled)
						removedGraphIds.push_back(graphSet.bindings[selectedIndex].graphId);
					graphSet.bindings.erase(graphSet.bindings.begin() + selectedIndex);
				}
			}
			m_AssetData->layers.erase(m_AssetData->layers.begin() + selectedIndex);
			m_AssetData->slots.erase(std::remove_if(m_AssetData->slots.begin(), m_AssetData->slots.end(),
				[&](const VansAnimationSlotDefinition& slot) { return slot.layerId == removedLayerId; }), m_AssetData->slots.end());
			for (VansAnimationLayerDefinition& layer : m_AssetData->layers)
				if (layer.syncLeaderLayerId == removedLayerId)
				{
					layer.sync = VansLayerSyncMode::Independent;
					layer.syncLeaderLayerId.clear();
				}
			m_AssetData->graphs.erase(std::remove_if(m_AssetData->graphs.begin(), m_AssetData->graphs.end(),
				[&](const AnimatorGraphAsset& graph)
				{
					if (std::find(removedGraphIds.begin(), removedGraphIds.end(), graph.id) == removedGraphIds.end())
						return false;
					return std::none_of(m_AssetData->graphSets.begin(), m_AssetData->graphSets.end(),
						[&](const VansAnimationGraphSetDefinition& graphSet)
						{
							return std::any_of(graphSet.bindings.begin(), graphSet.bindings.end(),
								[&](const auto& binding) { return binding.enabled && binding.graphId == graph.id; });
						});
				}), m_AssetData->graphs.end());
			m_EditState->selectedLayerId = m_AssetData->layers.front().id;
			if (auto graphSet = selectedGraphSet(); graphSet != m_AssetData->graphSets.end())
				if (auto* binding = findBinding(*graphSet, m_EditState->selectedLayerId);
					binding && binding->enabled) activateGraph(binding->graphId);
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
				for (auto& graphSet : m_AssetData->graphSets)
					std::swap(graphSet.bindings[selectedIndex], graphSet.bindings[selectedIndex - 1]);
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
				for (auto& graphSet : m_AssetData->graphSets)
					std::swap(graphSet.bindings[selectedIndex], graphSet.bindings[selectedIndex + 1]);
				++selectedIndex;
				m_EditState->isDirty = true;
				reorderedLayer = true;
			}
			if (!canMoveDown) ImGui::EndDisabled();
			if (reorderedLayer)
				return;

			ImGui::SeparatorText("Layer Inspector");
			if (EditStringProperty("Layer Name", layer.name)) m_EditState->isDirty = true;
			if (auto graphSet = selectedGraphSet(); graphSet != m_AssetData->graphSets.end())
			{
				VansAnimationGraphBindingDefinition* binding = findBinding(*graphSet, layer.id);
				if (binding)
				{
					ImGui::SeparatorText("Graph Set Binding");
					if (layer.kind == VansAnimationLayerKind::Base) ImGui::BeginDisabled();
					bool enabled = binding->enabled;
					if (ImGui::Checkbox("Enabled In Graph Set", &enabled))
					{
						binding->enabled = enabled;
						if (!enabled)
							binding->graphId.clear();
						else if (binding->graphId.empty())
						{
							auto poseGraph = std::find_if(m_AssetData->graphs.begin(), m_AssetData->graphs.end(),
								[](const AnimatorGraphAsset& graph)
								{ return graph.role == AnimatorGraphAsset::Role::Pose; });
							if (poseGraph != m_AssetData->graphs.end()) binding->graphId = poseGraph->id;
						}
						m_EditState->isDirty = true;
					}
					if (layer.kind == VansAnimationLayerKind::Base) ImGui::EndDisabled();
					if (binding->enabled)
					{
						const AnimatorGraphAsset* selectedGraph = nullptr;
						for (const auto& graph : m_AssetData->graphs)
							if (graph.id == binding->graphId) { selectedGraph = &graph; break; }
						const char* preview = selectedGraph ? selectedGraph->name.c_str() : "(select Pose Graph)";
						if (ImGui::BeginCombo("Pose Graph", preview))
						{
							for (const auto& graph : m_AssetData->graphs)
							{
								if (graph.role != AnimatorGraphAsset::Role::Pose) continue;
								if (ImGui::Selectable(graph.name.c_str(), graph.id == binding->graphId))
								{
									binding->graphId = graph.id;
									activateGraph(graph.id);
									m_EditState->isDirty = true;
								}
							}
							ImGui::EndCombo();
						}
						if (ImGui::SmallButton("+ New Pose Graph"))
						{
							AnimatorGraphAsset graph;
							graph.id = makeUniqueId("graph-pose", [&](const std::string& id)
								{ return m_AssetData->FindGraph(id) != nullptr; });
							graph.name = "Pose Graph";
							graph.role = AnimatorGraphAsset::Role::Pose;
							graph.graph = std::make_unique<VansAnimGraph>();
							const int entry = graph.graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Entry));
							const int output = graph.graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
							graph.graph->AddLink(entry, 0, output, 0);
							binding->graphId = graph.id;
							m_AssetData->graphs.push_back(std::move(graph));
							activateGraph(binding->graphId);
							m_EditState->isDirty = true;
						}
					}
				}
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
			ImGui::SetTooltip("ID: %s\nLayer: %s", slot.id.c_str(), slot.layerId.c_str());
		ImGui::PopID();
	}

	if (ImGui::SmallButton("Create From Selected Slot Node"))
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
					return slot.id == slotNode->m_SlotId;
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
	ImGui::TextDisabled("Layer: %s", selected->layerId.c_str());
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
		if (ImGui::MenuItem("Goal")) addNode(AnimGraphNodeType::Goal);
		if (ImGui::MenuItem("Aim Constraint")) addNode(AnimGraphNodeType::AimConstraint);
		if (targetPostProcess && ImGui::MenuItem("Grounding")) addNode(AnimGraphNodeType::Grounding);
		if (targetPostProcess && ImGui::MenuItem("Limb IK")) addNode(AnimGraphNodeType::LimbIK);
		if (targetPostProcess && ImGui::MenuItem("Chain IK")) addNode(AnimGraphNodeType::ChainIK);
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
	auto editGoal = [&](const char* label, AnimationGoalDefinition& goal)
	{
		bool changed = false;
		ImGui::SeparatorText(label);
		changed |= EditStringProperty("Goal ID", goal.goalId);
		int source = static_cast<int>(goal.source);
		if (ImGui::Combo("Source", &source, "Binding\0Parameters\0Fixed Model Space\0"))
		{
			goal.source = static_cast<AnimationGoalSource>(source);
			changed = true;
		}
		if (goal.source == AnimationGoalSource::Binding)
			changed |= EditStringProperty("Binding", goal.binding);
		else if (goal.source == AnimationGoalSource::Parameters)
		{
			changed |= editParameterBinding("Position Parameter", goal.positionParameter,
				AnimatorParamType::Vector3, false);
			changed |= editParameterBinding("Rotation Parameter", goal.rotationParameter,
				AnimatorParamType::Quaternion);
			changed |= editParameterBinding("Weight Parameter", goal.weightParameter,
				AnimatorParamType::Float);
		}
		else
		{
			changed |= ImGui::DragFloat3("Position", &goal.fixedPositionModel.x, 0.01f);
			changed |= ImGui::DragFloat4("Rotation (xyzw)", &goal.fixedRotationModel.x, 0.01f);
			changed |= ImGui::SliderFloat("Position Weight", &goal.fixedPositionWeight, 0.0f, 1.0f);
			changed |= ImGui::SliderFloat("Rotation Weight", &goal.fixedRotationWeight, 0.0f, 1.0f);
		}
		return changed;
	};
	auto editStringList = [&](const char* label, std::vector<std::string>& values)
	{
		bool changed = false;
		ImGui::SeparatorText(label);
		for (int index = 0; index < static_cast<int>(values.size()); ++index)
		{
			ImGui::PushID(index + 24000);
			changed |= EditStringProperty("ID", values[static_cast<std::size_t>(index)]);
			if (ImGui::SameLine(); ImGui::SmallButton("Remove"))
			{
				values.erase(values.begin() + index);
				changed = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (ImGui::SmallButton("Add ID"))
		{
			values.emplace_back();
			changed = true;
		}
		return changed;
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
				if (slot.id == previousSlotId && slot.layerId == m_EditState->selectedLayerId)
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
	case AnimGraphNodeType::Goal:
	{
		auto* n = static_cast<AnimGraphGoalNode*>(node);
		if (editGoal("Goal Definition", n->m_Goal)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::AimConstraint:
	{
		auto* n = static_cast<AnimGraphAimConstraintNode*>(node);
		if (EditStringProperty("Rig Chain ID", n->m_ChainId)) m_EditState->isDirty = true;
		if (editGoal("Aim Target", n->m_Target)) m_EditState->isDirty = true;
		if (ImGui::DragFloat2("Yaw Min / Max", &n->m_AimSettings.minYawDegrees, 0.5f, -180.0f, 180.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat2("Pitch Min / Max", &n->m_AimSettings.minPitchDegrees, 0.5f, -180.0f, 180.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Max Angular Speed", &n->m_AimSettings.maxAngularSpeedDegrees, 1.0f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Weight", &n->m_AimSettings.weight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Target Half-Life", &n->m_TargetHalfLife, 0.005f, 0.0f)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::Grounding:
	{
		auto* n = static_cast<AnimGraphGroundingNode*>(node);
		auto& settings = n->m_GroundingSettings;
		if (editStringList("Rig Contact IDs", settings.contacts)) m_EditState->isDirty = true;
		if (EditStringProperty("Physics Query Profile", settings.query.profile)) m_EditState->isDirty = true;
		if (EditStringProperty("Plant Signal", settings.plantSignal)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Probe Start", &settings.query.startDistanceAgainstApproach, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Probe End", &settings.query.endDistanceAlongApproach, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Max Step Up", &settings.query.maxStepUp, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Max Step Down", &settings.query.maxStepDown, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Max Slope", &settings.query.maxSlopeDegrees, 0.0f, 89.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Max Plane Residual", &settings.query.maxPlaneResidual, 0.001f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Max Normal Deviation", &settings.query.maxNormalDeviationDegrees, 0.0f, 89.0f)) m_EditState->isDirty = true;
		ImGui::SeparatorText("Contact Alignment");
		if (ImGui::DragFloat("Full Contact Height", &settings.alignment.fullContactHeight, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Contact Fade Height", &settings.alignment.contactFadeHeight, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Normal Half-Life", &settings.alignment.normalHalfLife, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Rotation Weight", &settings.alignment.rotationWeight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		ImGui::SeparatorText("Plant Lock");
		if (ImGui::Checkbox("Lock Enabled", &settings.plant.lockEnabled)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Enter Phase", &settings.plant.enterPhase, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Exit Phase", &settings.plant.exitPhase, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Unplant Distance", &settings.plant.unplantDistance, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Replant Distance", &settings.plant.replantDistance, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Unplant Angle", &settings.plant.unplantAngleDegrees, 0.0f, 90.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Replant Angle", &settings.plant.replantAngleDegrees, 0.0f, 90.0f)) m_EditState->isDirty = true;
		int pivot = static_cast<int>(settings.plant.pivot);
		if (ImGui::Combo("Plant Pivot", &pivot, "Heel\0Ball\0Ankle\0"))
		{
			settings.plant.pivot = static_cast<AnimationPlantPivot>(pivot);
			m_EditState->isDirty = true;
		}
		if (ImGui::DragFloat("Lock Weight Half-Life", &settings.plant.weightHalfLife, 0.005f, 0.0f)) m_EditState->isDirty = true;
		ImGui::SeparatorText("Pelvis");
		if (ImGui::DragFloat("Max Up Offset", &settings.pelvis.maxUpOffset, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Max Down Offset", &settings.pelvis.maxDownOffset, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Max Horizontal Offset", &settings.pelvis.maxHorizontalOffset, 0.01f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Pelvis Half-Life", &settings.pelvis.halfLife, 0.005f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Weight", &settings.weight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::LimbIK:
	{
		auto* n = static_cast<AnimGraphLimbIKNode*>(node);
		if (editStringList("Rig Chain IDs", n->m_ChainIds)) m_EditState->isDirty = true;
		int mode = static_cast<int>(n->m_LimbSettings.tipRotationMode);
		if (ImGui::Combo("Tip Rotation", &mode, "Preserve Input\0Match Goal\0Follow Chain\0"))
		{
			n->m_LimbSettings.tipRotationMode = static_cast<AnimationLimbTipRotationMode>(mode);
			m_EditState->isDirty = true;
		}
		if (ImGui::DragFloat("Position Tolerance", &n->m_LimbSettings.positionTolerance, 0.0001f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Weight", &n->m_LimbSettings.weight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Commit Clamped Pose", &n->m_LimbSettings.commitClampedPose)) m_EditState->isDirty = true;
		break;
	}
	case AnimGraphNodeType::ChainIK:
	{
		auto* n = static_cast<AnimGraphChainIKNode*>(node);
		if (editStringList("Rig Chain IDs", n->m_ChainIds)) m_EditState->isDirty = true;
		if (ImGui::InputInt("Max Iterations", &n->m_ChainSettings.maxIterations)) m_EditState->isDirty = true;
		if (ImGui::DragFloat("Position Tolerance", &n->m_ChainSettings.positionTolerance, 0.0001f, 0.0f)) m_EditState->isDirty = true;
		if (ImGui::SliderFloat("Weight", &n->m_ChainSettings.weight, 0.0f, 1.0f)) m_EditState->isDirty = true;
		if (ImGui::Checkbox("Commit Clamped Pose", &n->m_ChainSettings.commitClampedPose)) m_EditState->isDirty = true;
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
	const std::string previousGraphSetId = m_EditState->selectedGraphSetId;
	m_AssetData = std::move(reloaded);
	m_EditState->name = m_AssetData->name;
	m_EditState->parameters = m_AssetData->parameters;
	m_EditState->clipRefs = m_AssetData->clipRefs;
	m_EditState->selectedLayerId = previousLayerId.empty()
		? m_AssetData->layers.front().id : previousLayerId;
	m_EditState->selectedGraphSetId = previousGraphSetId.empty()
		? m_AssetData->defaultGraphSetId : previousGraphSetId;
	if (std::none_of(m_AssetData->graphSets.begin(), m_AssetData->graphSets.end(),
		[&](const auto& graphSet) { return graphSet.id == m_EditState->selectedGraphSetId; }))
		m_EditState->selectedGraphSetId = m_AssetData->defaultGraphSetId;
	if (std::none_of(m_AssetData->layers.begin(), m_AssetData->layers.end(),
		[&](const VansAnimationLayerDefinition& layer) { return layer.id == previousLayerId; }))
		m_EditState->selectedLayerId = m_AssetData->layers.front().id;
	if (std::none_of(m_AssetData->slots.begin(), m_AssetData->slots.end(),
		[&](const VansAnimationSlotDefinition& slot) { return slot.id == m_EditState->selectedSlotId; }))
		m_EditState->selectedSlotId.clear();
	auto resolveSelectedBindingGraph = [&]() -> std::string
	{
		for (const auto& graphSet : m_AssetData->graphSets)
			if (graphSet.id == m_EditState->selectedGraphSetId)
				for (const auto& binding : graphSet.bindings)
					if (binding.layerId == m_EditState->selectedLayerId && binding.enabled)
						return binding.graphId;
		return {};
	};
	m_ActiveGraphId = previousGraphId.empty() ? resolveSelectedBindingGraph() : previousGraphId;
	m_TargetGraph = m_AssetData->FindGraph(m_ActiveGraphId);
	if (!m_TargetGraph)
	{
		m_ActiveGraphId = resolveSelectedBindingGraph();
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
