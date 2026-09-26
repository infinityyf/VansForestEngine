#include "VansSceneAnimationPreviewWindow.h"
#include "../../EngineAPILayer/Public/IAnimationEditorAPI.h"
#include "../../EngineAPILayer/Public/IAnimationPreviewEditorAPI.h"
#include "../../AuthoringCore/VansAssetDocumentRegistry.h"
#include "../../AuthoringCore/VansAssetDocumentEditService.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>

namespace VansGraphics
{
namespace
{
using namespace Vans::EditorAPI;
bool EditRigText(const char* label, std::string& value)
{
	char buffer[512]{};
	std::memcpy(buffer, value.data(), std::min(value.size(), sizeof(buffer) - 1));
	if (!ImGui::InputText(label, buffer, sizeof(buffer))) return false;
	value = buffer;
	return true;
}
}

bool VansSceneAnimationPreviewWindow::ApplyRigWorkingCopy(IEngineEditorAPI& api,
	const AnimationRigDocumentDTO& document)
{
	IAnimationEditorAPI& animationAPI = api;
	IAnimationPreviewEditorAPI& previewAPI = api;
	auto asset = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(m_RigSnapshot.rigAssetPath);
	if (!asset || asset->sourceDocument.CurrentStateId() != m_RigDocumentStateId)
	{ m_Message = "Rig was edited elsewhere; restart preview to reload it"; return false; }
	const auto before = previewAPI.GetAnimationPreviewWorkingRigDocument(m_SessionId);
	const auto encoded = animationAPI.EncodeAnimationRigDocument(document);
	if (!before.success || !encoded.success)
	{ m_Message = before.success ? encoded.message : before.message; return false; }
	const auto applied = previewAPI.SetAnimationPreviewRigDefinition({m_SessionId, m_RigSnapshot.rigRevision, document});
	if (!applied.success) { m_Message = applied.message; return false; }
	const auto edited = Vans::VansAssetDocumentEditService::ReplaceRoot(asset->sourceDocument,
		Vans::DecodeSerializedValueJson(nlohmann::json::parse(encoded.canonicalJson)));
	if (!edited && edited.message != "Asset property is unchanged")
	{
		const auto rollback = previewAPI.SetAnimationPreviewRigDefinition({m_SessionId, applied.acceptedRevision, before.document});
		m_Message = edited.message + (rollback.success ? "" : "; preview rollback failed: " + rollback.message);
		RefreshRigSnapshot(previewAPI);
		return false;
	}
	m_RigDocumentStateId = asset->sourceDocument.CurrentStateId();
	RefreshRigSnapshot(previewAPI);
	m_Message = "Rig applied to preview and working copy; Save Animation Setup writes the asset";
	return true;
}

void VansSceneAnimationPreviewWindow::ChangeRigHistory(IEngineEditorAPI& api, bool redo)
{
	IAnimationEditorAPI& animationAPI = api;
	IAnimationPreviewEditorAPI& previewAPI = api;
	auto asset = Vans::VansAssetDocumentRegistry::Get().Find(m_RigSnapshot.rigAssetPath);
	if (!asset || asset->sourceDocument.CurrentStateId() != m_RigDocumentStateId)
	{ m_Message = "Rig was edited elsewhere; restart preview to reload it"; return; }
	const auto edit = redo ? Vans::VansAssetDocumentEditService::Redo(asset->sourceDocument)
		: Vans::VansAssetDocumentEditService::Undo(asset->sourceDocument);
	if (!edit) { m_Message = edit.message; return; }
	const auto json = Vans::EncodeSerializedValueJson<nlohmann::json>(asset->sourceDocument.SerializedRootSnapshot());
	const auto decoded = animationAPI.DecodeAnimationRigDocument(json.dump());
	bool applied = false;
	if (decoded.success)
	{
		const auto result = previewAPI.SetAnimationPreviewRigDefinition({m_SessionId, m_RigSnapshot.rigRevision, decoded.document});
		applied = result.success;
		m_Message = result.message;
	}
	else m_Message = decoded.message;
	if (!applied)
	{
		const auto restored = redo ? Vans::VansAssetDocumentEditService::Undo(asset->sourceDocument)
			: Vans::VansAssetDocumentEditService::Redo(asset->sourceDocument);
		if (!restored) m_Message += "; Rig history rollback failed: " + restored.message;
	}
	else
	{
		m_LimitDraft = {}; m_RotationDraft = {};
		m_LimitOriginalBone.clear(); m_RotationOriginalId.clear();
		m_LimitDraftDirty = m_RotationDraftDirty = false;
	}
	m_RigDocumentStateId = asset->sourceDocument.CurrentStateId();
	RefreshRigSnapshot(previewAPI);
}

bool VansSceneAnimationPreviewWindow::DrawBoneNameCombo(const char* label, std::string& name)
{
	bool changed = false;
	if (ImGui::BeginCombo(label, name.empty() ? "Choose bone..." : name.c_str()))
	{
		m_ConstraintBoneFilter.Draw();
		for (const auto& bone : m_Snapshot.bones)
			if (m_ConstraintBoneFilter.Matches(bone.name) && ImGui::Selectable(bone.name.c_str(), bone.name == name))
			{ name = bone.name; changed = true; }
		ImGui::EndCombo();
	}
	return changed;
}

void VansSceneAnimationPreviewWindow::DrawRigConstraintEditor(IEngineEditorAPI& api)
{
	IAnimationPreviewEditorAPI& previewAPI = api;
	auto working = previewAPI.GetAnimationPreviewWorkingRigDocument(m_SessionId);
	if (!working.success) return;
	if (ImGui::TreeNode("Joint Limits"))
	{
		if (ImGui::BeginCombo("Configured Limit", m_LimitOriginalBone.empty() ? "New limit" : m_LimitOriginalBone.c_str()))
		{
			for (const auto& limit : working.document.jointLimits)
				if (ImGui::Selectable(limit.bone.c_str(), m_LimitOriginalBone == limit.bone))
				{ m_LimitDraft = limit; m_LimitOriginalBone = limit.bone; m_LimitDraftDirty = false; }
			ImGui::EndCombo();
		}
		if (ImGui::Button("New Limit"))
		{ m_LimitDraft = {}; m_LimitOriginalBone.clear(); m_LimitDraftDirty = false; }
		m_LimitDraftDirty |= DrawBoneNameCombo("Limited Bone", m_LimitDraft.bone);
		int kind = static_cast<int>(m_LimitDraft.kind);
		if (ImGui::Combo("Limit Type", &kind, "Hinge\0Swing / Twist\0Locked\0"))
		{ m_LimitDraft.kind = static_cast<AnimationRigJointLimitKind>(kind); m_LimitDraftDirty = true; }
		if (m_LimitDraft.kind != AnimationRigJointLimitKind::Locked)
		{
			m_LimitDraftDirty |= ImGui::DragFloat3("Twist Axis (bind local)", &m_LimitDraft.axisLocal.x, 0.01f);
			m_LimitDraftDirty |= ImGui::DragFloat("Min Twist (deg)", &m_LimitDraft.minDegrees, 0.5f, -180, 180);
			m_LimitDraftDirty |= ImGui::DragFloat("Max Twist (deg)", &m_LimitDraft.maxDegrees, 0.5f, -180, 180);
		}
		if (m_LimitDraft.kind == AnimationRigJointLimitKind::SwingTwist)
		{
			m_LimitDraftDirty |= ImGui::DragFloat3("Swing Reference Axis", &m_LimitDraft.swingReferenceAxisLocal.x, 0.01f);
			m_LimitDraftDirty |= ImGui::DragFloat2("Swing Cone (deg)", &m_LimitDraft.swingLimitDegrees.x, 0.5f, 0, 180);
			ImGui::TextWrapped("Swing reference must be perpendicular to twist. Cone X uses the reference axis; Y uses twist cross reference.");
		}
		ImGui::TextWrapped("Angles are relative to the bone's bind-local rotation. Hinge permits one axis; Locked keeps bind rotation. Limits apply when a constraint writes the bone. Rotation Distribution blends toward the limited pose; full activation enforces the final limits.");
		if (ImGui::Button("Apply Limit"))
		{
			auto& limits = working.document.jointLimits;
			const bool collision = std::any_of(limits.begin(), limits.end(), [&](const auto& item)
				{ return item.bone == m_LimitDraft.bone && item.bone != m_LimitOriginalBone; });
			if (collision) m_Message = "This bone already has a limit; select it to edit";
			else
			{
				limits.erase(std::remove_if(limits.begin(), limits.end(), [&](const auto& item)
					{ return item.bone == m_LimitOriginalBone; }), limits.end());
				limits.push_back(m_LimitDraft);
				if (ApplyRigWorkingCopy(api, working.document))
				{ m_LimitOriginalBone = m_LimitDraft.bone; m_LimitDraftDirty = false; }
			}
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(m_LimitOriginalBone.empty());
		if (ImGui::Button("Remove Limit"))
		{
			auto& limits = working.document.jointLimits;
			limits.erase(std::remove_if(limits.begin(), limits.end(), [&](const auto& item)
				{ return item.bone == m_LimitOriginalBone; }), limits.end());
			if (ApplyRigWorkingCopy(api, working.document))
			{ m_LimitOriginalBone.clear(); m_LimitDraft = {}; m_LimitDraftDirty = false; }
		}
		ImGui::EndDisabled();
		if (m_LimitDraftDirty) ImGui::TextDisabled("Limit draft has unapplied edits");
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Rotation Distribution"))
	{
		if (ImGui::BeginCombo("Rig Profile", m_RotationOriginalId.empty() ? "New profile" : m_RotationOriginalId.c_str()))
		{
			for (const auto& profile : working.document.rotationDistributions)
				if (ImGui::Selectable(profile.id.c_str(), profile.id == m_RotationOriginalId))
				{ m_RotationDraft = profile; m_RotationOriginalId = profile.id; m_RotationDraftDirty = false; }
			ImGui::EndCombo();
		}
		if (ImGui::Button("New Rotation Profile"))
		{ m_RotationDraft = {}; m_RotationOriginalId.clear(); m_RotationDraftDirty = false; }
		m_RotationDraftDirty |= EditRigText("Profile ID", m_RotationDraft.id);
		if (ImGui::BeginCombo("Rotation Goal", m_RotationDraft.goal.c_str()))
		{
			for (const auto& goal : working.document.goals)
				if (ImGui::Selectable(goal.id.c_str(), goal.id == m_RotationDraft.goal))
				{ m_RotationDraft.goal = goal.id; m_RotationDraftDirty = true; }
			ImGui::EndCombo();
		}
		m_RotationDraftDirty |= DrawBoneNameCombo("Base Bone", m_RotationDraft.baseBone);
		m_RotationDraftDirty |= ImGui::SliderFloat("Base Twist Fraction", &m_RotationDraft.baseFraction, 0, 1);
		ImGui::TextWrapped("Base must directly parent the Goal effector. Rotation follows the base-to-effector axis, preserving the solved endpoint. Goal Rotation Weight controls activation.");
		for (std::size_t i = 0; i < m_RotationDraft.recipients.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i) + 98000);
			auto& recipient = m_RotationDraft.recipients[i];
			m_RotationDraftDirty |= DrawBoneNameCombo("Recipient", recipient.bone);
			m_RotationDraftDirty |= ImGui::SliderFloat("Total Twist Fraction", &recipient.fraction, 0, 1);
			if (ImGui::SmallButton("Remove Recipient"))
			{ m_RotationDraft.recipients.erase(m_RotationDraft.recipients.begin() + i); m_RotationDraftDirty = true; ImGui::PopID(); break; }
			ImGui::PopID();
		}
		if (ImGui::Button("Add Recipient")) { m_RotationDraft.recipients.push_back({}); m_RotationDraftDirty = true; }
		ImGui::TextWrapped("Recipients are auxiliary descendants outside the effector subtree. Each fraction is a total rotation from the base input frame; fractions do not sum to 1. Active profiles drive helpers from their bind-relative orientation.");
		if (ImGui::Button("Apply Rotation Profile"))
		{
			auto& profiles = working.document.rotationDistributions;
			const bool collision = std::any_of(profiles.begin(), profiles.end(), [&](const auto& item)
				{ return item.id == m_RotationDraft.id && item.id != m_RotationOriginalId; });
			if (collision) m_Message = "This profile ID already exists; select it to edit";
			else
			{
				profiles.erase(std::remove_if(profiles.begin(), profiles.end(), [&](const auto& item)
					{ return item.id == m_RotationOriginalId; }), profiles.end());
				profiles.push_back(m_RotationDraft);
				if (ApplyRigWorkingCopy(api, working.document))
				{ m_RotationOriginalId = m_RotationDraft.id; m_RotationDraftDirty = false; }
			}
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(m_RotationOriginalId.empty());
		if (ImGui::Button("Remove Rotation Profile"))
		{
			auto& profiles = working.document.rotationDistributions;
			profiles.erase(std::remove_if(profiles.begin(), profiles.end(), [&](const auto& item)
				{ return item.id == m_RotationOriginalId; }), profiles.end());
			if (ApplyRigWorkingCopy(api, working.document))
			{ m_RotationOriginalId.clear(); m_RotationDraft = {}; m_RotationDraftDirty = false; }
		}
		ImGui::EndDisabled();
		ImGui::BeginDisabled(m_RotationOriginalId.empty() || m_RotationDraftDirty);
		if (ImGui::Button("Add Rotation Node to Graph")) AddRotationDistributionNode();
		ImGui::EndDisabled();
		ImGui::TextWrapped("Use position IK followed by this node. Set the corresponding Limb IK Tip Rotation to Preserve Input so this node owns orientation. Apply IK Graph to preview.");
		if (m_RotationDraftDirty) ImGui::TextDisabled("Rotation profile draft has unapplied edits");
		ImGui::TreePop();
	}
	auto asset = Vans::VansAssetDocumentRegistry::Get().Find(m_RigSnapshot.rigAssetPath);
	if (asset)
	{
		ImGui::BeginDisabled(!Vans::VansAssetDocumentEditService::CanUndo(asset->sourceDocument));
		if (ImGui::Button("Undo Rig Edit")) ChangeRigHistory(api, false);
		ImGui::EndDisabled(); ImGui::SameLine();
		ImGui::BeginDisabled(!Vans::VansAssetDocumentEditService::CanRedo(asset->sourceDocument));
		if (ImGui::Button("Redo Rig Edit")) ChangeRigHistory(api, true);
		ImGui::EndDisabled();
	}
}
}
