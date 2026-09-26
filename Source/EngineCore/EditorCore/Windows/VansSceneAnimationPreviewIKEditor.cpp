#include "VansSceneAnimationPreviewWindow.h"
#include "../../EngineAPILayer/Public/IAnimationEditorAPI.h"
#include "../../EngineAPILayer/Public/IAnimationPreviewEditorAPI.h"
#include "../../EngineAPILayer/Public/IAssetEditorAPI.h"
#include "../VansEditorWindow.h"
#include "../VansSceneEditService.h"
#include "../VansSceneEntityCreationService.h"
#include "../../AuthoringCore/VansAssetDocumentRegistry.h"
#include "../../AuthoringCore/VansAssetDocumentEditService.h"
#include "../VansEditorAssetSaveService.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include <imgui.h>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>

namespace VansGraphics
{
namespace
{
using namespace Vans::EditorAPI;
bool EditText(const char* label, std::string& value)
{
	char buffer[512]{};
	std::memcpy(buffer, value.data(), std::min(value.size(), sizeof(buffer)-1));
	if (!ImGui::InputText(label, buffer, sizeof(buffer))) return false;
	value = buffer;
	return true;
}
AnimationGraphDTO* TargetGraph(AnimatorDocumentDTO* document)
{
	if (document) for (auto& graph : document->graphs)
		if (graph.role == AnimatorGraphRole::TargetPostProcess) return graph.graph.get();
	return nullptr;
}
std::string BindingProperty(const nlohmann::json& root, const std::string& entity, const std::string& component)
{
	const auto& entities = root.at("entities");
	for (std::size_t i=0; i<entities.size(); ++i)
		if (entities[i].value("id", "") == entity)
			for (std::size_t j=0; j<entities[i].at("components").size(); ++j)
				if (entities[i]["components"][j].value("id", "") == component)
					return "/entities/"+std::to_string(i)+"/components/"+std::to_string(j)+"/data/targetBindings";
	return {};
}
bool InsertBefore(AnimationGraphDTO& graph, int successor, std::unique_ptr<AnimationNodeDTO> node)
{
	auto link = std::find_if(graph.links.begin(), graph.links.end(),
		[&](const auto& item){ return item.toNodeId == successor && item.toPinIndex == 0; });
	if (link == graph.links.end()) return false;
	const int previous = link->fromNodeId, pin = link->fromPinIndex, linkId = link->linkId;
	const int id = graph.AddNode(std::move(node));
	graph.RemoveLink(linkId);
	graph.AddLink(previous, pin, id, 0);
	graph.AddLink(id, 0, successor, 0);
	return true;
}
}

void VansSceneAnimationPreviewWindow::AddRotationDistributionNode()
{
	auto* graph = TargetGraph(m_AnimatorDocument.get());
	if (!graph) { m_Message = "Animator has no Target Post Process graph"; return; }
	for (const auto& [id, node] : graph->nodes)
		if (node->GetType() == VansAnimGraphNodeType::RotationDistribution && node->m_RotationProfileId == m_RotationOriginalId)
		{ m_Message = "This rotation profile already has a graph node"; return; }
	auto node = AnimationGraphDTO::CreateNodeByType(VansAnimGraphNodeType::RotationDistribution);
	node->m_RotationProfileId = m_RotationOriginalId;
	if (InsertBefore(*graph, graph->outputNodeId, std::move(node))) m_AnimatorDraftDirty = true;
	else m_Message = "Connect the Target Post Process output before adding a rotation node";
}

bool VansSceneAnimationPreviewWindow::ApplyAnimatorWorkingCopy(IEngineEditorAPI& api, bool recordEdit)
{
	IAnimationEditorAPI& animationAPI = api;
	IAnimationPreviewEditorAPI& previewAPI = api;
	IAssetEditorAPI& assetAPI = api;
	if (!m_AnimatorDocument) return false;
	auto sharedDocument = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(assetAPI.ResolveAssetGuid(m_SelectedAnimatorGuid).sourcePath);
	if (!sharedDocument || sharedDocument->sourceDocument.CurrentStateId() != m_AnimatorDocumentStateId)
	{ m_Message = "Animator was edited elsewhere; reload its working copy before applying"; return false; }
	const auto encoded = animationAPI.EncodeAnimatorDocument(*m_AnimatorDocument);
	if (!encoded.success) { m_Message = encoded.message; return false; }
	const auto snapshot = previewAPI.GetAnimationPreviewSnapshot(m_SessionId);
	AnimationPreviewDefinitionUpdate update;
	update.sessionId = m_SessionId;
	update.revision = snapshot.requestedRevision + 1;
	update.canonicalJson = encoded.canonicalJson;
	const auto applied = previewAPI.UpdateAnimationPreviewDefinition(update);
	if (!applied.success) { m_Message = applied.message; return false; }
	if (recordEdit)
	{
		const auto resolved = assetAPI.ResolveAssetGuid(m_SelectedAnimatorGuid);
		auto document = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(resolved.sourcePath);
		if (!document) { m_Message = "Animator document is unavailable"; return false; }
		const auto edit = Vans::VansAssetDocumentEditService::ReplaceRoot(document->sourceDocument,
			Vans::DecodeSerializedValueJson(nlohmann::json::parse(encoded.canonicalJson)));
		if (!edit && edit.message != "Asset property is unchanged") { m_Message = edit.message; return false; }
	}
	m_AnimatorDocumentStateId = sharedDocument->sourceDocument.CurrentStateId();
	m_AnimatorDraftDirty = false;
	m_Message = "Animator working copy applied; use Save Animation Setup to publish";
	return true;
}

bool VansSceneAnimationPreviewWindow::ApplyTargetBindingsToDocument(
	IAnimationPreviewEditorAPI& previewAPI)
{
	auto* scene = VansEditorWindow::GetSceneDocument();
	auto* edits = VansEditorWindow::GetSceneEditService();
	if (!scene || !edits) return false;
	const auto root = Vans::EncodeSerializedValueJson<nlohmann::json>(scene->SerializedRootSnapshot());
	const auto pointer = BindingProperty(root, m_SelectedEntityGuid, m_SelectedAnimationComponentGuid);
	if (pointer.empty()) { m_Message = "Scene Animation Component was removed"; return false; }
	const auto before = m_RigSnapshot.targetBindings;
	const auto applied = previewAPI.SetAnimationPreviewTargetBindings({m_SessionId, m_RigSnapshot.bindingRevision, m_TargetBindings});
	if (!applied.success) { m_Message = applied.message; return false; }
	nlohmann::json bindings = nlohmann::json::array();
	for (const auto& binding : m_TargetBindings)
		bindings.push_back({{"id", binding.id}, {"target", {{"domain", "SceneEntity"},
			{"guid", binding.targetEntityGuid}, {"entityGuid", binding.targetEntityGuid}}}});
	const auto edited = edits->Set({Vans::DocumentPropertySpace::Scene, pointer}, Vans::DecodeSerializedValueJson(bindings));
	if (!edited && edited.message != "Scene property is unchanged")
	{
		previewAPI.SetAnimationPreviewTargetBindings({m_SessionId, applied.acceptedRevision, before});
		m_Message = edited.message;
		RefreshRigSnapshot(previewAPI);
		return false;
	}
	RefreshRigSnapshot(previewAPI);
	m_Message = "IK bindings applied to Scene working copy";
	return true;
}

void VansSceneAnimationPreviewWindow::ReloadSceneTargetEdits(
	IAnimationPreviewEditorAPI& previewAPI)
{
	auto* scene = VansEditorWindow::GetSceneDocument();
	if (!scene) return;
	const auto root = Vans::EncodeSerializedValueJson<nlohmann::json>(scene->SerializedRootSnapshot());
	const auto pointer = BindingProperty(root, m_SelectedEntityGuid, m_SelectedAnimationComponentGuid);
	m_TargetBindings.clear();
	if (!pointer.empty() && root.contains(nlohmann::json::json_pointer(pointer)))
		for (const auto& binding : root.at(nlohmann::json::json_pointer(pointer)))
			m_TargetBindings.push_back({binding.at("id").get<std::string>(), binding.at("target").at("entityGuid").get<std::string>(), {}});
	previewAPI.SetAnimationPreviewTargetBindings({m_SessionId, m_RigSnapshot.bindingRevision, m_TargetBindings});
	RefreshRigSnapshot(previewAPI);
	for (const auto& entity : root.at("entities"))
	{
		const auto guid = entity.value("id", "");
		if (std::find(m_AppliedTargetTransforms.begin(), m_AppliedTargetTransforms.end(), guid) == m_AppliedTargetTransforms.end()) continue;
		for (const auto& component : entity.at("components"))
			if (component.value("type", "") == "Transform")
			{
				const auto& data = component.at("data");
				RuntimeTransformSnapshot transform;
				transform.available = true;
				transform.entityGuid = guid;
				transform.position = {data["position"][0], data["position"][1], data["position"][2]};
				transform.scale = {data["scale"][0], data["scale"][1], data["scale"][2]};
				const glm::quat rotation(data["rotation"][3].get<float>(), data["rotation"][0].get<float>(),
					data["rotation"][1].get<float>(), data["rotation"][2].get<float>());
				const auto degrees = glm::degrees(glm::eulerAngles(rotation));
				transform.rotationDegrees = {degrees.x,degrees.y,degrees.z};
				previewAPI.SetAnimationPreviewAttachmentTransform({m_SessionId, m_RigSnapshot.attachmentRevision, guid,
					RuntimeTransformSpace::Local, transform});
				RefreshRigSnapshot(previewAPI);
			}
	}
}

bool VansSceneAnimationPreviewWindow::SaveAnimationSetup(IEngineEditorAPI& api)
{
	IAnimationEditorAPI& animationAPI = api;
	IAnimationPreviewEditorAPI& previewAPI = api;
	IAssetEditorAPI& assetAPI = api;
	if (m_LimitDraftDirty || m_RotationDraftDirty)
	{ m_Message = "Apply or discard the Limit / Rotation Profile draft before saving"; return false; }
	if (!ApplyAnimatorWorkingCopy(api, true) || !ApplyTargetBindingsToDocument(previewAPI)) return false;
	auto* scene = VansEditorWindow::GetSceneDocument();
	auto* edits = VansEditorWindow::GetSceneEditService();
	if (!scene || !edits) return false;
	// 仅普通 Transform 节点写回 Scene；临时武器挂接仍由会话恢复。
	for (const auto& attachment : m_RigSnapshot.attachments)
		if (attachment.dirty && !attachment.temporary &&
			(attachment.parent.kind == RuntimeParentKind::Entity || attachment.parent.kind == RuntimeParentKind::None))
		{
			const auto changed = edits->SetEntityTransform(attachment.entityGuid, attachment.localTransform);
			if (!changed && changed.message != "Scene entity transform is unchanged") { m_Message = changed.message; return false; }
			m_AppliedTargetTransforms.push_back(attachment.entityGuid);
		}
	auto workingRig = previewAPI.GetAnimationPreviewWorkingRigDocument(m_SessionId);
	if (!workingRig.success) { m_Message = workingRig.message; return false; }
	const auto rigJson = animationAPI.EncodeAnimationRigDocument(workingRig.document);
	if (!rigJson.success) { m_Message = rigJson.message; return false; }
	auto rig = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(m_RigSnapshot.rigAssetPath);
	auto animator = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(assetAPI.ResolveAssetGuid(m_SelectedAnimatorGuid).sourcePath);
	if (!rig || !animator) return false;
	if (rig->sourceDocument.CurrentStateId() != m_RigDocumentStateId)
	{m_Message="Rig was edited elsewhere; restart preview before saving";return false;}
	const auto edited = Vans::VansAssetDocumentEditService::ReplaceRoot(rig->sourceDocument,
		Vans::DecodeSerializedValueJson(nlohmann::json::parse(rigJson.canonicalJson)));
	if (!edited && edited.message != "Asset property is unchanged") { m_Message = edited.message; return false; }
	m_RigDocumentStateId = rig->sourceDocument.CurrentStateId();
	const auto saved = Vans::VansEditorAssetSaveService::Get().SaveSceneAndAssets(api, *scene, {animator, rig});
	if (!saved) { m_Message = saved.message; return false; }
	if (!previewAPI.AdoptAnimationPreviewRig({m_SessionId, m_RigSnapshot.rigRevision}).success ||
		!previewAPI.AdoptAnimationPreviewSceneChanges({
			m_SessionId, m_RigSnapshot.attachmentRevision,
			m_AppliedTargetTransforms }))
	{ m_Message = "Files saved; preview baseline adoption failed"; return false; }
	m_Message = "Scene, Animator and Rig saved";
	RefreshRigSnapshot(previewAPI);
	return true;
}

bool VansSceneAnimationPreviewWindow::DrawTransformIKEditor(IEngineEditorAPI& api)
{
	IAssetEditorAPI& assetAPI = api;
	IAnimationPreviewEditorAPI& previewAPI = api;
	if (!ImGui::CollapsingHeader("Transform Target IK", ImGuiTreeNodeFlags_DefaultOpen)) return false;
	auto* scene = VansEditorWindow::GetSceneDocument();
	auto* sceneEdits = VansEditorWindow::GetSceneEditService();
	auto* graph = TargetGraph(m_AnimatorDocument.get());
	bool inlineAnchorVisible = false;
	std::vector<int> visibleGoals;
	const auto drawGoal = [&](int id, AnimationGoalDefinitionDTO& goal)
	{
		ImGui::PushID(id+81000);
		ImGui::SeparatorText(goal.goalId.c_str());
		m_AnimatorDraftDirty |= EditText("Target Binding",goal.binding);
		m_AnimatorDraftDirty |= ImGui::SliderFloat("Position Weight",&goal.fixedPositionWeight,0,1);
		m_AnimatorDraftDirty |= ImGui::SliderFloat("Rotation Weight",&goal.fixedRotationWeight,0,1);
		if (ImGui::BeginCombo("Weight Parameter",goal.weightParameter.empty()?"Constant":goal.weightParameter.c_str()))
		{
			if (ImGui::Selectable("Constant",goal.weightParameter.empty())) {goal.weightParameter.clear();m_AnimatorDraftDirty=true;}
			for (const auto& parameter : m_AnimatorDocument->parameters)
				if (parameter.type == AnimatorParamType::Float && ImGui::Selectable(parameter.name.c_str(),goal.weightParameter==parameter.name))
				{goal.weightParameter=parameter.name;m_AnimatorDraftDirty=true;}
			ImGui::EndCombo();
		}
		ImGui::PopID();
	};
	ImGui::TextWrapped("Choose a target, then Edit Position or Edit Rotation. The selected anchor's pose and IK weights appear here.");
	for (std::size_t i=0; i<m_TargetBindings.size(); ++i)
	{
		ImGui::PushID(static_cast<int>(i)+72000);
		auto& binding = m_TargetBindings[i];
		EditText("Binding", binding.id);
		DrawSceneEntityCombo(api, "Target Object", binding.targetEntityGuid, m_TargetObjectFilter);
		ImGui::BeginDisabled(binding.targetEntityGuid.empty());
		const bool editPosition = ImGui::SmallButton("Edit Position");
		ImGui::SameLine();
		const bool editRotation = ImGui::SmallButton("Edit Rotation");
		ImGui::EndDisabled();
		if (editPosition || editRotation)
		{
			// 选择仅更新预览绑定；Scene 文档仍由显式 Apply/Save 写入。
			const auto applied = previewAPI.SetAnimationPreviewTargetBindings({m_SessionId, m_RigSnapshot.bindingRevision, m_TargetBindings});
			m_Message = applied.message;
			if (applied.success)
			{
				RefreshRigSnapshot(api);
				m_SelectedAttachmentGuid = binding.targetEntityGuid;
				m_TransformTarget = TransformTarget::Attachment;
				m_GizmoOperation = editRotation ? GizmoOperation::Rotate : GizmoOperation::Translate;
			}
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Remove Binding"))
		{ m_TargetBindings.erase(m_TargetBindings.begin()+i); ImGui::PopID(); break; }
		const auto status = std::find_if(m_RigSnapshot.targetBindings.begin(), m_RigSnapshot.targetBindings.end(),
			[&](const auto& item){return item.id == binding.id;});
		if (status != m_RigSnapshot.targetBindings.end() && !status->diagnostic.empty())
			ImGui::TextWrapped("%s", status->diagnostic.c_str());
		if (!inlineAnchorVisible && m_TransformTarget == TransformTarget::Attachment
			&& !binding.targetEntityGuid.empty() && m_SelectedAttachmentGuid == binding.targetEntityGuid)
		{
			ImGui::SeparatorText("Anchor Pose");
			inlineAnchorVisible = DrawAttachmentPoseEditor(api);
			if (inlineAnchorVisible && graph)
			{
				for (auto& [id, node] : graph->nodes)
					if (node->GetType() == VansAnimGraphNodeType::Goal && node->m_Goal.binding == binding.id)
					{
						drawGoal(id, node->m_Goal);
						visibleGoals.push_back(id);
					}
				if (!visibleGoals.empty())
				{
					ImGui::TextWrapped("Rotation Weight: 0 keeps animation orientation; 1 follows the anchor. Weight Parameter controls the overall IK influence.");
					if (ImGui::Button("Apply IK Weights")) ApplyAnimatorWorkingCopy(api, true);
					if (m_AnimatorDraftDirty) ImGui::TextDisabled("IK settings have unapplied edits.");
				}
			}
			if (inlineAnchorVisible && ImGui::Button("Save Animation Setup")) SaveAnimationSetup(api);
			if (!inlineAnchorVisible) ImGui::TextWrapped("The target is unavailable for editing. Check the binding diagnostic above.");
		}
		ImGui::PopID();
	}
	if (ImGui::Button("Add Binding")) m_TargetBindings.push_back({"target"+std::to_string(m_TargetBindings.size()+1),m_SelectedSceneEntityGuid,{}});
	ImGui::SameLine();
	if (ImGui::Button("Apply Bindings")) ApplyTargetBindingsToDocument(api);
	ImGui::BeginDisabled(!scene || !sceneEdits || m_SelectedAttachmentGuid.empty());
	if (ImGui::Button("Create Empty Anchor under Selected Object"))
	{
		Vans::VansSceneParentReference parent;
		parent.kind = Vans::VansSceneParentKind::Entity;
		Vans::VansAssetGuid::TryParse(m_SelectedAttachmentGuid, parent.entityGuid);
		const auto created = VansSceneEntityCreationService::CreateEmptyObject(api, *scene, *sceneEdits, {parent,"IK Anchor"});
		m_Message = created.message;
		if (created)
		{
			m_SelectedAttachmentGuid = created.entityGuid;
			m_SelectedSceneEntityGuid = created.entityGuid;
			m_TransformTarget = TransformTarget::Attachment;
			m_SceneEntities = previewAPI.QueryAnimationPreviewSceneEntities(m_SessionId);
			RefreshRigSnapshot(api);
		}
	}
	ImGui::EndDisabled();
	for (const auto& diagnostic : m_RigSnapshot.constraintDiagnostics) ImGui::TextWrapped("%s",diagnostic.c_str());
	DrawRigConstraintEditor(api);
	if (graph)
	{

		for (auto& [id,node] : graph->nodes)
			if (node->GetType() == VansAnimGraphNodeType::Goal
				&& std::find(visibleGoals.begin(), visibleGoals.end(), id) == visibleGoals.end())
			{
				drawGoal(id, node->m_Goal);
			}
		auto rig = previewAPI.GetAnimationPreviewWorkingRigDocument(m_SessionId);
		if (rig.success && ImGui::BeginCombo("IK Chain",m_NewChainId.empty()?"Choose chain...":m_NewChainId.c_str()))
		{
			for (const auto& chain : rig.document.chains)
				if (chain.solver != AnimationRigSolverKind::Aim && ImGui::Selectable(chain.id.c_str(),chain.id==m_NewChainId)) m_NewChainId=chain.id;
			ImGui::EndCombo();
		}
		if (ImGui::TreeNode("Define Bone Chain"))
		{
			if (ImGui::Button("Load Selected Chain") && rig.success)
				for (const auto& chain : rig.document.chains) if (chain.id==m_NewChainId) m_ChainDraft=chain;
			EditText("Chain ID",m_ChainDraft.id);
			EditText("Goal ID",m_ChainDraft.goal);
			int solver=static_cast<int>(m_ChainDraft.solver);
			if(ImGui::Combo("Solver",&solver,"Limb (3 bones)\0CCD\0FABRIK\0"))m_ChainDraft.solver=static_cast<AnimationRigSolverKind>(solver);
			for(std::size_t i=0;i<m_ChainDraft.bones.size();++i)
			{
				ImGui::PushID(static_cast<int>(i)+93000);
				auto& name=m_ChainDraft.bones[i];
				if(ImGui::BeginCombo("Bone",name.empty()?"Choose bone...":name.c_str()))
				{
					for(const auto& bone:m_Snapshot.bones)if(ImGui::Selectable(bone.name.c_str(),bone.name==name))name=bone.name;
					ImGui::EndCombo();
				}
				ImGui::SameLine();
				if(ImGui::SmallButton("Remove")){m_ChainDraft.bones.erase(m_ChainDraft.bones.begin()+i);ImGui::PopID();break;}
				ImGui::PopID();
			}
			if(ImGui::Button("Add Bone"))m_ChainDraft.bones.push_back("");
			ImGui::DragFloat3("Bend Direction",&m_ChainDraft.poleAxisLocal.x,0.01f);
			auto rigAsset = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(m_RigSnapshot.rigAssetPath);
			const bool rigDocumentCurrent = rigAsset && rigAsset->sourceDocument.CurrentStateId() == m_RigDocumentStateId;
			ImGui::BeginDisabled(!rigDocumentCurrent);
			if(ImGui::Button("Apply Chain to Rig") && rig.success && !m_ChainDraft.bones.empty())
			{
				auto& document=rig.document;
				auto chain=std::find_if(document.chains.begin(),document.chains.end(),[&](const auto& c){return c.id==m_ChainDraft.id;});
				if(chain==document.chains.end())document.chains.push_back(m_ChainDraft);else *chain=m_ChainDraft;
				auto goal=std::find_if(document.goals.begin(),document.goals.end(),[&](const auto& g){return g.id==m_ChainDraft.goal;});
				AnimationRigGoalDTO value{m_ChainDraft.goal,m_ChainDraft.bones.back()};
				if(goal==document.goals.end())document.goals.push_back(value);else *goal=value;
				if (ApplyRigWorkingCopy(api, document)) m_NewChainId = m_ChainDraft.id;
			}
			ImGui::EndDisabled();
			if (!rigDocumentCurrent) ImGui::TextWrapped("Rig was edited elsewhere; restart preview to reload it.");
			ImGui::TreePop();
		}
		if (ImGui::TreeNode("IK Rotation Ownership"))
		{
			for (auto& [id, node] : graph->nodes)
			{
				ImGui::PushID(id + 99000);
				if (node->GetType() == VansAnimGraphNodeType::LimbIK)
				{
					ImGui::Text("%s", node->m_Name.c_str());
					for (const auto& chain : node->m_ChainIds) ImGui::TextDisabled("%s", chain.c_str());
					int mode = static_cast<int>(node->m_LimbSettings.tipRotationMode);
					if (ImGui::Combo("Tip Rotation", &mode, "Preserve Input\0Match Goal\0Follow Chain\0"))
					{ node->m_LimbSettings.tipRotationMode = static_cast<AnimationLimbTipRotationMode>(mode); m_AnimatorDraftDirty = true; }
				}
				if (node->GetType() == VansAnimGraphNodeType::RotationDistribution)
				{
					m_AnimatorDraftDirty |= EditText("Rotation Profile", node->m_RotationProfileId);
					if (ImGui::SmallButton("Remove Rotation Node"))
					{
						int from = -1, fromPin = 0, to = -1, toPin = 0;
						int inputs = 0, outputs = 0;
						for (const auto& link : graph->links)
						{
							if (link.toNodeId == id) { from = link.fromNodeId; fromPin = link.fromPinIndex; ++inputs; }
							if (link.fromNodeId == id) { to = link.toNodeId; toPin = link.toPinIndex; ++outputs; }
						}
						if (inputs == 1 && outputs == 1)
						{ graph->RemoveNode(id); graph->AddLink(from, fromPin, to, toPin); m_AnimatorDraftDirty = true; }
						else m_Message = "Use Animation Graph editor to remove a branched or disconnected node";
						ImGui::PopID(); break;
					}
				}
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
		if (ImGui::TreeNode("Pose Checkpoints"))
		{
			for(auto& [id,node]:graph->nodes) if(node->GetType()==VansAnimGraphNodeType::PoseCheckpoint)
			{
				ImGui::PushID(id+95000);
				m_AnimatorDraftDirty|=EditText("Checkpoint ID",node->m_CheckpointId);
				for(auto& name:node->m_CheckpointBones)ImGui::Text("Captured bone: %s",name.c_str());
				ImGui::PopID();
			}
			EditText("New Checkpoint ID",m_CheckpointDraftId);
			if(ImGui::BeginCombo("Capture Bone",m_CheckpointDraftBone.empty()?"Choose bone...":m_CheckpointDraftBone.c_str()))
			{
				for(const auto& bone:m_Snapshot.bones)if(ImGui::Selectable(bone.name.c_str(),bone.name==m_CheckpointDraftBone))m_CheckpointDraftBone=bone.name;
				ImGui::EndCombo();
			}
			if(ImGui::Button("Add Checkpoint Before Final IK") && !m_CheckpointDraftBone.empty())
			{
				int successor=graph->outputNodeId;
				for(const auto& link:graph->links)if(link.toNodeId==graph->outputNodeId)
				{
					const auto* previous=graph->GetNode(link.fromNodeId);
					if(previous && (previous->GetType()==VansAnimGraphNodeType::LimbIK || previous->GetType()==VansAnimGraphNodeType::ChainIK))successor=previous->GetNodeId();
				}
				auto checkpoint=AnimationGraphDTO::CreateNodeByType(VansAnimGraphNodeType::PoseCheckpoint);
				checkpoint->m_CheckpointId=m_CheckpointDraftId;checkpoint->m_CheckpointBones={m_CheckpointDraftBone};
				if(InsertBefore(*graph,successor,std::move(checkpoint)))m_AnimatorDraftDirty=true;
			}
			ImGui::TreePop();
		}
		EditText("New IK Binding",m_NewBindingId);
		if (ImGui::Button("Add IK for Chain") && rig.success)
		{
			const auto chain = std::find_if(rig.document.chains.begin(),rig.document.chains.end(),[&](const auto& c){return c.id==m_NewChainId;});
			bool used = false;
			for (const auto& [id,node] : graph->nodes)
				if (std::find(node->m_ChainIds.begin(),node->m_ChainIds.end(),m_NewChainId)!=node->m_ChainIds.end()) used=true;
			if (chain==rig.document.chains.end() || m_NewBindingId.empty() || used) m_Message="Choose an unused IK chain and a binding ID";
			else
			{
				auto candidate = AnimationGraphDTO::Clone(*graph);
				int first=-1;
				for (const auto& [id,node] : candidate->nodes)
					if (node->GetType()==VansAnimGraphNodeType::TargetPoseInput)
						for (const auto& link:candidate->links) if(link.fromNodeId==id) first=link.toNodeId;
				auto goal=AnimationGraphDTO::CreateNodeByType(VansAnimGraphNodeType::Goal);
				goal->m_Goal.goalId=chain->goal; goal->m_Goal.binding=m_NewBindingId;
				goal->m_Goal.fixedRotationWeight=1;
				auto solver=AnimationGraphDTO::CreateNodeByType(chain->solver==AnimationRigSolverKind::Limb?VansAnimGraphNodeType::LimbIK:VansAnimGraphNodeType::ChainIK);
				solver->m_ChainIds={chain->id};
				if (InsertBefore(*candidate,first,std::move(goal)) && InsertBefore(*candidate,candidate->outputNodeId,std::move(solver)))
				{ *graph=std::move(*candidate);m_AnimatorDraftDirty=true; }
				else m_Message="Target graph must have a connected input and output";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Apply IK Graph")) ApplyAnimatorWorkingCopy(api,true);
		if (m_AnimatorDraftDirty) ImGui::TextDisabled("IK graph has unapplied edits");
	}
	ImGui::Separator();
	if (ImGui::Button("Save Animation Setup")) SaveAnimationSetup(api);
	ImGui::SameLine();
	if (ImGui::Button("Reload Animator Working Copy") && LoadAnimatorDocument(api)) ApplyAnimatorWorkingCopy(api,false);
	ImGui::TextDisabled("Saves the current Scene, selected Animator and target Rig working copies.");
	if (sceneEdits)
	{
		ImGui::BeginDisabled(!sceneEdits->CanUndo());
		if (ImGui::Button("Undo Scene Edit")) {m_Message=sceneEdits->Undo().message;ReloadSceneTargetEdits(api);}
		ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!sceneEdits->CanRedo());
		if (ImGui::Button("Redo Scene Edit")) {m_Message=sceneEdits->Redo().message;ReloadSceneTargetEdits(api);}
		ImGui::EndDisabled();
	}
	auto document=Vans::VansAssetDocumentRegistry::Get().Find(assetAPI.ResolveAssetGuid(m_SelectedAnimatorGuid).sourcePath);
	if(document)
	{
		bool reload=false;
		ImGui::BeginDisabled(!Vans::VansAssetDocumentEditService::CanUndo(document->sourceDocument));
		if(ImGui::Button("Undo Animator Edit")) reload=bool(Vans::VansAssetDocumentEditService::Undo(document->sourceDocument));
		ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!Vans::VansAssetDocumentEditService::CanRedo(document->sourceDocument));
		if(ImGui::Button("Redo Animator Edit")) reload=bool(Vans::VansAssetDocumentEditService::Redo(document->sourceDocument));
		ImGui::EndDisabled();
		if(reload && LoadAnimatorDocument(api)) ApplyAnimatorWorkingCopy(api,false);
	}
	return inlineAnchorVisible;
}
}
