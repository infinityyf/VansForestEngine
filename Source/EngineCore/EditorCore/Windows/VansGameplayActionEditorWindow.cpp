#include "VansGameplayActionEditorWindow.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <unordered_set>

namespace VansGraphics
{
namespace
{
void HelpMarker(const char* text)
{
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}

ImVec4 DiagnosticColor(Vans::EditorAPI::GAFEditorDiagnosticSeverity severity)
{
	switch (severity)
	{
	case Vans::EditorAPI::GAFEditorDiagnosticSeverity::Warning:
		return ImVec4(0.95f, 0.72f, 0.24f, 1.0f);
	case Vans::EditorAPI::GAFEditorDiagnosticSeverity::Error:
	case Vans::EditorAPI::GAFEditorDiagnosticSeverity::Fatal:
		return ImVec4(0.95f, 0.30f, 0.28f, 1.0f);
	default:
		return ImVec4(0.55f, 0.75f, 0.92f, 1.0f);
	}
}

const char* ChangeLabel(Vans::EditorAPI::GAFSemanticChangeKind kind)
{
	switch (kind)
	{
	case Vans::EditorAPI::GAFSemanticChangeKind::Added: return "Added";
	case Vans::EditorAPI::GAFSemanticChangeKind::Removed: return "Removed";
	default: return "Modified";
	}
}
}

void VansGameplayActionEditorWindow::Open(const std::string& sourcePath)
{
	m_Path = sourcePath;
	m_IsOpen = true;
	m_NeedsRefresh = true;
	m_CloseRequested = false;
	m_LastError.clear();
	m_BaselineCanonicalJson.clear();
	m_Diff = {};
	m_ArraySelection.clear();
	m_GraphCatalog.clear();
	m_SelectedGraphNode.clear();
	m_GraphDragPositions.clear();
}

void VansGameplayActionEditorWindow::Close()
{
	m_IsOpen = false;
	m_NeedsRefresh = false;
	m_CloseRequested = false;
	m_StructuredEditorOpen = false;
	m_Path.clear();
	m_Document = {};
	m_Diff = {};
	m_GraphCatalog.clear();
	m_TagCatalog.clear();
	m_SelectedGraphNode.clear();
	m_GraphDragPositions.clear();
}

void VansGameplayActionEditorWindow::Refresh(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	m_Document = editorAPI.OpenGAFAsset(m_Path);
	m_NeedsRefresh = false;
	if (!m_Document.success)
	{
		m_LastError = m_Document.message;
		return;
	}
	if (m_Document.graph.available)
		m_GraphCatalog = editorAPI.GetGAFGraphNodeCatalog();
	m_TagCatalog = editorAPI.GetGAFTagCatalog();
	if (m_BaselineCanonicalJson.empty()) m_BaselineCanonicalJson = m_Document.canonicalJson;
	m_Diff = editorAPI.DiffGAFAsset(m_Path, m_BaselineCanonicalJson);
}

void VansGameplayActionEditorWindow::ApplyOperation(
	const Vans::EditorAPI::GAFEditorOperationResult& result)
{
	if (result.document.success) m_Document = result.document;
	m_LastError = result.success ? std::string() : result.message;
}

void VansGameplayActionEditorWindow::SetField(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI,
	const std::string& path,
	Vans::EditorAPI::GAFEditorValue value)
{
	Vans::EditorAPI::GAFEditorFieldEditRequest request;
	request.sourcePath = m_Path;
	request.fieldPath = path;
	request.value = std::move(value);
	ApplyOperation(editorAPI.SetGAFAssetField(request));
	m_Diff = editorAPI.DiffGAFAsset(m_Path, m_BaselineCanonicalJson);
}

void VansGameplayActionEditorWindow::DrawMenuBar(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!ImGui::BeginMenuBar()) return;
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", nullptr, false, m_Document.dirty))
		{
			const auto result = editorAPI.SaveGAFAsset(m_Path);
			ApplyOperation(result);
			if (result.success) m_BaselineCanonicalJson = m_Document.canonicalJson;
		}
		if (ImGui::MenuItem("Revert", nullptr, false, m_Document.dirty))
		{
			ApplyOperation(editorAPI.RevertGAFAsset(m_Path));
			m_Diff = editorAPI.DiffGAFAsset(m_Path, m_BaselineCanonicalJson);
		}
		if (ImGui::MenuItem("Close")) m_CloseRequested = true;
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem("Undo", nullptr, false, m_Document.canUndo))
			ApplyOperation(editorAPI.UndoGAFAsset(m_Path));
		if (ImGui::MenuItem("Redo", nullptr, false, m_Document.canRedo))
			ApplyOperation(editorAPI.RedoGAFAsset(m_Path));
		ImGui::EndMenu();
	}
	ImGui::EndMenuBar();
}

void VansGameplayActionEditorWindow::DrawToolbar(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	ImGui::BeginDisabled(!m_Document.dirty);
	if (ImGui::Button("Save"))
	{
		const auto result = editorAPI.SaveGAFAsset(m_Path);
		ApplyOperation(result);
		if (result.success) m_BaselineCanonicalJson = m_Document.canonicalJson;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!m_Document.canUndo);
	if (ImGui::Button("<-")) ApplyOperation(editorAPI.UndoGAFAsset(m_Path));
	HelpMarker("Undo");
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!m_Document.canRedo);
	if (ImGui::Button("->")) ApplyOperation(editorAPI.RedoGAFAsset(m_Path));
	HelpMarker("Redo");
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Refresh")) Refresh(editorAPI);
	ImGui::SameLine();
	const ImVec4 stateColor = m_Document.cookable
		? ImVec4(0.32f, 0.78f, 0.48f, 1.0f) : ImVec4(0.95f, 0.30f, 0.28f, 1.0f);
	ImGui::TextColored(stateColor, "%s", m_Document.cookable ? "Cook ready" : "Cook blocked");
	ImGui::SameLine();
	ImGui::TextDisabled("Hash %016llx",
		static_cast<unsigned long long>(m_Document.contentHash));
}

void VansGameplayActionEditorWindow::DrawDiagnostics() const
{
	if (m_Document.diagnostics.empty())
	{
		ImGui::TextColored(ImVec4(0.32f, 0.78f, 0.48f, 1.0f), "No validation issues");
		return;
	}
	for (const auto& diagnostic : m_Document.diagnostics)
	{
		ImGui::PushID(&diagnostic);
		ImGui::TextColored(DiagnosticColor(diagnostic.severity), "%s", diagnostic.code.c_str());
		if (!diagnostic.fieldPath.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", diagnostic.fieldPath.c_str());
		}
		ImGui::TextWrapped("%s", diagnostic.message.c_str());
		ImGui::PopID();
	}
}

void VansGameplayActionEditorWindow::DrawOverview()
{
	if (ImGui::BeginTable("##gaf-overview", 2, ImGuiTableFlags_SizingStretchProp))
	{
		const auto row = [](const char* name, const std::string& value)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", name);
			ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", value.c_str());
		};
		row("Asset", m_Document.assetKind);
		row("Path", m_Document.sourcePath);
		row("Schema", std::to_string(m_Document.schemaVersion));
		row("State", m_Document.dirty ? "Modified" : "Saved");
		ImGui::EndTable();
	}
	ImGui::SeparatorText("Dependencies");
	if (m_Document.dependencies.empty()) ImGui::TextDisabled("None");
	for (const std::string& dependency : m_Document.dependencies)
		ImGui::BulletText("%s", dependency.c_str());
	ImGui::SeparatorText("Diagnostics");
	DrawDiagnostics();
}

void VansGameplayActionEditorWindow::OpenStructuredEditor(
	const Vans::EditorAPI::GAFEditorFieldSnapshot& field)
{
	m_StructuredPath = field.path;
	m_StructuredKind = field.kind == Vans::EditorAPI::GAFEditorPropertyKind::Payload
		? Vans::EditorAPI::GAFEditorValueKind::Json : field.value.kind;
	const std::size_t capacity = (std::max<std::size_t>)(65536, field.value.canonicalJson.size() + 1024);
	m_StructuredBuffer.assign(capacity, '\0');
	std::memcpy(m_StructuredBuffer.data(), field.value.canonicalJson.data(),
		(std::min)(field.value.canonicalJson.size(), capacity - 1));
	m_StructuredEditorOpen = true;
}

void VansGameplayActionEditorWindow::DrawProperty(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI,
	Vans::EditorAPI::GAFEditorFieldSnapshot field)
{
	if (!field.visible) return;
	ImGui::PushID(field.path.c_str());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(field.displayName.c_str());
	if (field.required)
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.95f, 0.42f, 0.36f, 1.0f), "*");
	}
	if (!field.diagnostics.empty())
	{
		ImGui::SameLine();
		ImGui::TextColored(DiagnosticColor(field.diagnostics.front().severity), "!");
		HelpMarker(field.diagnostics.front().message.c_str());
	}
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-34.0f);
	ImGui::BeginDisabled(!field.enabled);
	switch (field.kind)
	{
	case Vans::EditorAPI::GAFEditorPropertyKind::Bool:
	{
		bool value = field.value.boolValue;
		if (ImGui::Checkbox("##value", &value))
		{
			auto edited = field.value;
			edited.kind = Vans::EditorAPI::GAFEditorValueKind::Bool;
			edited.boolValue = value;
			SetField(editorAPI, field.path, std::move(edited));
		}
		break;
	}
	case Vans::EditorAPI::GAFEditorPropertyKind::Int:
	{
		std::int64_t value = field.value.intValue;
		if (ImGui::InputScalar("##value", ImGuiDataType_S64, &value, nullptr, nullptr, nullptr,
			ImGuiInputTextFlags_EnterReturnsTrue))
		{
			if (field.hasMinimum) value = (std::max)(value, static_cast<std::int64_t>(field.minimum));
			if (field.hasMaximum) value = (std::min)(value, static_cast<std::int64_t>(field.maximum));
			auto edited = field.value;
			edited.kind = Vans::EditorAPI::GAFEditorValueKind::Int;
			edited.intValue = value;
			SetField(editorAPI, field.path, std::move(edited));
		}
		break;
	}
	case Vans::EditorAPI::GAFEditorPropertyKind::Float:
	{
		double value = field.value.kind == Vans::EditorAPI::GAFEditorValueKind::Int
			? static_cast<double>(field.value.intValue) : field.value.floatValue;
		if (ImGui::InputDouble("##value", &value, 0.0, 0.0, "%.6g",
			ImGuiInputTextFlags_EnterReturnsTrue))
		{
			if (field.hasMinimum) value = (std::max)(value, field.minimum);
			if (field.hasMaximum) value = (std::min)(value, field.maximum);
			auto edited = field.value;
			edited.kind = Vans::EditorAPI::GAFEditorValueKind::Float;
			edited.floatValue = value;
			SetField(editorAPI, field.path, std::move(edited));
		}
		break;
	}
	case Vans::EditorAPI::GAFEditorPropertyKind::Enum:
	{
		if (ImGui::BeginCombo("##value", field.value.stringValue.c_str()))
		{
			for (const std::string& option : field.enumValues)
				if (ImGui::Selectable(option.c_str(), option == field.value.stringValue))
				{
					auto edited = field.value;
					edited.kind = Vans::EditorAPI::GAFEditorValueKind::String;
					edited.stringValue = option;
					SetField(editorAPI, field.path, std::move(edited));
				}
			ImGui::EndCombo();
		}
		break;
	}
	case Vans::EditorAPI::GAFEditorPropertyKind::Vec2:
	case Vans::EditorAPI::GAFEditorPropertyKind::Vec3:
	case Vans::EditorAPI::GAFEditorPropertyKind::Vec4:
	case Vans::EditorAPI::GAFEditorPropertyKind::Quaternion:
	{
		const int count = field.kind == Vans::EditorAPI::GAFEditorPropertyKind::Vec2 ? 2 :
			field.kind == Vans::EditorAPI::GAFEditorPropertyKind::Vec3 ? 3 : 4;
		double values[4]{ 0.0, 0.0, 0.0,
			field.kind == Vans::EditorAPI::GAFEditorPropertyKind::Quaternion ? 1.0 : 0.0 };
		for (int index = 0; index < count && index < static_cast<int>(field.children.size()); ++index)
			values[index] = field.children[index].value.kind ==
				Vans::EditorAPI::GAFEditorValueKind::Int
				? static_cast<double>(field.children[index].value.intValue)
				: field.children[index].value.floatValue;
		if (ImGui::InputScalarN("##value", ImGuiDataType_Double, values, count,
			nullptr, nullptr, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue))
		{
			char json[512]{};
			if (count == 2)
				std::snprintf(json, sizeof(json), "{\"x\":%.17g,\"y\":%.17g}",
					values[0], values[1]);
			else if (count == 3)
				std::snprintf(json, sizeof(json), "{\"x\":%.17g,\"y\":%.17g,\"z\":%.17g}",
					values[0], values[1], values[2]);
			else
				std::snprintf(json, sizeof(json),
					"{\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,\"w\":%.17g}",
					values[0], values[1], values[2], values[3]);
			Vans::EditorAPI::GAFEditorValue edited;
			edited.kind = Vans::EditorAPI::GAFEditorValueKind::Object;
			edited.canonicalJson = json;
			SetField(editorAPI, field.path, std::move(edited));
		}
		break;
	}
	case Vans::EditorAPI::GAFEditorPropertyKind::Color:
	{
		float values[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
		for (int index = 0; index < 4 && index < static_cast<int>(field.children.size()); ++index)
			values[index] = static_cast<float>(field.children[index].value.kind ==
				Vans::EditorAPI::GAFEditorValueKind::Int
				? static_cast<double>(field.children[index].value.intValue)
				: field.children[index].value.floatValue);
		if (ImGui::ColorEdit4("##value", values, ImGuiColorEditFlags_Float))
		{
			char json[512]{};
			std::snprintf(json, sizeof(json),
				"{\"r\":%.9g,\"g\":%.9g,\"b\":%.9g,\"a\":%.9g}",
				values[0], values[1], values[2], values[3]);
			Vans::EditorAPI::GAFEditorValue edited;
			edited.kind = Vans::EditorAPI::GAFEditorValueKind::Object;
			edited.canonicalJson = json;
			SetField(editorAPI, field.path, std::move(edited));
		}
		break;
	}
	case Vans::EditorAPI::GAFEditorPropertyKind::String:
	{
		char buffer[1024]{};
		std::snprintf(buffer, sizeof(buffer), "%s", field.value.stringValue.c_str());
		if (ImGui::InputText("##value", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			auto edited = field.value;
			edited.kind = Vans::EditorAPI::GAFEditorValueKind::String;
			edited.stringValue = buffer;
			SetField(editorAPI, field.path, std::move(edited));
		}
		break;
	}
	case Vans::EditorAPI::GAFEditorPropertyKind::Tag:
	{
		char buffer[1024]{};
		std::snprintf(buffer, sizeof(buffer), "%s", field.value.stringValue.c_str());
		if (ImGui::InputText("##value", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			auto edited = field.value;
			edited.kind = Vans::EditorAPI::GAFEditorValueKind::String;
			edited.stringValue = buffer;
			SetField(editorAPI, field.path, std::move(edited));
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("...")) ImGui::OpenPopup("Tag Picker");
		HelpMarker("Choose a Gameplay Tag from the project dictionary");
		if (ImGui::BeginPopup("Tag Picker"))
		{
			for (const std::string& tag : m_TagCatalog)
				if ((buffer[0] == '\0' || tag.find(buffer) != std::string::npos) &&
					ImGui::Selectable(tag.c_str(), tag == field.value.stringValue))
				{
					auto edited = field.value;
					edited.kind = Vans::EditorAPI::GAFEditorValueKind::String;
					edited.stringValue = tag;
					SetField(editorAPI, field.path, std::move(edited));
					ImGui::CloseCurrentPopup();
				}
			if (m_TagCatalog.empty()) ImGui::TextDisabled("No Gameplay Tags in the project");
			ImGui::EndPopup();
		}
		break;
	}
	case Vans::EditorAPI::GAFEditorPropertyKind::AssetReference:
	{
		char buffer[1024]{};
		std::snprintf(buffer, sizeof(buffer), "%s", field.value.stringValue.c_str());
		if (ImGui::InputText("##value", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			auto edited = field.value;
			edited.kind = Vans::EditorAPI::GAFEditorValueKind::String;
			edited.stringValue = buffer;
			SetField(editorAPI, field.path, std::move(edited));
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("...")) ImGui::OpenPopup("Asset Picker");
		HelpMarker("Choose a compatible project asset");
		if (ImGui::BeginPopup("Asset Picker"))
		{
			bool any = false;
			for (const auto type : field.allowedAssetTypes)
				for (const auto& asset : editorAPI.QueryAssets({ type, false }))
				{
					any = true;
					const std::string label = asset.name + "##" + asset.guid;
					if (ImGui::Selectable(label.c_str()))
					{
						auto edited = field.value;
						edited.kind = Vans::EditorAPI::GAFEditorValueKind::String;
						edited.stringValue = asset.guid;
						SetField(editorAPI, field.path, std::move(edited));
						ImGui::CloseCurrentPopup();
					}
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset.relativePath.c_str());
				}
			if (!any) ImGui::TextDisabled("No compatible assets");
			ImGui::EndPopup();
		}
		break;
	}
	case Vans::EditorAPI::GAFEditorPropertyKind::Array:
	{
		ImGui::Text("%zu items", field.arraySize);
		ImGui::SameLine();
		if (ImGui::SmallButton("+"))
		{
			Vans::EditorAPI::GAFEditorArrayEditRequest request;
			request.sourcePath = m_Path;
			request.fieldPath = field.path;
			request.operation = Vans::EditorAPI::GAFEditorArrayOperation::Append;
			request.value = field.arrayElementDefault;
			ApplyOperation(editorAPI.EditGAFAssetArray(request));
		}
		HelpMarker("Add item");
		ImGui::SameLine();
		if (ImGui::SmallButton("JSON")) OpenStructuredEditor(field);
		HelpMarker("Advanced structured JSON editor");
		std::size_t& selected = m_ArraySelection[field.path];
		if (field.arraySize > 0)
		{
			selected = (std::min)(selected, field.arraySize - 1);
			ImGui::SameLine();
			std::uint64_t selectedValue = selected;
			ImGui::SetNextItemWidth(70.0f);
			if (ImGui::InputScalar("##index", ImGuiDataType_U64, &selectedValue))
				selected = static_cast<std::size_t>((std::min<std::uint64_t>)(selectedValue, field.arraySize - 1));
			const auto command = [&](Vans::EditorAPI::GAFEditorArrayOperation operation,
				std::size_t destination = 0)
			{
				Vans::EditorAPI::GAFEditorArrayEditRequest request;
				request.sourcePath = m_Path;
				request.fieldPath = field.path;
				request.operation = operation;
				request.index = selected;
				request.destinationIndex = destination;
				ApplyOperation(editorAPI.EditGAFAssetArray(request));
			};
			ImGui::SameLine(); if (ImGui::SmallButton("D")) command(Vans::EditorAPI::GAFEditorArrayOperation::Duplicate);
			HelpMarker("Duplicate selected item");
			ImGui::SameLine(); if (ImGui::SmallButton("^"))
				command(Vans::EditorAPI::GAFEditorArrayOperation::Move, selected > 0 ? selected - 1 : selected);
			HelpMarker("Move selected item up");
			ImGui::SameLine(); if (ImGui::SmallButton("v"))
				command(Vans::EditorAPI::GAFEditorArrayOperation::Move,
					(std::min)(selected + 1, field.arraySize - 1));
			HelpMarker("Move selected item down");
			ImGui::SameLine(); if (ImGui::SmallButton("x")) command(Vans::EditorAPI::GAFEditorArrayOperation::Remove);
			HelpMarker("Remove selected item");
		}
		if (!field.children.empty() && ImGui::TreeNodeEx("Items##structured",
			ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
		{
			DrawStructuredChildren(editorAPI, field.path + "##items", field.children);
			ImGui::TreePop();
		}
		break;
	}
	default:
		if (!field.children.empty())
		{
			if (ImGui::TreeNodeEx("Configure##structured",
				ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
			{
				DrawStructuredChildren(editorAPI, field.path + "##children", field.children);
				ImGui::TreePop();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("JSON")) OpenStructuredEditor(field);
			HelpMarker("Advanced structured JSON editor");
		}
		else if (ImGui::Button("Edit JSON", ImVec2(-34.0f, 0.0f))) OpenStructuredEditor(field);
		break;
	}
	ImGui::EndDisabled();
	if (!field.readOnly && !field.deprecated && !field.isArrayElement)
	{
		ImGui::SameLine();
		if (ImGui::SmallButton("R"))
			ApplyOperation(editorAPI.ResetGAFAssetField(m_Path, field.path));
		HelpMarker("Reset to default");
	}
	ImGui::PopID();
}

void VansGameplayActionEditorWindow::DrawStructuredChildren(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI,
	const std::string& tableId,
	const std::vector<Vans::EditorAPI::GAFEditorFieldSnapshot>& children)
{
	if (!ImGui::BeginTable(tableId.c_str(), 2,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
		ImGuiTableFlags_BordersInnerH)) return;
	ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 170.0f);
	ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
	for (const auto& child : children) DrawProperty(editorAPI, child);
	ImGui::EndTable();
}

void VansGameplayActionEditorWindow::DrawProperties(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	std::vector<std::string> groups;
	std::unordered_set<std::string> seen;
	for (const auto& field : m_Document.fields)
		if (field.visible && seen.insert(field.group).second) groups.push_back(field.group);
	for (const std::string& group : groups)
	{
		if (!ImGui::CollapsingHeader(group.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;
		ImGui::PushID(group.c_str());
		if (ImGui::BeginTable("##fields", 2,
			ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
		{
			ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 190.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			for (const auto& field : m_Document.fields)
				if (field.group == group) DrawProperty(editorAPI, field);
			ImGui::EndTable();
		}
		ImGui::PopID();
	}
}

void VansGameplayActionEditorWindow::ApplyGraphOperation(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI,
	Vans::EditorAPI::GAFGraphEditRequest request)
{
	request.sourcePath = m_Path;
	const std::size_t previousNodeCount = m_Document.graph.nodes.size();
	const auto operation = request.operation;
	const auto result = editorAPI.EditGAFGraph(request);
	ApplyOperation(result);
	if (result.success && operation == Vans::EditorAPI::GAFGraphEditOperation::AddNode &&
		m_Document.graph.nodes.size() > previousNodeCount)
		m_SelectedGraphNode = m_Document.graph.nodes.back().guid;
	if (result.success && operation == Vans::EditorAPI::GAFGraphEditOperation::RemoveNode)
		m_SelectedGraphNode.clear();
	m_Diff = editorAPI.DiffGAFAsset(m_Path, m_BaselineCanonicalJson);
}

void VansGameplayActionEditorWindow::DrawGraph(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!m_Document.graph.available)
	{
		ImGui::TextDisabled("This asset has no Action Graph document");
		return;
	}
	if (m_GraphCatalog.empty()) m_GraphCatalog = editorAPI.GetGAFGraphNodeCatalog();
	ImGui::SetNextItemWidth(130.0f);
	ImGui::SliderFloat("Zoom", &m_GraphZoom, 0.75f, 1.5f, "%.2fx");
	ImGui::SameLine();
	if (ImGui::Button("Reset View"))
	{
		m_GraphPanX = 30.0f;
		m_GraphPanY = 30.0f;
		m_GraphZoom = 1.0f;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%zu nodes, %zu connections",
		m_Document.graph.nodes.size(), m_Document.graph.edges.size());

	if (!ImGui::BeginTable("##gaf-graph-workbench", 3,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_SizingStretchProp)) return;
	ImGui::TableSetupColumn("Palette", ImGuiTableColumnFlags_WidthFixed, 190.0f);
	ImGui::TableSetupColumn("Canvas", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, 285.0f);
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::InputTextWithHint("##graph-search", "Search nodes", m_GraphSearch,
		sizeof(m_GraphSearch));
	ImGui::BeginChild("##graph-palette", ImVec2(0.0f, 0.0f), false);
	std::string activeCategory;
	for (const auto& nodeType : m_GraphCatalog)
	{
		const bool matches = m_GraphSearch[0] == '\0' ||
			nodeType.displayName.find(m_GraphSearch) != std::string::npos ||
			nodeType.type.find(m_GraphSearch) != std::string::npos;
		if (!matches) continue;
		if (activeCategory != nodeType.category)
		{
			activeCategory = nodeType.category;
			ImGui::SeparatorText(activeCategory.c_str());
		}
		ImGui::PushID(nodeType.type.c_str());
		ImGui::BeginDisabled(!nodeType.allowed);
		if (ImGui::Button(nodeType.displayName.c_str(), ImVec2(-1.0f, 0.0f)))
		{
			Vans::EditorAPI::GAFGraphEditRequest request;
			request.operation = Vans::EditorAPI::GAFGraphEditOperation::AddNode;
			request.nodeType = nodeType.type;
			request.x = static_cast<double>((m_Document.graph.nodes.size() % 3) * 220);
			request.y = static_cast<double>((m_Document.graph.nodes.size() / 3) * 120);
			ApplyGraphOperation(editorAPI, std::move(request));
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s%s", nodeType.type.c_str(),
				nodeType.authorityOnly ? "\nAuthority only" : "");
		ImGui::PopID();
	}
	ImGui::EndChild();

	ImGui::TableSetColumnIndex(1);
	ImGui::BeginChild("##graph-canvas", ImVec2(0.0f, 0.0f), true,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
	const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const bool canvasHovered = ImGui::IsWindowHovered();
	if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
	{
		const ImVec2 delta = ImGui::GetIO().MouseDelta;
		m_GraphPanX += delta.x;
		m_GraphPanY += delta.y;
	}
	const float grid = 32.0f * m_GraphZoom;
	for (float x = std::fmod(m_GraphPanX, grid); x < canvasSize.x; x += grid)
		drawList->AddLine(ImVec2(canvasOrigin.x + x, canvasOrigin.y),
			ImVec2(canvasOrigin.x + x, canvasOrigin.y + canvasSize.y), IM_COL32(48, 52, 60, 120));
	for (float y = std::fmod(m_GraphPanY, grid); y < canvasSize.y; y += grid)
		drawList->AddLine(ImVec2(canvasOrigin.x, canvasOrigin.y + y),
			ImVec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + y), IM_COL32(48, 52, 60, 120));
	const ImVec2 nodeSize(180.0f * m_GraphZoom, 78.0f * m_GraphZoom);
	const auto logicalPosition = [&](const Vans::EditorAPI::GAFGraphNodeSnapshot& node)
	{
		const auto dragged = m_GraphDragPositions.find(node.guid);
		return dragged == m_GraphDragPositions.end()
			? std::array<float, 2>{ static_cast<float>(node.x), static_cast<float>(node.y) }
			: dragged->second;
	};
	const auto screenPosition = [&](const Vans::EditorAPI::GAFGraphNodeSnapshot& node)
	{
		const auto position = logicalPosition(node);
		return ImVec2(canvasOrigin.x + m_GraphPanX + position[0] * m_GraphZoom,
			canvasOrigin.y + m_GraphPanY + position[1] * m_GraphZoom);
	};
	for (const auto& edge : m_Document.graph.edges)
	{
		const auto from = std::find_if(m_Document.graph.nodes.begin(), m_Document.graph.nodes.end(),
			[&](const auto& node) { return node.guid == edge.from; });
		const auto to = std::find_if(m_Document.graph.nodes.begin(), m_Document.graph.nodes.end(),
			[&](const auto& node) { return node.guid == edge.to; });
		if (from == m_Document.graph.nodes.end() || to == m_Document.graph.nodes.end()) continue;
		const ImVec2 fromPosition = screenPosition(*from);
		const ImVec2 toPosition = screenPosition(*to);
		const ImVec2 start(fromPosition.x + nodeSize.x, fromPosition.y + nodeSize.y * 0.62f);
		const ImVec2 end(toPosition.x, toPosition.y + nodeSize.y * 0.62f);
		const float tangent = (std::max)(60.0f, std::abs(end.x - start.x) * 0.45f);
		drawList->AddBezierCubic(start, ImVec2(start.x + tangent, start.y),
			ImVec2(end.x - tangent, end.y), end, IM_COL32(104, 180, 220, 220), 2.0f);
	}
	std::optional<Vans::EditorAPI::GAFGraphEditRequest> deferredGraphEdit;
	for (const auto& node : m_Document.graph.nodes)
	{
		const ImVec2 position = screenPosition(node);
		const bool selected = node.guid == m_SelectedGraphNode;
		const bool entry = node.guid == m_Document.graph.entryNode;
		const ImU32 bodyColor = node.nodeKind == "Command" || node.nodeKind == "SubAction"
			? IM_COL32(69, 74, 86, 255) : node.nodeKind == "Latent"
			? IM_COL32(55, 77, 74, 255) : node.nodeKind == "Transaction"
			? IM_COL32(82, 66, 58, 255) : IM_COL32(61, 66, 77, 255);
		drawList->AddRectFilled(position, ImVec2(position.x + nodeSize.x, position.y + nodeSize.y),
			bodyColor, 5.0f);
		drawList->AddRect(position, ImVec2(position.x + nodeSize.x, position.y + nodeSize.y),
			selected ? IM_COL32(244, 194, 92, 255) : entry ? IM_COL32(92, 210, 142, 255)
			: IM_COL32(112, 121, 138, 255), 5.0f, 0, selected ? 2.5f : 1.5f);
		drawList->AddLine(ImVec2(position.x, position.y + 27.0f),
			ImVec2(position.x + nodeSize.x, position.y + 27.0f), IM_COL32(125, 132, 145, 180));
		const auto type = std::find_if(m_GraphCatalog.begin(), m_GraphCatalog.end(),
			[&](const auto& item) { return item.type == node.type; });
		const std::string title = type == m_GraphCatalog.end() ? node.type : type->displayName;
		drawList->AddText(ImVec2(position.x + 9.0f, position.y + 6.0f), IM_COL32_WHITE,
			title.c_str());
		drawList->AddText(ImVec2(position.x + 9.0f, position.y + 36.0f),
			IM_COL32(174, 183, 198, 255), node.guid.c_str());
		drawList->AddCircleFilled(ImVec2(position.x, position.y + nodeSize.y * 0.62f),
			4.0f, IM_COL32(185, 200, 214, 255));
		drawList->AddCircleFilled(ImVec2(position.x + nodeSize.x, position.y + nodeSize.y * 0.62f),
			4.0f, IM_COL32(104, 180, 220, 255));
		ImGui::SetCursorScreenPos(position);
		ImGui::PushID(node.guid.c_str());
		ImGui::InvisibleButton("##node", nodeSize, ImGuiButtonFlags_MouseButtonLeft |
			ImGuiButtonFlags_MouseButtonRight);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) m_SelectedGraphNode = node.guid;
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			auto draggedPosition = m_GraphDragPositions.find(node.guid);
			if (draggedPosition == m_GraphDragPositions.end())
				draggedPosition = m_GraphDragPositions.emplace(node.guid, logicalPosition(node)).first;
			auto& dragged = draggedPosition->second;
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			dragged[0] += delta.x / m_GraphZoom;
			dragged[1] += delta.y / m_GraphZoom;
		}
		if (ImGui::IsItemDeactivated() && m_GraphDragPositions.find(node.guid) != m_GraphDragPositions.end())
		{
			const auto moved = m_GraphDragPositions[node.guid];
			m_GraphDragPositions.erase(node.guid);
			Vans::EditorAPI::GAFGraphEditRequest request;
			request.operation = Vans::EditorAPI::GAFGraphEditOperation::MoveNode;
			request.nodeGuid = node.guid;
			request.x = moved[0];
			request.y = moved[1];
			deferredGraphEdit = std::move(request);
		}
		if (ImGui::BeginPopupContextItem("##node-menu"))
		{
			if (ImGui::MenuItem("Set as Entry", nullptr, entry))
			{
				Vans::EditorAPI::GAFGraphEditRequest request;
				request.operation = Vans::EditorAPI::GAFGraphEditOperation::SetEntryNode;
				request.nodeGuid = node.guid;
				deferredGraphEdit = std::move(request);
			}
			if (ImGui::MenuItem("Delete"))
			{
				Vans::EditorAPI::GAFGraphEditRequest request;
				request.operation = Vans::EditorAPI::GAFGraphEditOperation::RemoveNode;
				request.nodeGuid = node.guid;
				deferredGraphEdit = std::move(request);
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::Dummy(canvasSize);
	ImGui::EndChild();
	if (deferredGraphEdit) ApplyGraphOperation(editorAPI, std::move(*deferredGraphEdit));

	ImGui::TableSetColumnIndex(2);
	const auto selected = std::find_if(m_Document.graph.nodes.begin(), m_Document.graph.nodes.end(),
		[&](const auto& node) { return node.guid == m_SelectedGraphNode; });
	if (selected == m_Document.graph.nodes.end())
	{
		ImGui::TextDisabled("Select a node to edit it");
		ImGui::EndTable();
		return;
	}
	const auto nodeType = std::find_if(m_GraphCatalog.begin(), m_GraphCatalog.end(),
		[&](const auto& item) { return item.type == selected->type; });
	ImGui::TextUnformatted(nodeType == m_GraphCatalog.end()
		? selected->type.c_str() : nodeType->displayName.c_str());
	ImGui::TextDisabled("%s", selected->guid.c_str());
	if (selected->guid != m_Document.graph.entryNode && ImGui::Button("Set Entry"))
	{
		Vans::EditorAPI::GAFGraphEditRequest request;
		request.operation = Vans::EditorAPI::GAFGraphEditOperation::SetEntryNode;
		request.nodeGuid = selected->guid;
		ApplyGraphOperation(editorAPI, std::move(request));
		ImGui::EndTable();
		return;
	}
	ImGui::SeparatorText("Connect");
	char outputBuffer[128]{};
	std::snprintf(outputBuffer, sizeof(outputBuffer), "%s", m_GraphConnectOutput.c_str());
	if (ImGui::InputTextWithHint("##output-pin", "Output pin", outputBuffer,
		sizeof(outputBuffer))) m_GraphConnectOutput = outputBuffer;
	if (nodeType != m_GraphCatalog.end())
	{
		for (const auto& pin : nodeType->pins)
			if (!pin.input && pin.name != "*" && pin.name.find('*') == std::string::npos)
			{
				ImGui::SameLine();
				if (ImGui::SmallButton(pin.name.c_str())) m_GraphConnectOutput = pin.name;
			}
	}
	if (ImGui::BeginCombo("Target", m_GraphConnectTarget.empty()
		? "Select node" : m_GraphConnectTarget.c_str()))
	{
		for (const auto& target : m_Document.graph.nodes)
			if (target.guid != selected->guid && ImGui::Selectable(target.guid.c_str(),
				target.guid == m_GraphConnectTarget)) m_GraphConnectTarget = target.guid;
		ImGui::EndCombo();
	}
	ImGui::BeginDisabled(m_GraphConnectOutput.empty() || m_GraphConnectTarget.empty());
	if (ImGui::Button("Connect", ImVec2(-1.0f, 0.0f)))
	{
		Vans::EditorAPI::GAFGraphEditRequest request;
		request.operation = Vans::EditorAPI::GAFGraphEditOperation::Connect;
		request.fromNode = selected->guid;
		request.outputPin = m_GraphConnectOutput;
		request.toNode = m_GraphConnectTarget;
		ApplyGraphOperation(editorAPI, std::move(request));
		ImGui::EndDisabled();
		ImGui::EndTable();
		return;
	}
	ImGui::EndDisabled();
	ImGui::SeparatorText("Properties");
	if (nodeType != m_GraphCatalog.end())
	{
		for (const auto& property : nodeType->properties)
		{
			const auto current = std::find_if(selected->propertyValues.begin(),
				selected->propertyValues.end(), [&](const auto& value)
				{ return value.name == property.name; });
			Vans::EditorAPI::GAFEditorValue value = current == selected->propertyValues.end()
				? property.defaultValue : current->value;
			ImGui::PushID(property.name.c_str());
			ImGui::TextDisabled("%s", property.displayName.c_str());
			bool edited = false;
			if (property.kind == Vans::EditorAPI::GAFEditorPropertyKind::Bool)
			{
				edited = ImGui::Checkbox("##value", &value.boolValue);
				value.kind = Vans::EditorAPI::GAFEditorValueKind::Bool;
			}
			else if (property.kind == Vans::EditorAPI::GAFEditorPropertyKind::Int)
			{
				edited = ImGui::InputScalar("##value", ImGuiDataType_S64, &value.intValue,
					nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue);
				value.kind = Vans::EditorAPI::GAFEditorValueKind::Int;
			}
			else if (property.kind == Vans::EditorAPI::GAFEditorPropertyKind::Float)
			{
				edited = ImGui::InputDouble("##value", &value.floatValue, 0.0, 0.0, "%.6g",
					ImGuiInputTextFlags_EnterReturnsTrue);
				value.kind = Vans::EditorAPI::GAFEditorValueKind::Float;
			}
			else if (property.kind == Vans::EditorAPI::GAFEditorPropertyKind::String)
			{
				char buffer[512]{};
				std::snprintf(buffer, sizeof(buffer), "%s", value.stringValue.c_str());
				edited = ImGui::InputText("##value", buffer, sizeof(buffer),
					ImGuiInputTextFlags_EnterReturnsTrue);
				if (edited) value.stringValue = buffer;
				value.kind = Vans::EditorAPI::GAFEditorValueKind::String;
			}
			else if (property.kind == Vans::EditorAPI::GAFEditorPropertyKind::AssetReference)
			{
				char buffer[512]{};
				std::snprintf(buffer, sizeof(buffer), "%s", value.stringValue.c_str());
				edited = ImGui::InputText("##value", buffer, sizeof(buffer),
					ImGuiInputTextFlags_EnterReturnsTrue);
				if (edited) value.stringValue = buffer;
				value.kind = Vans::EditorAPI::GAFEditorValueKind::String;
				ImGui::SameLine();
				if (ImGui::SmallButton("...")) ImGui::OpenPopup("Graph Asset Picker");
				if (ImGui::BeginPopup("Graph Asset Picker"))
				{
					bool any = false;
					for (const auto type : property.allowedAssetTypes)
						for (const auto& asset : editorAPI.QueryAssets({ type, false }))
						{
							any = true;
							const std::string label = asset.name + "##" + asset.guid;
							if (ImGui::Selectable(label.c_str()))
							{
								value.stringValue = asset.guid;
								edited = true;
								ImGui::CloseCurrentPopup();
							}
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("%s", asset.relativePath.c_str());
						}
					if (!any) ImGui::TextDisabled("No compatible assets");
					ImGui::EndPopup();
				}
			}
			else if (ImGui::Button("Edit JSON", ImVec2(-1.0f, 0.0f)))
			{
				m_GraphPropertyNode = selected->guid;
				m_GraphPropertyName = property.name;
				m_GraphPropertyKind = Vans::EditorAPI::GAFEditorValueKind::Json;
				const std::size_t capacity = (std::max<std::size_t>)(
					65536, value.canonicalJson.size() + 1024);
				m_GraphPropertyBuffer.assign(capacity, '\0');
				std::memcpy(m_GraphPropertyBuffer.data(), value.canonicalJson.data(),
					(std::min)(value.canonicalJson.size(), capacity - 1));
				m_GraphPropertyEditorOpen = true;
			}
			if (edited)
			{
				Vans::EditorAPI::GAFGraphEditRequest request;
				request.operation = Vans::EditorAPI::GAFGraphEditOperation::SetNodeProperty;
				request.nodeGuid = selected->guid;
				request.propertyName = property.name;
				request.value = std::move(value);
				ApplyGraphOperation(editorAPI, std::move(request));
				ImGui::PopID();
				ImGui::EndTable();
				return;
			}
			ImGui::PopID();
		}
	}
	ImGui::SeparatorText("Connections");
	for (const auto& edge : m_Document.graph.edges)
	{
		if (edge.from != selected->guid && edge.to != selected->guid) continue;
		ImGui::PushID(static_cast<int>(edge.index));
		ImGui::TextWrapped("%s.%s -> %s", edge.from.c_str(), edge.output.c_str(), edge.to.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("x"))
		{
			Vans::EditorAPI::GAFGraphEditRequest request;
			request.operation = Vans::EditorAPI::GAFGraphEditOperation::Disconnect;
			request.fromNode = edge.from;
			request.outputPin = edge.output;
			request.toNode = edge.to;
			ApplyGraphOperation(editorAPI, std::move(request));
			ImGui::PopID();
			ImGui::EndTable();
			return;
		}
		ImGui::PopID();
	}
	ImGui::EndTable();
}

void VansGameplayActionEditorWindow::DrawGraphPropertyEditor(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (m_GraphPropertyEditorOpen)
	{
		ImGui::OpenPopup("Edit Graph Node Property");
		m_GraphPropertyEditorOpen = false;
	}
	if (!ImGui::BeginPopupModal("Edit Graph Node Property", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize)) return;
	ImGui::Text("%s / %s", m_GraphPropertyNode.c_str(), m_GraphPropertyName.c_str());
	ImGui::InputTextMultiline("##graph-property-json", m_GraphPropertyBuffer.data(),
		m_GraphPropertyBuffer.size(), ImVec2(620.0f, 360.0f), ImGuiInputTextFlags_AllowTabInput);
	if (ImGui::Button("Apply"))
	{
		Vans::EditorAPI::GAFGraphEditRequest request;
		request.operation = Vans::EditorAPI::GAFGraphEditOperation::SetNodeProperty;
		request.nodeGuid = m_GraphPropertyNode;
		request.propertyName = m_GraphPropertyName;
		request.value.kind = m_GraphPropertyKind;
		request.value.canonicalJson = m_GraphPropertyBuffer.data();
		request.sourcePath = m_Path;
		const auto result = editorAPI.EditGAFGraph(request);
		ApplyOperation(result);
		if (result.success)
		{
			m_Diff = editorAPI.DiffGAFAsset(m_Path, m_BaselineCanonicalJson);
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
	if (!m_LastError.empty())
		ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.28f, 1.0f), "%s", m_LastError.c_str());
	ImGui::EndPopup();
}

void VansGameplayActionEditorWindow::DrawDiff(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (ImGui::Button("Refresh Diff"))
		m_Diff = editorAPI.DiffGAFAsset(m_Path, m_BaselineCanonicalJson);
	if (!m_Diff.success)
	{
		ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.28f, 1.0f), "%s", m_Diff.message.c_str());
		return;
	}
	if (m_Diff.entries.empty())
	{
		ImGui::TextDisabled("No semantic changes");
		return;
	}
	if (ImGui::BeginTable("##gaf-diff", 2,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
	{
		ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn("Field");
		ImGui::TableHeadersRow();
		for (const auto& entry : m_Diff.entries)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(ChangeLabel(entry.kind));
			ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(entry.fieldPath.c_str());
		}
		ImGui::EndTable();
	}
}


void VansGameplayActionEditorWindow::DrawStructuredEditor(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (m_StructuredEditorOpen)
	{
		ImGui::OpenPopup("Edit Structured Value");
		m_StructuredEditorOpen = false;
	}
	if (!ImGui::BeginPopupModal("Edit Structured Value", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize)) return;
	ImGui::TextUnformatted(m_StructuredPath.c_str());
	ImGui::InputTextMultiline("##json", m_StructuredBuffer.data(), m_StructuredBuffer.size(),
		ImVec2(680.0f, 420.0f), ImGuiInputTextFlags_AllowTabInput);
	if (ImGui::Button("Apply"))
	{
		Vans::EditorAPI::GAFEditorValue value;
		value.kind = m_StructuredKind;
		value.canonicalJson = m_StructuredBuffer.data();
		Vans::EditorAPI::GAFEditorFieldEditRequest request;
		request.sourcePath = m_Path;
		request.fieldPath = m_StructuredPath;
		request.value = std::move(value);
		const auto result = editorAPI.SetGAFAssetField(request);
		ApplyOperation(result);
		if (result.success)
		{
			m_Diff = editorAPI.DiffGAFAsset(m_Path, m_BaselineCanonicalJson);
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
	if (!m_LastError.empty())
		ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.28f, 1.0f), "%s", m_LastError.c_str());
	ImGui::EndPopup();
}

void VansGameplayActionEditorWindow::DrawCloseConfirmation(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (m_CloseRequested)
	{
		if (!m_Document.dirty)
		{
			Close();
			return;
		}
		ImGui::OpenPopup("Unsaved GAF Asset");
		m_CloseRequested = false;
	}
	if (!ImGui::BeginPopupModal("Unsaved GAF Asset", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize)) return;
	ImGui::TextUnformatted(m_Document.sourcePath.c_str());
	if (ImGui::Button("Save"))
	{
		const auto result = editorAPI.SaveGAFAsset(m_Path);
		ApplyOperation(result);
		if (result.success)
		{
			ImGui::CloseCurrentPopup();
			Close();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Discard"))
	{
		const auto result = editorAPI.RevertGAFAsset(m_Path);
		ApplyOperation(result);
		if (result.success)
		{
			ImGui::CloseCurrentPopup();
			Close();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

void VansGameplayActionEditorWindow::ShowWindow(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!m_IsOpen) return;
	if (m_NeedsRefresh) Refresh(editorAPI);
	bool open = true;
	const std::string title = (m_Document.assetKind.empty() ? "GAF Asset" : m_Document.assetKind) +
		" Editor###GameplayActionEditor";
	if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_MenuBar))
	{
		ImGui::End();
		if (!open) m_CloseRequested = true;
		DrawCloseConfirmation(editorAPI);
		return;
	}
	DrawMenuBar(editorAPI);
	if (!m_Document.success)
	{
		ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.28f, 1.0f), "%s", m_LastError.c_str());
	}
	else
	{
		DrawToolbar(editorAPI);
		if (!m_LastError.empty())
			ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.28f, 1.0f), "%s", m_LastError.c_str());
		ImGui::Separator();
		if (ImGui::BeginTabBar("##gaf-tabs"))
		{
			if (ImGui::BeginTabItem("Overview")) { DrawOverview(); ImGui::EndTabItem(); }
			if (m_Document.graph.available && ImGui::BeginTabItem("Graph"))
			{
				DrawGraph(editorAPI);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Properties")) { DrawProperties(editorAPI); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("Semantic Diff")) { DrawDiff(editorAPI); ImGui::EndTabItem(); }
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
	if (!open) m_CloseRequested = true;
	DrawStructuredEditor(editorAPI);
	DrawGraphPropertyEditor(editorAPI);
	DrawCloseConfirmation(editorAPI);
}
}
