#include "VansSceneAnimationPreviewWindow.h"

#include "../Animation/VansAnimationRigSaveService.h"
#include "../VansAssetDocumentEditService.h"
#include "../VansAssetDocumentRegistry.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../RenderCore/VansCamera.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace VansGraphics
{
	namespace
	{
		glm::mat4 BuildTransformMatrix(
			const Vans::EditorAPI::RuntimeTransformSnapshot& transform)
		{
			glm::mat4 matrix(1.0f);
			matrix = glm::translate(matrix, glm::vec3(
				transform.position.x, transform.position.y, transform.position.z));
			matrix = glm::rotate(matrix, glm::radians(transform.rotationDegrees.z),
				glm::vec3(0.0f, 0.0f, 1.0f));
			matrix = glm::rotate(matrix, glm::radians(transform.rotationDegrees.y),
				glm::vec3(0.0f, 1.0f, 0.0f));
			matrix = glm::rotate(matrix, glm::radians(transform.rotationDegrees.x),
				glm::vec3(1.0f, 0.0f, 0.0f));
			return glm::scale(matrix, glm::vec3(
				transform.scale.x, transform.scale.y, transform.scale.z));
		}

		bool IsUsableTransform(
			const Vans::EditorAPI::RuntimeTransformSnapshot& transform)
		{
			const auto finite = [](const Vans::EditorAPI::Vec3& value)
			{
				return std::isfinite(value.x) && std::isfinite(value.y)
					&& std::isfinite(value.z);
			};
			return transform.available && finite(transform.position)
				&& finite(transform.rotationDegrees) && finite(transform.scale);
		}

		bool EditTransform(
			const char* id,
			Vans::EditorAPI::RuntimeTransformSnapshot& transform)
		{
			ImGui::PushID(id);
			float position[3] = {
				transform.position.x, transform.position.y, transform.position.z };
			float rotation[3] = {
				transform.rotationDegrees.x, transform.rotationDegrees.y,
				transform.rotationDegrees.z };
			float scale[3] = { transform.scale.x, transform.scale.y, transform.scale.z };
			bool changed = ImGui::DragFloat3("Position", position, 0.005f, 0.0f, 0.0f, "%.4f");
			changed = ImGui::DragFloat3("Rotation", rotation, 0.25f, 0.0f, 0.0f,
				"%.2f deg") || changed;
			changed = ImGui::DragFloat3("Scale", scale, 0.005f, 0.001f, 1000.0f,
				"%.4f") || changed;
			if (changed)
			{
				transform.position = { position[0], position[1], position[2] };
				transform.rotationDegrees = { rotation[0], rotation[1], rotation[2] };
				transform.scale = { scale[0], scale[1], scale[2] };
			}
			ImGui::PopID();
			return changed;
		}
	}

	VansSceneAnimationPreviewWindow::~VansSceneAnimationPreviewWindow()
	{
		StopPreview();
	}

	void VansSceneAnimationPreviewWindow::SetOpen(bool open)
	{
		m_IsOpen = open;
		if (!m_IsOpen)
			StopPreview();
	}

	void VansSceneAnimationPreviewWindow::StopPreview()
	{
		if (m_SessionId != 0 && m_ActiveAPI)
			m_ActiveAPI->DestroyAnimationPreview(m_SessionId);
		m_SessionId = 0;
		m_Snapshot = {};
		m_RigSnapshot = {};
		m_SceneEntities.clear();
		m_SelectedSocketGuid.clear();
		m_SelectedBindAnchorGuid.clear();
		m_SelectedBindAnchorLabel = "Choose Bone or Socket...";
		m_SelectedBindParentKind = Vans::EditorAPI::RuntimeParentKind::None;
		m_SelectedAttachmentGuid.clear();
		m_TransformTarget = TransformTarget::None;
	}

	bool VansSceneAnimationPreviewWindow::LoadAnimatorDocument(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		const auto resolved = editorAPI.ResolveAssetGuid(m_SelectedAnimatorGuid);
		if (!resolved.found || resolved.sourcePath.empty())
		{
			m_Message = "Selected Animator asset could not be resolved";
			return false;
		}
		auto document = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(
			resolved.sourcePath);
		if (!document || !document->sourceDocument.IsLoaded())
		{
			m_Message = document ? document->lastError
				: "Selected Animator document could not be opened";
			return false;
		}
		if (document->IsDirty())
		{
			m_Message = "Save the Animator document before starting a Scene preview";
			return false;
		}
		const nlohmann::json root = Vans::EncodeSerializedValueJson<nlohmann::json>(
			document->sourceDocument.SerializedRootSnapshot());
		auto decoded = editorAPI.DecodeAnimatorDocument(root.dump());
		if (!decoded.success || !decoded.document)
		{
			m_Message = decoded.message;
			return false;
		}
		m_AnimatorDocument = std::move(decoded.document);
		ResetParameterValues();
		m_SelectedGraphSetId = m_AnimatorDocument->defaultGraphSetId;
		m_SelectedSlotId.clear();
		m_SelectedClipName.clear();
		return true;
	}

	void VansSceneAnimationPreviewWindow::ResetParameterValues()
	{
		m_FloatParameters.clear();
		m_BoolParameters.clear();
		m_IntParameters.clear();
		m_VectorParameters.clear();
		m_QuaternionParameters.clear();
		if (!m_AnimatorDocument)
			return;
		for (const auto& parameter : m_AnimatorDocument->parameters)
		{
			switch (parameter.type)
			{
			case Vans::EditorAPI::AnimatorParamType::Float:
				m_FloatParameters[parameter.name] = parameter.floatVal; break;
			case Vans::EditorAPI::AnimatorParamType::Bool:
			case Vans::EditorAPI::AnimatorParamType::Trigger:
				m_BoolParameters[parameter.name] = parameter.boolVal; break;
			case Vans::EditorAPI::AnimatorParamType::Int:
				m_IntParameters[parameter.name] = parameter.intVal; break;
			case Vans::EditorAPI::AnimatorParamType::Vector3:
				m_VectorParameters[parameter.name] = parameter.vec3Val; break;
			case Vans::EditorAPI::AnimatorParamType::Quaternion:
				m_QuaternionParameters[parameter.name] = parameter.quatVal; break;
			}
		}
	}

	void VansSceneAnimationPreviewWindow::StartPreview(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		StopPreview();
		m_ActiveAPI = &editorAPI;
		if (m_SelectedAnimatorGuid.empty() || m_SelectedEntityGuid.empty()
			|| m_SelectedAnimationComponentGuid.empty())
		{
			m_Message = "Choose both an Animator and a Scene Skeleton";
			return;
		}
		if (!LoadAnimatorDocument(editorAPI))
			return;
		Vans::EditorAPI::AnimationPreviewCreateRequest request;
		request.targetKind =
			Vans::EditorAPI::AnimationPreviewTargetKind::SceneAnimationComponent;
		request.animatorAssetGuid = m_SelectedAnimatorGuid;
		request.entityGuid = m_SelectedEntityGuid;
		request.animationComponentGuid = m_SelectedAnimationComponentGuid;
		const auto created = editorAPI.CreateAnimationPreview(request);
		if (!created.success)
		{
			m_Message = created.message;
			return;
		}
		m_SessionId = created.sessionId;
		m_Playing = true;
		m_Speed = 1.0f;
		m_Message = created.message;
		RefreshRigSnapshot(editorAPI);
		m_SceneEntities = editorAPI.QueryAnimationPreviewSceneEntities(m_SessionId);
	}

	void VansSceneAnimationPreviewWindow::RefreshRigSnapshot(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (m_SessionId != 0)
			m_RigSnapshot = editorAPI.GetAnimationPreviewRigSnapshot(m_SessionId);
	}

	void VansSceneAnimationPreviewWindow::DrawSessionControls(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (ImGui::Button(m_Playing ? "Pause" : "Play"))
		{
			m_Playing = !m_Playing;
			editorAPI.SetAnimationPreviewPlayback(
				{ m_SessionId, m_Playing, m_Speed, false, 0.0f });
		}
		ImGui::SameLine();
		if (ImGui::Button("Step"))
		{
			editorAPI.SetAnimationPreviewPlayback(
				{ m_SessionId, true, m_Speed, false, 0.0f });
			editorAPI.TickAnimationPreview(m_SessionId, 1.0f / 60.0f);
			m_Playing = false;
			editorAPI.SetAnimationPreviewPlayback(
				{ m_SessionId, false, m_Speed, false, 0.0f });
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop Preview"))
		{
			StopPreview();
			return;
		}
		if (ImGui::SliderFloat("Speed", &m_Speed, 0.0f, 3.0f, "%.2f"))
			editorAPI.SetAnimationPreviewPlayback(
				{ m_SessionId, m_Playing, m_Speed, false, 0.0f });

		if (m_Snapshot.duration > 0.0f)
		{
			float time = m_Snapshot.currentTime;
			ImGui::BeginDisabled(!m_Snapshot.seekSupported);
			if (ImGui::SliderFloat("Timeline", &time, 0.0f, m_Snapshot.duration,
				"%.3f s"))
			{
				m_Playing = false;
				Vans::EditorAPI::AnimationPreviewPlaybackRequest request;
				request.sessionId = m_SessionId;
				request.playing = false;
				request.speed = m_Speed;
				request.seek = true;
				request.seekSeconds = time;
				editorAPI.SetAnimationPreviewPlayback(request);
			}
			ImGui::EndDisabled();
			if (!m_Snapshot.seekSupported)
				ImGui::TextDisabled("%s", m_Snapshot.seekUnavailableReason.c_str());
		}
	}

	void VansSceneAnimationPreviewWindow::DrawParameters(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (!m_AnimatorDocument || !ImGui::CollapsingHeader("Parameters",
			ImGuiTreeNodeFlags_DefaultOpen))
			return;
		for (const auto& parameter : m_AnimatorDocument->parameters)
		{
			ImGui::PushID(parameter.name.c_str());
			Vans::EditorAPI::AnimationPreviewParameterValue value;
			value.sessionId = m_SessionId;
			value.name = parameter.name;
			bool changed = false;
			switch (parameter.type)
			{
			case Vans::EditorAPI::AnimatorParamType::Float:
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Float;
				changed = ImGui::DragFloat(parameter.name.c_str(),
					&m_FloatParameters[parameter.name], 0.01f);
				value.floatValue = m_FloatParameters[parameter.name];
				break;
			case Vans::EditorAPI::AnimatorParamType::Bool:
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Bool;
				changed = ImGui::Checkbox(parameter.name.c_str(),
					&m_BoolParameters[parameter.name]);
				value.boolValue = m_BoolParameters[parameter.name];
				break;
			case Vans::EditorAPI::AnimatorParamType::Int:
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Int;
				changed = ImGui::DragInt(parameter.name.c_str(),
					&m_IntParameters[parameter.name], 1.0f);
				value.intValue = m_IntParameters[parameter.name];
				break;
			case Vans::EditorAPI::AnimatorParamType::Trigger:
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Trigger;
				changed = ImGui::Button(parameter.name.c_str());
				break;
			case Vans::EditorAPI::AnimatorParamType::Vector3:
			{
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Vector3;
				auto& source = m_VectorParameters[parameter.name];
				float data[3] = { source.x, source.y, source.z };
				changed = ImGui::DragFloat3(parameter.name.c_str(), data, 0.01f);
				source = { data[0], data[1], data[2] };
				value.vectorValue = { source.x, source.y, source.z };
				break;
			}
			case Vans::EditorAPI::AnimatorParamType::Quaternion:
			{
				value.type = Vans::EditorAPI::AnimationPreviewParameterType::Quaternion;
				auto& source = m_QuaternionParameters[parameter.name];
				float data[4] = { source.x, source.y, source.z, source.w };
				changed = ImGui::DragFloat4(parameter.name.c_str(), data, 0.01f);
				source = { data[0], data[1], data[2], data[3] };
				value.quaternionValue = { source.x, source.y, source.z, source.w };
				break;
			}
			}
			if (changed && !editorAPI.SetAnimationPreviewParameter(value))
				m_Message = "Animator parameter update was rejected: " + parameter.name;
			ImGui::PopID();
		}
	}

	void VansSceneAnimationPreviewWindow::DrawGraphSetsAndSlots(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (!m_AnimatorDocument)
			return;
		if (ImGui::CollapsingHeader("Graph Sets & Slots", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const auto graphSet = std::find_if(
				m_AnimatorDocument->graphSets.begin(), m_AnimatorDocument->graphSets.end(),
				[&](const auto& item) { return item.id == m_SelectedGraphSetId; });
			const char* graphSetLabel = graphSet == m_AnimatorDocument->graphSets.end()
				? "Choose Graph Set..." : graphSet->name.c_str();
			if (ImGui::BeginCombo("Graph Set", graphSetLabel))
			{
				for (const auto& item : m_AnimatorDocument->graphSets)
				{
					if (ImGui::Selectable(item.name.c_str(), item.id == m_SelectedGraphSetId))
					{
						m_SelectedGraphSetId = item.id;
						if (!editorAPI.SwitchAnimationPreviewGraphSet(
							{ m_SessionId, item.id }))
							m_Message = "Graph Set switch was rejected";
					}
				}
				ImGui::EndCombo();
			}

			const auto slot = std::find_if(
				m_AnimatorDocument->slots.begin(), m_AnimatorDocument->slots.end(),
				[&](const auto& item) { return item.id == m_SelectedSlotId; });
			const char* slotLabel = slot == m_AnimatorDocument->slots.end()
				? "Choose Slot..." : slot->name.c_str();
			if (ImGui::BeginCombo("Slot", slotLabel))
			{
				for (const auto& item : m_AnimatorDocument->slots)
					if (ImGui::Selectable(item.name.c_str(), item.id == m_SelectedSlotId))
						m_SelectedSlotId = item.id;
				ImGui::EndCombo();
			}
			const auto clip = std::find_if(
				m_AnimatorDocument->clipRefs.begin(), m_AnimatorDocument->clipRefs.end(),
				[&](const auto& item) { return item.name == m_SelectedClipName; });
			const char* clipLabel = clip == m_AnimatorDocument->clipRefs.end()
				? "Choose Clip..." : clip->name.c_str();
			if (ImGui::BeginCombo("Slot Clip", clipLabel))
			{
				for (const auto& item : m_AnimatorDocument->clipRefs)
					if (ImGui::Selectable(item.name.c_str(), item.name == m_SelectedClipName))
						m_SelectedClipName = item.name;
				ImGui::EndCombo();
			}
			const bool canTrigger = !m_SelectedSlotId.empty() && !m_SelectedClipName.empty();
			ImGui::BeginDisabled(!canTrigger);
			if (ImGui::Button("Trigger Slot") && !editorAPI.TriggerAnimationPreviewSlot(
				{ m_SessionId, m_SelectedSlotId, m_SelectedClipName, 1.0f, 1, 0 }))
				m_Message = "Slot request was rejected";
			ImGui::EndDisabled();
		}
	}

	void VansSceneAnimationPreviewWindow::DrawSocketTransform(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		const auto found = std::find_if(m_RigSnapshot.sockets.begin(),
			m_RigSnapshot.sockets.end(),
			[&](const auto& socket) { return socket.guid == m_SelectedSocketGuid; });
		if (found == m_RigSnapshot.sockets.end())
			return;
		ImGui::SeparatorText("Socket Transform");
		ImGui::Text("%s", found->name.c_str());
		int space = m_SocketEditSpace == Vans::EditorAPI::RuntimeTransformSpace::World ? 1 : 0;
		if (ImGui::Combo("Edit Space##Socket", &space, "Local (Bone)\0World\0"))
			m_SocketEditSpace = space == 0
				? Vans::EditorAPI::RuntimeTransformSpace::Local
				: Vans::EditorAPI::RuntimeTransformSpace::World;
		auto transform = m_SocketEditSpace == Vans::EditorAPI::RuntimeTransformSpace::World
			? found->worldTransform : found->localTransform;
		if (EditTransform("SocketTransform", transform))
		{
			Vans::EditorAPI::AnimationPreviewRigSocketTransformRequest request;
			request.sessionId = m_SessionId;
			request.expectedRigRevision = m_RigSnapshot.rigRevision;
			request.socketGuid = found->guid;
			request.space = m_SocketEditSpace;
			request.transform = transform;
			const auto edited = editorAPI.SetAnimationPreviewRigSocketTransform(request);
			m_Message = edited.message;
			RefreshRigSnapshot(editorAPI);
		}
		ImGui::TextDisabled("The Scene viewport handle edits the same selected Socket.");
	}

	void VansSceneAnimationPreviewWindow::DrawAttachmentTransform(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		const auto found = std::find_if(m_RigSnapshot.attachments.begin(),
			m_RigSnapshot.attachments.end(),
			[&](const auto& item) { return item.entityGuid == m_SelectedAttachmentGuid; });
		if (found == m_RigSnapshot.attachments.end())
			return;
		auto attachment = *found;
		ImGui::SeparatorText("Preview Object Transform");
		ImGui::Text("%s", attachment.name.c_str());
		int space = m_AttachmentEditSpace == Vans::EditorAPI::RuntimeTransformSpace::World ? 1 : 0;
		if (ImGui::Combo("Edit Space##Attachment", &space, "Local (Socket)\0World\0"))
			m_AttachmentEditSpace = space == 0
				? Vans::EditorAPI::RuntimeTransformSpace::Local
				: Vans::EditorAPI::RuntimeTransformSpace::World;
		auto transform = m_AttachmentEditSpace == Vans::EditorAPI::RuntimeTransformSpace::World
			? attachment.worldTransform : attachment.localTransform;
		if (EditTransform("AttachmentTransform", transform))
		{
			Vans::EditorAPI::AnimationPreviewAttachmentTransformRequest request;
			request.sessionId = m_SessionId;
			request.expectedAttachmentRevision = m_RigSnapshot.attachmentRevision;
			request.entityGuid = attachment.entityGuid;
			request.space = m_AttachmentEditSpace;
			request.transform = transform;
			const auto edited = editorAPI.SetAnimationPreviewAttachmentTransform(request);
			m_Message = edited.message;
			if (edited.success)
				attachment.localTransform = edited.localTransform;
			RefreshRigSnapshot(editorAPI);
		}
		const auto savedProfile = std::find_if(m_RigSnapshot.attachmentProfiles.begin(),
			m_RigSnapshot.attachmentProfiles.end(),
			[&](const auto& profile)
			{
				return profile.modelGuid == attachment.modelGuid
					&& profile.parentKind == attachment.parent.kind
					&& profile.anchorGuid == attachment.parent.anchorGuid;
			});
		const bool hasSavedProfile =
			savedProfile != m_RigSnapshot.attachmentProfiles.end();
		const bool canSaveProfile = !attachment.modelGuid.empty()
			&& (attachment.parent.kind == Vans::EditorAPI::RuntimeParentKind::Bone
				|| attachment.parent.kind == Vans::EditorAPI::RuntimeParentKind::Socket)
			&& attachment.localTransform.available;
		ImGui::BeginDisabled(!canSaveProfile);
		if (ImGui::Button("Save Attachment Offset to Rig"))
		{
			Vans::EditorAPI::AnimationPreviewRigAttachmentProfileRequest request;
			request.sessionId = m_SessionId;
			request.expectedRigRevision = m_RigSnapshot.rigRevision;
			request.modelGuid = attachment.modelGuid;
			request.parentKind = attachment.parent.kind;
			request.anchorGuid = attachment.parent.anchorGuid;
			request.localTransform = attachment.localTransform;
			const auto edited = editorAPI.SetAnimationPreviewRigAttachmentProfile(request);
			m_Message = edited.message;
			if (edited.success)
			{
				RefreshRigSnapshot(editorAPI);
				SaveRigChanges(editorAPI, "Attachment offset saved to Animation Rig");
			}
		}
		ImGui::EndDisabled();
		if (!canSaveProfile)
			ImGui::TextDisabled("The selected Scene Object needs one Model asset and a Bone/Socket parent.");
		else if (hasSavedProfile)
		{
			ImGui::SameLine();
			if (ImGui::Button("Remove Saved Offset"))
			{
				Vans::EditorAPI::AnimationPreviewRigAttachmentProfileRequest request;
				request.sessionId = m_SessionId;
				request.expectedRigRevision = m_RigSnapshot.rigRevision;
				request.modelGuid = attachment.modelGuid;
				request.parentKind = attachment.parent.kind;
				request.anchorGuid = attachment.parent.anchorGuid;
				request.remove = true;
				const auto edited = editorAPI.SetAnimationPreviewRigAttachmentProfile(request);
				m_Message = edited.message;
				if (edited.success)
				{
					RefreshRigSnapshot(editorAPI);
					SaveRigChanges(editorAPI, "Attachment offset removed from Animation Rig");
				}
			}
		}
		if (ImGui::Button("Remove Preview Binding"))
		{
			Vans::EditorAPI::AnimationPreviewAttachmentBindingRequest request;
			request.sessionId = m_SessionId;
			request.expectedAttachmentRevision = m_RigSnapshot.attachmentRevision;
			request.entityGuid = attachment.entityGuid;
			request.transformPolicy =
				Vans::EditorAPI::RuntimeReparentTransformPolicy::KeepWorld;
			m_Message = editorAPI.SetAnimationPreviewAttachmentBinding(request).message;
			m_SelectedAttachmentGuid.clear();
			m_TransformTarget = TransformTarget::None;
			RefreshRigSnapshot(editorAPI);
		}
	}

	bool VansSceneAnimationPreviewWindow::SaveRigChanges(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI,
		const char* successMessage)
	{
		auto working = editorAPI.GetAnimationPreviewWorkingRigDocument(m_SessionId);
		if (!working.success)
		{
			m_Message = working.message;
			return false;
		}
		const auto encoded = editorAPI.EncodeAnimationRigDocument(working.document);
		if (!encoded.success)
		{
			m_Message = encoded.message;
			return false;
		}
		auto document = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(
			m_RigSnapshot.rigAssetPath);
		if (!document || !document->sourceDocument.IsLoaded())
		{
			m_Message = document ? document->lastError
				: "Animation Rig document could not be opened";
			return false;
		}
		const auto edit = Vans::VansAssetDocumentEditService::ReplaceRoot(
			document->sourceDocument,
			Vans::DecodeSerializedValueJson(nlohmann::json::parse(encoded.canonicalJson)));
		if (!edit && edit.message != "Asset property is unchanged")
		{
			m_Message = edit.message;
			return false;
		}
		const auto saved = Vans::VansAnimationRigSaveService::Save(document);
		if (!saved)
		{
			m_Message = saved.message;
			return false;
		}
		const auto adopted = editorAPI.AdoptAnimationPreviewRig(
			{ m_SessionId, m_RigSnapshot.rigRevision });
		if (!adopted.success)
		{
			m_Message = "Animation Rig file was saved, but the active preview "
				"could not adopt the saved baseline: " + adopted.message;
			return false;
		}
		m_Message = successMessage;
		RefreshRigSnapshot(editorAPI);
		return true;
	}

	void VansSceneAnimationPreviewWindow::DrawSocketAndAttachmentEditor(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (!m_RigSnapshot.available)
		{
			ImGui::TextDisabled("%s", m_RigSnapshot.diagnostic.c_str());
			return;
		}
		ImGui::SeparatorText("Sockets & Preview Objects");
		ImGui::TextDisabled("Rig: %s", m_RigSnapshot.rigAssetPath.c_str());
		if (m_RigSnapshot.retargetEnabled)
		{
			ImGui::TextColored(ImVec4(0.45f, 0.78f, 1.0f, 1.0f),
				"Auto Retarget: Source Animator -> Scene Target Skeleton");
			ImGui::TextDisabled("Profile: %s", m_RigSnapshot.retargetProfilePath.c_str());
		}
		if (ImGui::BeginTable("PreviewSocketLists", 2,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Sockets");
			ImGui::TableSetupColumn("Temporary Bindings");
			ImGui::TableNextColumn();
			for (const auto& socket : m_RigSnapshot.sockets)
			{
				const std::string label = socket.name + " [" +
					std::to_string(socket.attachmentCount) + "]##" + socket.guid;
				if (ImGui::Selectable(label.c_str(), m_SelectedSocketGuid == socket.guid))
				{
					m_SelectedSocketGuid = socket.guid;
					m_SelectedBindAnchorGuid = socket.guid;
					m_SelectedBindAnchorLabel = "Socket: " + socket.name;
					m_SelectedBindParentKind =
						Vans::EditorAPI::RuntimeParentKind::Socket;
					m_TransformTarget = TransformTarget::Socket;
				}
			}
			ImGui::TableNextColumn();
			for (const auto& attachment : m_RigSnapshot.attachments)
			{
				// A removed preview binding remains dirty until the session ends so
				// the restore service can put its original Scene state back. It is no
				// longer an attachment of this target and should not be presented as
				// one in the editor.
				if (attachment.parent.kind == Vans::EditorAPI::RuntimeParentKind::None)
					continue;
				if (ImGui::Selectable((attachment.name + "##" +
					attachment.entityGuid).c_str(),
					m_SelectedAttachmentGuid == attachment.entityGuid))
				{
					m_SelectedAttachmentGuid = attachment.entityGuid;
					m_TransformTarget = TransformTarget::Attachment;
				}
			}
			ImGui::EndTable();
		}

		const auto selectedEntity = std::find_if(m_SceneEntities.begin(),
			m_SceneEntities.end(), [&](const auto& item)
			{ return item.entityGuid == m_SelectedSceneEntityGuid; });
		const char* entityLabel = selectedEntity == m_SceneEntities.end()
			? "Choose Scene Object..." : selectedEntity->name.c_str();
		if (ImGui::BeginCombo("Scene Object", entityLabel))
		{
			m_SceneEntities =
				editorAPI.QueryAnimationPreviewSceneEntities(m_SessionId);
			for (const auto& entity : m_SceneEntities)
			{
				const std::string label = entity.name + (entity.active ? "" : " (inactive)")
					+ "##" + entity.entityGuid;
				if (ImGui::Selectable(label.c_str(),
					entity.entityGuid == m_SelectedSceneEntityGuid))
					m_SelectedSceneEntityGuid = entity.entityGuid;
			}
			ImGui::EndCombo();
		}
		if (ImGui::BeginCombo("Attach To", m_SelectedBindAnchorLabel.c_str()))
		{
			if (ImGui::BeginMenu("Sockets"))
			{
				for (const auto& socket : m_RigSnapshot.sockets)
				{
					const std::string label = socket.name + "##" + socket.guid;
					const bool selected = m_SelectedBindParentKind ==
						Vans::EditorAPI::RuntimeParentKind::Socket
						&& m_SelectedBindAnchorGuid == socket.guid;
					if (ImGui::MenuItem(label.c_str(), nullptr, selected))
					{
						m_SelectedSocketGuid = socket.guid;
						m_SelectedBindAnchorGuid = socket.guid;
						m_SelectedBindAnchorLabel = "Socket: " + socket.name;
						m_SelectedBindParentKind =
							Vans::EditorAPI::RuntimeParentKind::Socket;
						m_TransformTarget = TransformTarget::Socket;
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Bones"))
			{
				for (const auto& bone : m_Snapshot.bones)
				{
					if (bone.guid.empty())
						continue;
					const std::string label = bone.name + "##" + bone.guid;
					const bool selected = m_SelectedBindParentKind ==
						Vans::EditorAPI::RuntimeParentKind::Bone
						&& m_SelectedBindAnchorGuid == bone.guid;
					if (ImGui::MenuItem(label.c_str(), nullptr, selected))
					{
						m_SelectedSocketGuid.clear();
						m_SelectedBindAnchorGuid = bone.guid;
						m_SelectedBindAnchorLabel = "Bone: " + bone.name;
						m_SelectedBindParentKind =
							Vans::EditorAPI::RuntimeParentKind::Bone;
						m_TransformTarget = TransformTarget::None;
					}
				}
				ImGui::EndMenu();
			}
			ImGui::EndCombo();
		}
		int policy = static_cast<int>(m_BindPolicy);
		if (ImGui::Combo("Bind Policy", &policy, "Keep World\0Keep Local\0Snap\0"))
			m_BindPolicy = policy == 0
				? Vans::EditorAPI::RuntimeReparentTransformPolicy::KeepWorld
				: policy == 1
					? Vans::EditorAPI::RuntimeReparentTransformPolicy::KeepLocal
					: Vans::EditorAPI::RuntimeReparentTransformPolicy::Snap;
		const bool canBind = !m_SelectedSceneEntityGuid.empty()
			&& !m_SelectedBindAnchorGuid.empty()
			&& (m_SelectedBindParentKind == Vans::EditorAPI::RuntimeParentKind::Bone
				|| m_SelectedBindParentKind ==
					Vans::EditorAPI::RuntimeParentKind::Socket);
		ImGui::BeginDisabled(!canBind);
		if (ImGui::Button("Bind Scene Object to Bone/Socket (Preview Only)"))
		{
			Vans::EditorAPI::AnimationPreviewAttachmentBindingRequest request;
			request.sessionId = m_SessionId;
			request.expectedAttachmentRevision = m_RigSnapshot.attachmentRevision;
			request.entityGuid = m_SelectedSceneEntityGuid;
			request.parent.kind = m_SelectedBindParentKind;
			request.parent.entityGuid = m_RigSnapshot.entityGuid;
			request.parent.animationComponentGuid =
				m_RigSnapshot.animationComponentGuid;
			request.parent.anchorGuid = m_SelectedBindAnchorGuid;
			request.transformPolicy = m_BindPolicy;
			const auto bound = editorAPI.SetAnimationPreviewAttachmentBinding(request);
			m_Message = bound.message;
			if (bound.success)
			{
				m_SelectedAttachmentGuid = m_SelectedSceneEntityGuid;
				m_TransformTarget = TransformTarget::Attachment;
				RefreshRigSnapshot(editorAPI);
				const auto attachment = std::find_if(m_RigSnapshot.attachments.begin(),
					m_RigSnapshot.attachments.end(),
					[&](const auto& item)
					{ return item.entityGuid == m_SelectedAttachmentGuid; });
				if (attachment != m_RigSnapshot.attachments.end())
				{
					const auto profile = std::find_if(
						m_RigSnapshot.attachmentProfiles.begin(),
						m_RigSnapshot.attachmentProfiles.end(),
						[&](const auto& item)
						{
							return item.modelGuid == attachment->modelGuid
								&& item.parentKind == attachment->parent.kind
								&& item.anchorGuid == attachment->parent.anchorGuid;
						});
					if (profile != m_RigSnapshot.attachmentProfiles.end())
					{
						Vans::EditorAPI::AnimationPreviewAttachmentTransformRequest transformRequest;
						transformRequest.sessionId = m_SessionId;
						transformRequest.expectedAttachmentRevision =
							m_RigSnapshot.attachmentRevision;
						transformRequest.entityGuid = attachment->entityGuid;
						transformRequest.space =
							Vans::EditorAPI::RuntimeTransformSpace::Local;
						transformRequest.transform = profile->localTransform;
						const auto applied = editorAPI.SetAnimationPreviewAttachmentTransform(
							transformRequest);
						m_Message = applied.success
							? "Preview binding applied the saved Animation Rig attachment offset"
							: applied.message;
						RefreshRigSnapshot(editorAPI);
					}
				}
			}
		}
		ImGui::EndDisabled();
		ImGui::TextDisabled(
			"Scene state is restored when preview stops; saved offsets live in the Animation Rig.");

		if (m_TransformTarget == TransformTarget::Socket)
			DrawSocketTransform(editorAPI);
		else if (m_TransformTarget == TransformTarget::Attachment)
			DrawAttachmentTransform(editorAPI);

		ImGui::SeparatorText("Scene Handle");
		int operation = static_cast<int>(m_GizmoOperation);
		if (ImGui::Combo("Operation", &operation, "Translate\0Rotate\0Scale\0"))
			m_GizmoOperation = static_cast<GizmoOperation>(operation);
		ImGui::Checkbox("World Space", &m_GizmoWorldSpace);
		const bool dirty = m_RigSnapshot.rigRevision > 0;
		ImGui::BeginDisabled(!dirty);
		if (ImGui::Button("Save Rig Changes"))
			SaveRigChanges(editorAPI, "Animation Rig changes saved");
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextColored(dirty ? ImVec4(1.0f, 0.7f, 0.3f, 1.0f)
			: ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
			"Rig %s", dirty ? "Modified" : "Saved");
	}

	void VansSceneAnimationPreviewWindow::ShowWindow(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		m_ActiveAPI = &editorAPI;
		if (!m_IsOpen)
		{
			StopPreview();
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(560.0f, 760.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Scene Animation Preview", &m_IsOpen))
		{
			ImGui::End();
			return;
		}
		if (!m_IsOpen)
		{
			StopPreview();
			ImGui::End();
			return;
		}
		if (!editorAPI.IsRuntimeSceneReady())
		{
			StopPreview();
			ImGui::TextDisabled("Load a Scene in Editor mode first.");
			ImGui::End();
			return;
		}

		if (ImGui::BeginCombo("Animator", m_SelectedAnimatorLabel.c_str()))
		{
			const auto animators = editorAPI.QueryAssets({
				Vans::EditorAPI::AssetType::AnimatorController, false,
				Vans::EditorAPI::AssetQueryCapability::Any });
			for (const auto& asset : animators)
			{
				if (ImGui::Selectable(asset.relativePath.c_str(),
					asset.guid == m_SelectedAnimatorGuid))
				{
					if (m_SelectedAnimatorGuid != asset.guid) StopPreview();
					m_SelectedAnimatorGuid = asset.guid;
					m_SelectedAnimatorLabel = asset.relativePath;
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::BeginCombo("Scene Skeleton", m_SelectedRigLabel.c_str()))
		{
			const auto sceneRigs = editorAPI.GetSceneSkeletonHierarchy("");
			for (const auto& rig : sceneRigs.rigs)
			{
				if (rig.entityGuid.empty() || rig.animationComponentGuid.empty())
					continue;
				const std::string label = rig.nodeName + "##" + rig.entityGuid
					+ rig.animationComponentGuid;
				const bool selected = rig.entityGuid == m_SelectedEntityGuid
					&& rig.animationComponentGuid == m_SelectedAnimationComponentGuid;
				if (ImGui::Selectable(label.c_str(), selected))
				{
					if (!selected) StopPreview();
					m_SelectedEntityGuid = rig.entityGuid;
					m_SelectedAnimationComponentGuid = rig.animationComponentGuid;
					m_SelectedRigLabel = rig.nodeName;
				}
			}
			ImGui::EndCombo();
		}
		if (m_SessionId == 0)
		{
			const bool canStart = !m_SelectedAnimatorGuid.empty()
				&& !m_SelectedEntityGuid.empty();
			ImGui::BeginDisabled(!canStart);
			if (ImGui::Button("Start Scene Preview"))
				StartPreview(editorAPI);
			ImGui::EndDisabled();
			ImGui::TextDisabled(
				"The selected Animator is compiled against the target's direct or Retarget Source Skeleton automatically.");
		}
		else
		{
			editorAPI.TickAnimationPreview(m_SessionId, ImGui::GetIO().DeltaTime);
			m_Snapshot = editorAPI.GetAnimationPreviewSnapshot(m_SessionId);
			if (!m_Snapshot.available)
			{
				// Scene load/unload owns the engine-side teardown. Do not leave this
				// window pointing at the retired session id.
				m_Message = "Scene preview ended because its target Scene changed";
				m_SessionId = 0;
				m_RigSnapshot = {};
				m_SceneEntities.clear();
				m_SelectedSocketGuid.clear();
				m_SelectedBindAnchorGuid.clear();
				m_SelectedBindAnchorLabel = "Choose Bone or Socket...";
				m_SelectedBindParentKind =
					Vans::EditorAPI::RuntimeParentKind::None;
				m_SelectedAttachmentGuid.clear();
				m_TransformTarget = TransformTarget::None;
			}
			if (m_SessionId == 0)
			{
				ImGui::TextDisabled("Start a new preview for the current Scene.");
			}
			else
			{
				RefreshRigSnapshot(editorAPI);
				DrawSessionControls(editorAPI);
				if (m_SessionId != 0)
				{
					DrawParameters(editorAPI);
					DrawGraphSetsAndSlots(editorAPI);
					DrawSocketAndAttachmentEditor(editorAPI);
				}
			}
		}
		if (!m_Message.empty())
			ImGui::TextWrapped("%s", m_Message.c_str());
		if (m_SessionId != 0 && !m_Snapshot.diagnostic.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
				m_Snapshot.diagnostic.c_str());
		ImGui::End();
	}

	bool VansSceneAnimationPreviewWindow::DrawSceneViewportHandle(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI,
		VansCamera* camera,
		const ImVec2& viewportOrigin,
		const ImVec2& viewportSize)
	{
		if (!m_IsOpen || m_SessionId == 0 || !camera
			|| viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return false;
		const Vans::EditorAPI::RuntimeTransformSnapshot* worldTransform = nullptr;
		std::string targetGuid;
		if (m_TransformTarget == TransformTarget::Socket)
		{
			const auto found = std::find_if(m_RigSnapshot.sockets.begin(),
				m_RigSnapshot.sockets.end(),
				[&](const auto& item) { return item.guid == m_SelectedSocketGuid; });
			if (found != m_RigSnapshot.sockets.end())
			{
				worldTransform = &found->worldTransform;
				targetGuid = found->guid;
			}
		}
		else if (m_TransformTarget == TransformTarget::Attachment)
		{
			const auto found = std::find_if(m_RigSnapshot.attachments.begin(),
				m_RigSnapshot.attachments.end(),
				[&](const auto& item) { return item.entityGuid == m_SelectedAttachmentGuid; });
			if (found != m_RigSnapshot.attachments.end() && found->editable)
			{
				worldTransform = &found->worldTransform;
				targetGuid = found->entityGuid;
			}
		}
		if (!worldTransform || !IsUsableTransform(*worldTransform))
			return false;

		glm::mat4 matrix = BuildTransformMatrix(*worldTransform);
		const glm::mat4 view = camera->GetViewMatrix();
		const glm::mat4 projection = camera->GetProjectiveMatrix();
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
		ImGuizmo::SetRect(viewportOrigin.x, viewportOrigin.y,
			viewportSize.x, viewportSize.y);
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetID(static_cast<int>(ImGui::GetID(
			("SceneAnimationPreviewHandle##" + targetGuid).c_str())));
		const ImGuizmo::OPERATION operation = m_GizmoOperation == GizmoOperation::Translate
			? ImGuizmo::TRANSLATE : m_GizmoOperation == GizmoOperation::Rotate
				? ImGuizmo::ROTATE : ImGuizmo::SCALE;
		if (!ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
			operation, m_GizmoWorldSpace ? ImGuizmo::WORLD : ImGuizmo::LOCAL,
			glm::value_ptr(matrix)))
		{
			return true;
		}

		float position[3], rotation[3], scale[3];
		ImGuizmo::DecomposeMatrixToComponents(
			glm::value_ptr(matrix), position, rotation, scale);
		auto edited = *worldTransform;
		edited.space = Vans::EditorAPI::RuntimeTransformSpace::World;
		edited.position = { position[0], position[1], position[2] };
		edited.rotationDegrees = { rotation[0], rotation[1], rotation[2] };
		edited.scale = { scale[0], scale[1], scale[2] };
		if (m_TransformTarget == TransformTarget::Socket)
		{
			Vans::EditorAPI::AnimationPreviewRigSocketTransformRequest request;
			request.sessionId = m_SessionId;
			request.expectedRigRevision = m_RigSnapshot.rigRevision;
			request.socketGuid = targetGuid;
			request.space = Vans::EditorAPI::RuntimeTransformSpace::World;
			request.transform = edited;
			m_Message = editorAPI.SetAnimationPreviewRigSocketTransform(request).message;
		}
		else
		{
			Vans::EditorAPI::AnimationPreviewAttachmentTransformRequest request;
			request.sessionId = m_SessionId;
			request.expectedAttachmentRevision = m_RigSnapshot.attachmentRevision;
			request.entityGuid = targetGuid;
			request.space = Vans::EditorAPI::RuntimeTransformSpace::World;
			request.transform = edited;
			m_Message = editorAPI.SetAnimationPreviewAttachmentTransform(request).message;
		}
		RefreshRigSnapshot(editorAPI);
		return true;
	}
}
