#include "VansBoneMaskEditorWindow.h"

#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../VansAssetDocumentEditService.h"
#include "../VansAssetDocumentRegistry.h"
#include "../VansEditorAssetSaveService.h"
#include "../../Util/VansLog.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace VansGraphics
{
using VansBoneMaskRuleMode = Vans::EditorAPI::BoneMaskRuleMode;
using VansBoneMaskFalloff = Vans::EditorAPI::BoneMaskFalloff;
using VansBoneMaskDiagnosticSeverity = Vans::EditorAPI::BoneMaskDiagnosticSeverity;
using VansBoneMaskBranchRule = Vans::EditorAPI::BoneMaskRuleDTO;

namespace
{
	std::string Lower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		return value;
	}

	bool ContainsInsensitive(const std::string& value, const char* filter)
	{
		if (!filter || !*filter)
			return true;
		return Lower(value).find(Lower(filter)) != std::string::npos;
	}

	bool EditString(const char* label, std::string& value)
	{
		char buffer[256]{};
		std::strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
		if (!ImGui::InputText(label, buffer, sizeof(buffer)))
			return false;
		value = buffer;
		return true;
	}

	const char* RuleModeName(VansBoneMaskRuleMode mode)
	{
		return mode == VansBoneMaskRuleMode::Include ? "Include" : "Exclude";
	}

	const char* FalloffName(VansBoneMaskFalloff falloff)
	{
		switch (falloff)
		{
		case VansBoneMaskFalloff::Constant: return "Constant";
		case VansBoneMaskFalloff::SmoothStep: return "Smooth Step";
		case VansBoneMaskFalloff::Linear: return "Linear";
		}
		return "Linear";
	}

	ImU32 WeightColor(float weight)
	{
		weight = std::clamp(weight, 0.0f, 1.0f);
		return ImGui::ColorConvertFloat4ToU32(ImVec4(
			0.08f + 0.92f * weight,
			0.18f + 0.55f * (1.0f - std::fabs(weight * 2.0f - 1.0f)),
			0.95f - 0.82f * weight,
			1.0f));
	}
}

void VansBoneMaskEditorWindow::Open(const std::string& boneMaskPath)
{
	if (m_IsOpen && (m_WorkingDirty || (m_Document && m_Document->IsDirty())))
	{
		m_LastError = "Save or discard the current Bone Mask before opening another asset";
		return;
	}
	Close();
	m_Path = boneMaskPath;
	m_Document = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(m_Path);
	if (!m_Document || !m_Document->sourceDocument.IsLoaded())
	{
		if (m_LastError.empty())
			m_LastError = m_Document ? m_Document->lastError : "Failed to open Bone Mask document";
		m_Document.reset();
		m_Path.clear();
		return;
	}
	m_IsOpen = true;
	m_CloseRequested = false;
	m_NeedsDecode = true;
	m_WorkingDirty = false;
	m_SelectedBones.clear();
	m_SelectedRule = m_Asset.branchRules.empty() ? -1 : 0;
	m_Skeleton = {};
	m_Compiled = {};
	m_LastError.clear();
}

void VansBoneMaskEditorWindow::Close()
{
	if (m_IsOpen && (m_WorkingDirty || (m_Document && m_Document->IsDirty())))
	{
		m_CloseRequested = true;
		return;
	}
	m_IsOpen = false;
	m_CloseRequested = false;
	m_NeedsDecode = false;
	m_WorkingDirty = false;
	m_Path.clear();
	m_Document.reset();
	m_ActiveAPI = nullptr;
	m_DocumentStateId = 0;
	m_Asset = {};
	m_Compiled = {};
	m_Skeleton = {};
	m_SelectedBones.clear();
	m_SelectedRule = -1;
}

bool VansBoneMaskEditorWindow::DecodeDocument()
{
	if (!m_Document || !m_Document->sourceDocument.IsLoaded())
		return false;
	if (!m_ActiveAPI)
	{
		m_LastError = "Bone Mask authoring API is not available";
		return false;
	}
	const nlohmann::json root = Vans::EncodeSerializedValueJson<nlohmann::json>(
		m_Document->sourceDocument.SerializedRootSnapshot());
	auto decoded = m_ActiveAPI->DecodeBoneMaskDocument(root.dump());
	if (!decoded.success)
	{
		m_LastError = decoded.message;
		return false;
	}
	m_Asset = std::move(decoded.document);
	m_NeedsDecode = false;
	m_DocumentStateId = m_Document->sourceDocument.CurrentStateId();
	m_WorkingDirty = false;
	if (m_SelectedRule >= static_cast<int>(m_Asset.branchRules.size()))
		m_SelectedRule = m_Asset.branchRules.empty() ? -1 : static_cast<int>(m_Asset.branchRules.size()) - 1;
	if (m_ActiveAPI)
		RefreshSkeleton();
	return true;
}

bool VansBoneMaskEditorWindow::CommitDocument()
{
	if (!m_WorkingDirty)
		return true;
	if (!m_Document)
	{
		m_LastError = "Bone Mask document is not available";
		return false;
	}
	if (!m_ActiveAPI)
	{
		m_LastError = "Bone Mask authoring API is not available";
		return false;
	}
	const auto encoded = m_ActiveAPI->EncodeBoneMaskDocument(m_Asset);
	if (!encoded.success)
	{
		m_LastError = encoded.message;
		return false;
	}
	const nlohmann::json root = nlohmann::json::parse(encoded.canonicalJson);
	const auto result = Vans::VansAssetDocumentEditService::ReplaceRoot(
		m_Document->sourceDocument,
		Vans::DecodeSerializedValueJson(root));
	if (!result && result.message != "Asset property is unchanged")
	{
		m_LastError = result.message;
		return false;
	}
	m_DocumentStateId = m_Document->sourceDocument.CurrentStateId();
	m_WorkingDirty = false;
	m_LastError.clear();
	return true;
}

bool VansBoneMaskEditorWindow::Save()
{
	if (!CommitDocument())
		return false;
	if (!m_Document || !m_ActiveAPI)
	{
		m_LastError = "Bone Mask save API is not available";
		return false;
	}
	const auto result = Vans::VansEditorAssetSaveService::Get().SaveAsset(*m_ActiveAPI, m_Document);
	if (!result)
	{
		m_LastError = result.message.empty() ? "Bone Mask save failed" : result.message;
		return false;
	}
	m_LastError.clear();
	return true;
}

bool VansBoneMaskEditorWindow::Undo()
{
	if (!m_Document)
		return false;
	if (m_WorkingDirty && !CommitDocument())
		return DecodeDocument();
	const auto result = Vans::VansAssetDocumentEditService::Undo(m_Document->sourceDocument);
	if (!result)
	{
		m_LastError = result.message;
		return false;
	}
	return DecodeDocument();
}

bool VansBoneMaskEditorWindow::Redo()
{
	if (!m_Document || (m_WorkingDirty && !CommitDocument()))
		return false;
	const auto result = Vans::VansAssetDocumentEditService::Redo(m_Document->sourceDocument);
	if (!result)
	{
		m_LastError = result.message;
		return false;
	}
	return DecodeDocument();
}

void VansBoneMaskEditorWindow::MarkEdited()
{
	m_WorkingDirty = true;
	Recompile();
}

void VansBoneMaskEditorWindow::RefreshSkeleton()
{
	m_Skeleton = {};
	m_SelectedBones.clear();
	if (!m_ActiveAPI || m_Asset.previewSkeletonGuid.empty())
	{
		Recompile();
		return;
	}
	m_Skeleton = m_ActiveAPI->GetAssetSkeletonSnapshot(m_Asset.previewSkeletonGuid);
	if (!m_Skeleton.available && !m_Skeleton.error.empty())
		m_LastError = m_Skeleton.error;
	Recompile();
}

void VansBoneMaskEditorWindow::Recompile()
{
	if (!m_Skeleton.available)
	{
		m_Compiled = {};
		return;
	}
	if (!m_ActiveAPI)
	{
		m_Compiled = {};
		return;
	}
	m_Compiled = m_ActiveAPI->CompileBoneMaskDocument(m_Asset, m_Skeleton);
}

void VansBoneMaskEditorWindow::SelectBone(int boneIndex)
{
	if (m_LockSelection || boneIndex < 0 || boneIndex >= static_cast<int>(m_Skeleton.bones.size()))
		return;
	if (ImGui::GetIO().KeyCtrl)
	{
		if (!m_SelectedBones.erase(boneIndex))
			m_SelectedBones.insert(boneIndex);
	}
	else
	{
		m_SelectedBones.clear();
		m_SelectedBones.insert(boneIndex);
	}
}

void VansBoneMaskEditorWindow::DrawBoneTreeNode(int boneIndex)
{
	if (boneIndex < 0 || boneIndex >= static_cast<int>(m_Skeleton.bones.size()))
		return;
	const auto& bone = m_Skeleton.bones[boneIndex];
	bool descendantMatches = ContainsInsensitive(bone.name, m_Search);
	for (std::size_t child = 0; child < m_Skeleton.bones.size() && !descendantMatches; ++child)
		if (m_Skeleton.bones[child].parentIndex == boneIndex
			&& ContainsInsensitive(m_Skeleton.bones[child].name, m_Search))
			descendantMatches = true;
	if (!descendantMatches && *m_Search)
		return;
	const float weight = boneIndex < static_cast<int>(m_Compiled.weights.size())
		? m_Compiled.weights[boneIndex] : 0.0f;
	if (m_ShowOnlyWeighted && weight <= 0.0001f)
		return;
	bool hasChildren = false;
	for (const auto& candidate : m_Skeleton.bones)
		if (candidate.parentIndex == boneIndex) { hasChildren = true; break; }
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
	if (m_SelectedBones.find(boneIndex) != m_SelectedBones.end()) flags |= ImGuiTreeNodeFlags_Selected;
	const auto expanded = std::find(m_Asset.editorExpandedBones.begin(),
		m_Asset.editorExpandedBones.end(), bone.name);
	ImGui::SetNextItemOpen(expanded != m_Asset.editorExpandedBones.end(), ImGuiCond_Once);
	ImGui::PushID(boneIndex);
	ImGui::PushStyleColor(ImGuiCol_Text, WeightColor(weight));
	const bool open = ImGui::TreeNodeEx("bone", flags, "%s  %.2f", bone.name.c_str(), weight);
	ImGui::PopStyleColor();
	if (ImGui::IsItemToggledOpen() && hasChildren)
	{
		if (open && expanded == m_Asset.editorExpandedBones.end())
			m_Asset.editorExpandedBones.push_back(bone.name);
		else if (!open && expanded != m_Asset.editorExpandedBones.end())
			m_Asset.editorExpandedBones.erase(expanded);
		MarkEdited();
	}
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) SelectBone(boneIndex);
	if (open)
	{
		for (int child = 0; child < static_cast<int>(m_Skeleton.bones.size()); ++child)
			if (m_Skeleton.bones[child].parentIndex == boneIndex) DrawBoneTreeNode(child);
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void VansBoneMaskEditorWindow::DrawSkeletonTree()
{
	ImGui::TextUnformatted("Skeleton Tree");
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##BoneSearch", "Search bones", m_Search, sizeof(m_Search));
	ImGui::Checkbox("Lock Selection", &m_LockSelection);
	ImGui::SameLine();
	ImGui::Checkbox("Weighted Only", &m_ShowOnlyWeighted);
	ImGui::Separator();
	if (!m_Skeleton.available)
	{
		ImGui::TextDisabled("Choose a Preview Skeleton model.");
		return;
	}
	for (int index = 0; index < static_cast<int>(m_Skeleton.bones.size()); ++index)
		if (m_Skeleton.bones[index].parentIndex < 0) DrawBoneTreeNode(index);
}

void VansBoneMaskEditorWindow::DrawSkeletonPreview()
{
	ImGui::TextUnformatted("Isolated Bind-Pose Heatmap");
	ImGui::SameLine();
	ImGui::TextDisabled("drag: orbit  wheel: zoom");
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();
	size.y = std::max(size.y, 220.0f);
	ImGui::InvisibleButton("BoneMaskHeatmap", size,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 end(origin.x + size.x, origin.y + size.y);
	draw->AddRectFilled(origin, end, IM_COL32(21, 24, 31, 255));
	draw->AddRect(origin, end, IM_COL32(66, 72, 84, 255));
	if (!m_Skeleton.available || m_Skeleton.bones.empty())
	{
		draw->AddText(ImVec2(origin.x + 14.0f, origin.y + 14.0f), IM_COL32(155, 160, 170, 255),
			"No preview skeleton");
		return;
	}
	if (ImGui::IsItemHovered())
	{
		m_PreviewZoom = std::clamp(m_PreviewZoom + ImGui::GetIO().MouseWheel * 0.08f, 0.2f, 3.0f);
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			m_PreviewYaw += ImGui::GetIO().MouseDelta.x * 0.01f;
			m_PreviewPitch = std::clamp(m_PreviewPitch + ImGui::GetIO().MouseDelta.y * 0.01f, -1.45f, 1.45f);
		}
	}

	struct Point { float x; float y; float z; };
	std::vector<Point> points;
	points.reserve(m_Skeleton.bones.size());
	Point center{};
	for (const auto& bone : m_Skeleton.bones)
	{
		center.x += bone.bindPosition.x;
		center.y += bone.bindPosition.y;
		center.z += bone.bindPosition.z;
	}
	const float invCount = 1.0f / static_cast<float>(m_Skeleton.bones.size());
	center.x *= invCount; center.y *= invCount; center.z *= invCount;
	const float cy = std::cos(m_PreviewYaw), sy = std::sin(m_PreviewYaw);
	const float cp = std::cos(m_PreviewPitch), sp = std::sin(m_PreviewPitch);
	float radius = 0.0001f;
	for (const auto& bone : m_Skeleton.bones)
	{
		const float x = bone.bindPosition.x - center.x;
		const float y = bone.bindPosition.y - center.y;
		const float z = bone.bindPosition.z - center.z;
		const float rx = cy * x + sy * z;
		const float rz = -sy * x + cy * z;
		const float ry = cp * y - sp * rz;
		const float rz2 = sp * y + cp * rz;
		points.push_back({ rx, ry, rz2 });
		radius = std::max(radius, std::max(std::fabs(rx), std::fabs(ry)));
	}
	const float scale = 0.43f * std::min(size.x, size.y) * m_PreviewZoom / radius;
	const ImVec2 middle(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
	std::vector<ImVec2> screen(points.size());
	for (std::size_t index = 0; index < points.size(); ++index)
		screen[index] = ImVec2(
			middle.x + points[index].x * scale,
			middle.y - points[index].y * scale);
	for (std::size_t index = 0; index < points.size(); ++index)
	{
		const int parent = m_Skeleton.bones[index].parentIndex;
		if (parent >= 0 && parent < static_cast<int>(points.size()))
		{
			const float weight = index < m_Compiled.weights.size() ? m_Compiled.weights[index] : 0.0f;
			draw->AddLine(screen[parent], screen[index], WeightColor(weight), 2.0f);
		}
	}
	int hovered = -1;
	float nearest = 9.0f * 9.0f;
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	for (std::size_t index = 0; index < points.size(); ++index)
	{
		const float dx = screen[index].x - mouse.x;
		const float dy = screen[index].y - mouse.y;
		const float distance = dx * dx + dy * dy;
		if (distance < nearest) { nearest = distance; hovered = static_cast<int>(index); }
		const float weight = index < m_Compiled.weights.size() ? m_Compiled.weights[index] : 0.0f;
		const bool selected = m_SelectedBones.find(static_cast<int>(index)) != m_SelectedBones.end();
		draw->AddCircleFilled(screen[index], selected ? 5.5f : 3.5f, WeightColor(weight));
		if (selected) draw->AddCircle(screen[index], 7.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
	}
	if (ImGui::IsItemHovered() && hovered >= 0)
	{
		const float weight = hovered < static_cast<int>(m_Compiled.weights.size()) ? m_Compiled.weights[hovered] : 0.0f;
		ImGui::SetTooltip("%s\nFinal weight %.3f", m_Skeleton.bones[hovered].name.c_str(), weight);
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) SelectBone(hovered);
	}
	draw->AddRectFilled(ImVec2(origin.x + 10.0f, origin.y + size.y - 28.0f),
		ImVec2(origin.x + 150.0f, origin.y + size.y - 10.0f), IM_COL32(15, 18, 24, 210), 3.0f);
	draw->AddText(ImVec2(origin.x + 16.0f, origin.y + size.y - 26.0f), IM_COL32(90, 130, 245, 255), "0");
	draw->AddText(ImVec2(origin.x + 76.0f, origin.y + size.y - 26.0f), IM_COL32(235, 180, 75, 255), "weight");
	draw->AddText(ImVec2(origin.x + 132.0f, origin.y + size.y - 26.0f), IM_COL32(245, 75, 55, 255), "1");
}

void VansBoneMaskEditorWindow::AddRule(VansBoneMaskRuleMode mode)
{
	if (m_SelectedBones.empty() || !m_Skeleton.available)
	{
		m_LastError = "Select a skeleton bone before adding a branch rule";
		return;
	}
	const int boneIndex = *m_SelectedBones.begin();
	VansBoneMaskBranchRule rule;
	rule.id = Vans::VansAssetGuid::New().ToString();
	rule.mode = mode;
	rule.rootBone = m_Skeleton.bones[boneIndex].name;
	rule.rootWeight = mode == VansBoneMaskRuleMode::Include ? 1.0f : 0.0f;
	rule.endWeight = rule.rootWeight;
	rule.falloff = VansBoneMaskFalloff::Constant;
	m_Asset.branchRules.push_back(std::move(rule));
	m_SelectedRule = static_cast<int>(m_Asset.branchRules.size()) - 1;
	MarkEdited();
}

void VansBoneMaskEditorWindow::ApplyTemplate(const char* templateName)
{
	if (!m_Skeleton.available)
	{
		m_LastError = "Choose a Preview Skeleton before applying a template";
		return;
	}
	const std::string requested = templateName ? Lower(templateName) : std::string{};
	auto findBone = [&](std::initializer_list<const char*> tokens) -> int
	{
		for (int index = 0; index < static_cast<int>(m_Skeleton.bones.size()); ++index)
		{
			const std::string name = Lower(m_Skeleton.bones[index].name);
			for (const char* token : tokens)
				if (name.find(Lower(token)) != std::string::npos) return index;
		}
		return -1;
	};
	int root = -1;
	if (requested == "upper body") root = findBone({ "spine_01", "spine1", "spine", "chest" });
	else if (requested == "lower body") root = findBone({ "pelvis", "hips", "hip" });
	else if (requested == "left arm") root = findBone({ "upperarm_l", "leftarm", "l_upperarm", "arm_l" });
	else if (requested == "right arm") root = findBone({ "upperarm_r", "rightarm", "r_upperarm", "arm_r" });
	else if (requested == "left hand") root = findBone({ "hand_l", "lefthand", "l_hand" });
	else if (requested == "right hand") root = findBone({ "hand_r", "righthand", "r_hand" });
	if (root < 0)
	{
		m_LastError = std::string("Template could not find a compatible ") + templateName + " root bone";
		return;
	}
	m_Asset.defaultWeight = 0.0f;
	m_Asset.branchRules.clear();
	m_Asset.explicitWeights.clear();
	VansBoneMaskBranchRule include;
	include.id = Vans::VansAssetGuid::New().ToString();
	include.mode = VansBoneMaskRuleMode::Include;
	include.rootBone = m_Skeleton.bones[root].name;
	include.includeDescendants = true;
	include.rootWeight = 1.0f;
	include.endWeight = 1.0f;
	include.falloff = VansBoneMaskFalloff::Constant;
	m_Asset.branchRules.push_back(std::move(include));
	if (requested == "lower body")
	{
		const int spine = findBone({ "spine_01", "spine1", "spine", "chest" });
		if (spine >= 0)
		{
			VansBoneMaskBranchRule exclude;
			exclude.id = Vans::VansAssetGuid::New().ToString();
			exclude.mode = VansBoneMaskRuleMode::Exclude;
			exclude.rootBone = m_Skeleton.bones[spine].name;
			exclude.rootWeight = 0.0f;
			exclude.endWeight = 0.0f;
			exclude.falloff = VansBoneMaskFalloff::Constant;
			m_Asset.branchRules.push_back(std::move(exclude));
		}
	}
	m_SelectedRule = 0;
	MarkEdited();
	m_LastError.clear();
}

void VansBoneMaskEditorWindow::DrawRulesPanel()
{
	if (EditString("Name", m_Asset.name)) MarkEdited();
	if (ImGui::SliderFloat("Default Weight", &m_Asset.defaultWeight, 0.0f, 1.0f)) MarkEdited();

	const auto models = m_ActiveAPI
		? m_ActiveAPI->QueryAssets({ Vans::EditorAPI::AssetType::Model, false })
		: std::vector<Vans::EditorAPI::AssetEntry>{};
	const char* currentModel = m_Asset.previewSkeletonPathHint.empty()
		? "Choose Model..." : m_Asset.previewSkeletonPathHint.c_str();
	if (ImGui::BeginCombo("Preview Skeleton", currentModel))
	{
		for (const auto& model : models)
		{
			const bool selected = model.guid == m_Asset.previewSkeletonGuid;
			if (ImGui::Selectable(model.relativePath.c_str(), selected))
			{
				m_Asset.previewSkeletonGuid = model.guid;
				m_Asset.previewSkeletonPathHint = model.relativePath;
				RefreshSkeleton();
				MarkEdited();
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	if (!m_Asset.previewSkeletonGuid.empty())
	{
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##Skeleton"))
		{
			m_Asset.previewSkeletonGuid.clear();
			m_Asset.previewSkeletonPathHint.clear();
			RefreshSkeleton();
			MarkEdited();
		}
	}

	ImGui::SeparatorText("Templates");
	for (const char* name : { "Upper Body", "Lower Body", "Left Arm", "Right Arm", "Left Hand", "Right Hand" })
	{
		if (ImGui::SmallButton(name)) ApplyTemplate(name);
		if (std::strcmp(name, "Right Hand") != 0) ImGui::SameLine();
	}

	ImGui::SeparatorText("Branch Rules");
	if (ImGui::Button("+ Include")) AddRule(VansBoneMaskRuleMode::Include);
	ImGui::SameLine();
	if (ImGui::Button("+ Exclude")) AddRule(VansBoneMaskRuleMode::Exclude);
	for (int index = 0; index < static_cast<int>(m_Asset.branchRules.size()); ++index)
	{
		const auto& rule = m_Asset.branchRules[index];
		const std::string label = std::string(RuleModeName(rule.mode)) + "  " + rule.rootBone + "##" + rule.id;
		if (ImGui::Selectable(label.c_str(), m_SelectedRule == index)) m_SelectedRule = index;
	}
	if (m_SelectedRule >= 0 && m_SelectedRule < static_cast<int>(m_Asset.branchRules.size()))
	{
		ImGui::SeparatorText("Selected Rule");
		auto& rule = m_Asset.branchRules[m_SelectedRule];
		int mode = rule.mode == VansBoneMaskRuleMode::Include ? 0 : 1;
		if (ImGui::Combo("Mode", &mode, "Include\0Exclude\0"))
		{
			rule.mode = mode == 0 ? VansBoneMaskRuleMode::Include : VansBoneMaskRuleMode::Exclude;
			MarkEdited();
		}
		if (EditString("Root Bone", rule.rootBone)) MarkEdited();
		if (!m_SelectedBones.empty())
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Use Selection"))
			{
				rule.rootBone = m_Skeleton.bones[*m_SelectedBones.begin()].name;
				MarkEdited();
			}
		}
		if (ImGui::Checkbox("Include Descendants", &rule.includeDescendants)) MarkEdited();
		if (ImGui::InputInt("Max Depth (-1 = all)", &rule.maxDepth))
		{
			rule.maxDepth = std::max(rule.maxDepth, -1);
			MarkEdited();
		}
		if (ImGui::SliderFloat("Root Weight", &rule.rootWeight, 0.0f, 1.0f)) MarkEdited();
		if (ImGui::SliderFloat("End Weight", &rule.endWeight, 0.0f, 1.0f)) MarkEdited();
		int falloff = static_cast<int>(rule.falloff);
		if (ImGui::Combo("Falloff", &falloff, "Constant\0Linear\0Smooth Step\0"))
		{
			rule.falloff = static_cast<VansBoneMaskFalloff>(falloff);
			MarkEdited();
		}
		ImGui::TextDisabled("Curve: %s", FalloffName(rule.falloff));
		if (ImGui::Button("Move Up") && m_SelectedRule > 0)
		{
			std::swap(m_Asset.branchRules[m_SelectedRule], m_Asset.branchRules[m_SelectedRule - 1]);
			--m_SelectedRule;
			MarkEdited();
		}
		ImGui::SameLine();
		if (ImGui::Button("Move Down") && m_SelectedRule + 1 < static_cast<int>(m_Asset.branchRules.size()))
		{
			std::swap(m_Asset.branchRules[m_SelectedRule], m_Asset.branchRules[m_SelectedRule + 1]);
			++m_SelectedRule;
			MarkEdited();
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete Rule"))
		{
			m_Asset.branchRules.erase(m_Asset.branchRules.begin() + m_SelectedRule);
			m_SelectedRule = std::min(m_SelectedRule, static_cast<int>(m_Asset.branchRules.size()) - 1);
			MarkEdited();
		}
	}

	ImGui::SeparatorText("Explicit Weight Brush");
	if (m_SelectedBones.empty())
		ImGui::TextDisabled("Select one or more bones (Ctrl-click for multi-select).");
	else
	{
		float value = 1.0f;
		const std::string& firstBone = m_Skeleton.bones[*m_SelectedBones.begin()].name;
		if (const auto found = m_Asset.explicitWeights.find(firstBone); found != m_Asset.explicitWeights.end())
			value = found->second;
		if (ImGui::SliderFloat("Selected Weight", &value, 0.0f, 1.0f))
		{
			for (int index : m_SelectedBones)
				m_Asset.explicitWeights[m_Skeleton.bones[index].name] = value;
			MarkEdited();
		}
		if (ImGui::Button("Clear Explicit Weight"))
		{
			for (int index : m_SelectedBones)
				m_Asset.explicitWeights.erase(m_Skeleton.bones[index].name);
			MarkEdited();
		}
	}
}

void VansBoneMaskEditorWindow::DrawDiagnostics()
{
	ImGui::SeparatorText("Compile Diagnostics");
	if (!m_LastError.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s", m_LastError.c_str());
	if (!m_Skeleton.available)
	{
		if (!m_Skeleton.error.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", m_Skeleton.error.c_str());
		else
			ImGui::TextDisabled("Preview compilation waits for a skeleton model.");
		return;
	}
	ImGui::Text("Bones: %zu   Active: %zu   Root Weight: %.3f",
		m_Compiled.weights.size(), m_Compiled.activeBones.size(), m_Compiled.rootWeight);
	if (m_Compiled.allZero)
		ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "Warning: the compiled mask is all zero.");
	if (m_Compiled.rootWeight > 0.0001f)
		ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
			"Root is weighted; Layers using Ignore Root Motion should verify this intentionally.");
	for (const auto& diagnostic : m_Compiled.diagnostics)
	{
		const ImVec4 color = diagnostic.severity == VansBoneMaskDiagnosticSeverity::Error
			? ImVec4(1.0f, 0.32f, 0.28f, 1.0f) : ImVec4(1.0f, 0.72f, 0.25f, 1.0f);
		ImGui::TextColored(color, "%s%s%s",
			diagnostic.ruleId.empty() ? "" : "[",
			diagnostic.ruleId.empty() ? "" : diagnostic.ruleId.c_str(),
			diagnostic.ruleId.empty() ? diagnostic.message.c_str() : ("] " + diagnostic.message).c_str());
	}
}

void VansBoneMaskEditorWindow::DrawToolbar()
{
	const bool canUndo = m_Document && Vans::VansAssetDocumentEditService::CanUndo(m_Document->sourceDocument);
	const bool canRedo = m_Document && Vans::VansAssetDocumentEditService::CanRedo(m_Document->sourceDocument);
	if (ImGui::Button("Save")) Save();
	ImGui::SameLine();
	ImGui::BeginDisabled(!canUndo);
	if (ImGui::Button("Undo")) Undo();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!canRedo);
	if (ImGui::Button("Redo")) Redo();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Compile")) Recompile();
	ImGui::SameLine();
	const bool dirty = m_WorkingDirty || (m_Document && m_Document->IsDirty());
	ImGui::TextColored(dirty ? ImVec4(1.0f, 0.72f, 0.25f, 1.0f) : ImVec4(0.45f, 0.85f, 0.55f, 1.0f),
		dirty ? "Unsaved" : "Saved");
}

void VansBoneMaskEditorWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!m_IsOpen)
		return;
	m_ActiveAPI = &editorAPI;
	if (m_NeedsDecode && !DecodeDocument())
	{
		m_IsOpen = false;
		return;
	}
	if (!m_Asset.previewSkeletonGuid.empty() && m_Skeleton.assetGuid != m_Asset.previewSkeletonGuid)
		RefreshSkeleton();
	if (m_Document && !m_WorkingDirty
		&& m_DocumentStateId != m_Document->sourceDocument.CurrentStateId())
		DecodeDocument();

	bool open = true;
	if (!ImGui::Begin(("Bone Mask Editor - " + m_Asset.name + "###BoneMaskEditor").c_str(), &open,
		ImGuiWindowFlags_MenuBar))
	{
		ImGui::End();
		return;
	}
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Save", "Ctrl+S")) Save();
			if (ImGui::MenuItem("Close")) Close();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
				m_Document && Vans::VansAssetDocumentEditService::CanUndo(m_Document->sourceDocument))) Undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false,
				m_Document && Vans::VansAssetDocumentEditService::CanRedo(m_Document->sourceDocument))) Redo();
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
	if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_S, false)) Save();
		else if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) Undo();
		else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
	}
	DrawToolbar();
	ImGui::Separator();
	if (ImGui::BeginTable("BoneMaskWorkspace", 3,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
		ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.72f)))
	{
		ImGui::TableSetupColumn("Tree", ImGuiTableColumnFlags_WidthFixed, 285.0f);
		ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Rules", ImGuiTableColumnFlags_WidthFixed, 370.0f);
		ImGui::TableNextColumn();
		ImGui::BeginChild("BoneMaskTree"); DrawSkeletonTree(); ImGui::EndChild();
		ImGui::TableNextColumn();
		ImGui::BeginChild("BoneMaskPreview"); DrawSkeletonPreview(); ImGui::EndChild();
		ImGui::TableNextColumn();
		ImGui::BeginChild("BoneMaskRules"); DrawRulesPanel(); ImGui::EndChild();
		ImGui::EndTable();
	}
	DrawDiagnostics();
	ImGui::End();

	if (!open)
		Close();
	if (m_WorkingDirty && !ImGui::IsAnyItemActive())
		CommitDocument();

	if (m_CloseRequested)
		ImGui::OpenPopup("Unsaved Bone Mask");
	if (ImGui::BeginPopupModal("Unsaved Bone Mask", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("The Bone Mask has unsaved changes.");
		if (ImGui::Button("Save and Close"))
		{
			if (Save())
			{
				m_WorkingDirty = false;
				m_Document.reset();
				m_IsOpen = false;
				m_CloseRequested = false;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard"))
		{
			if (m_Document && m_WorkingDirty)
				CommitDocument();
			const auto result = m_Document
				? Vans::VansAssetDocumentEditService::RevertToSaved(m_Document->sourceDocument)
				: Vans::AssetDocumentEditResult{ true, {} };
			if (result)
			{
				m_WorkingDirty = false;
				m_Document.reset();
				m_IsOpen = false;
				m_CloseRequested = false;
				ImGui::CloseCurrentPopup();
			}
			else
				m_LastError = result.message;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_CloseRequested = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
}
