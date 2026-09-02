#include "AnimationPreviewAttachmentAuthoringService.h"

#include "../../AssetCore/VansAssetGuid.h"
#include "../../RenderCore/VansScene.h"
#include "../../SceneCore/VansSceneParentReference.h"
#include "../../SceneRuntime/Transform/VansTransformGraph.h"
#include "../../ScriptCore/VansScriptContext.h"

#include <../../GLM/gtc/quaternion.hpp>

#include <cmath>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Vans::EditorAPI
{
	namespace
	{
		struct OriginalAttachmentState
		{
			bool hasParent = false;
			VansSceneParentReference parent;
			VansLocalTransform local;
			std::vector<std::pair<VansComponentHandle, bool>> renderComponents;
		};

		struct AttachmentSessionState
		{
			std::uint64_t revision = 0;
			std::unordered_map<std::string, OriginalAttachmentState> originals;
			std::unordered_set<std::string> dirtyEntities;
		};

		std::unordered_map<AnimationPreviewSessionId, AttachmentSessionState>& Sessions()
		{
			static std::unordered_map<AnimationPreviewSessionId, AttachmentSessionState> sessions;
			return sessions;
		}

		RuntimeParentReference ToDTO(const VansSceneParentReference& parent, bool hasParent)
		{
			RuntimeParentReference result;
			if (!hasParent)
				return result;
			result.entityGuid = parent.entityGuid.ToString();
			result.animationComponentGuid = parent.animationComponentGuid.ToString();
			result.anchorGuid = parent.anchorGuid.ToString();
			switch (parent.kind)
			{
			case VansSceneParentKind::Entity: result.kind = RuntimeParentKind::Entity; break;
			case VansSceneParentKind::Bone: result.kind = RuntimeParentKind::Bone; break;
			case VansSceneParentKind::Socket: result.kind = RuntimeParentKind::Socket; break;
			}
			return result;
		}

		bool ToNative(
			const RuntimeParentReference& source,
			std::optional<VansSceneParentReference>& parent,
			std::string& error)
		{
			parent.reset();
			if (source.kind == RuntimeParentKind::None)
				return source.entityGuid.empty() && source.animationComponentGuid.empty()
					&& source.anchorGuid.empty();
			VansSceneParentReference value;
			if (!VansAssetGuid::TryParse(source.entityGuid, value.entityGuid))
			{
				error = "Attachment parent entity GUID is invalid";
				return false;
			}
			if (source.kind == RuntimeParentKind::Entity)
			{
				value.kind = VansSceneParentKind::Entity;
				if (!source.animationComponentGuid.empty() || !source.anchorGuid.empty())
				{
					error = "Entity attachment parent must not contain anchor identity";
					return false;
				}
			}
			else if (source.kind == RuntimeParentKind::Bone
				|| source.kind == RuntimeParentKind::Socket)
			{
				value.kind = source.kind == RuntimeParentKind::Bone
					? VansSceneParentKind::Bone : VansSceneParentKind::Socket;
				if (!VansAssetGuid::TryParse(
					source.animationComponentGuid, value.animationComponentGuid)
					|| !VansAssetGuid::TryParse(source.anchorGuid, value.anchorGuid))
				{
					error = "Attachment anchor identity is invalid";
					return false;
				}
			}
			else
			{
				error = "Attachment parent kind is invalid";
				return false;
			}
			parent = std::move(value);
			return true;
		}

		VansTransformReparentMode ToNative(RuntimeReparentTransformPolicy policy)
		{
			switch (policy)
			{
			case RuntimeReparentTransformPolicy::KeepLocal: return VansTransformReparentMode::KeepLocal;
			case RuntimeReparentTransformPolicy::Snap: return VansTransformReparentMode::Snap;
			case RuntimeReparentTransformPolicy::KeepWorld:
			default: return VansTransformReparentMode::KeepWorld;
			}
		}

		RuntimeTransformSnapshot ToDTO(
			const VansLocalTransform& transform,
			RuntimeTransformSpace space,
			const std::string& entityGuid)
		{
			RuntimeTransformSnapshot result;
			result.available = true;
			result.entityGuid = entityGuid;
			result.space = space;
			result.position = { transform.position.x, transform.position.y, transform.position.z };
			const glm::vec3 degrees = glm::degrees(glm::eulerAngles(transform.rotation));
			result.rotationDegrees = { degrees.x, degrees.y, degrees.z };
			result.scale = { transform.scale.x, transform.scale.y, transform.scale.z };
			return result;
		}

		bool ToNative(
			const RuntimeTransformSnapshot& source,
			VansLocalTransform& transform,
			std::string& error)
		{
			transform.position = { source.position.x, source.position.y, source.position.z };
			const glm::vec3 rotationDegrees{
				source.rotationDegrees.x, source.rotationDegrees.y, source.rotationDegrees.z };
			transform.rotation = glm::quat(glm::radians(rotationDegrees));
			transform.scale = { source.scale.x, source.scale.y, source.scale.z };
			auto finite = [](const glm::vec3& value)
			{
				return std::isfinite(value.x) && std::isfinite(value.y)
					&& std::isfinite(value.z);
			};
			if (!finite(transform.position) || !finite(rotationDegrees) || !finite(transform.scale))
			{
				error = "Attachment transform contains a non-finite value";
				return false;
			}
			return true;
		}

		bool CaptureOriginal(
			AttachmentSessionState& state,
			VansGraphics::VansScene& scene,
			const std::string& entityGuid,
			std::string& error)
		{
			if (state.originals.find(entityGuid) != state.originals.end())
				return true;
			OriginalAttachmentState original;
			if (!scene.TryGetEntityParentReferenceByGuid(
				entityGuid, original.parent, original.hasParent)
				|| !scene.TryGetEntityLocalTransformByGuid(entityGuid, original.local))
			{
				error = "Attachment original Scene state is unavailable";
				return false;
			}
			if (VansRuntimeWorld* world = scene.GetRuntimeWorld())
			{
				const VansEntityHandle entity = world->Entities().FindByGuid(entityGuid);
				for (VansComponentHandle component : world->CollectComponentsInSubtree(entity))
				{
					if (component.typeId != VansRuntimeComponentType_Render)
						continue;
					original.renderComponents.emplace_back(
						component, world->IsComponentEffectivelyEnabled(component));
				}
			}
			state.originals.emplace(entityGuid, std::move(original));
			return true;
		}

		void ApplyPreviewVisibility(
			const OriginalAttachmentState& original,
			VansGraphics::VansScene& scene,
			bool visible)
		{
			VansRuntimeWorld* world = scene.GetRuntimeWorld();
			if (!world)
				return;
			for (const auto& [component, originallyVisible] : original.renderComponents)
			{
				if (world->GetComponentHeader(component))
					scene.ApplyRuntimeComponentEnabled(
						component, visible ? true : originallyVisible);
			}
		}
	}

	void AnimationPreviewAttachmentAuthoringService::BeginSession(
		AnimationPreviewSessionId sessionId)
	{
		Sessions().insert_or_assign(sessionId, AttachmentSessionState{});
	}

	std::vector<AnimationPreviewAttachmentSnapshot>
	AnimationPreviewAttachmentAuthoringService::GetSnapshots(
		AnimationPreviewSessionId sessionId,
		VansGraphics::VansScene& scene,
		const std::string& entityGuid,
		const std::string& animationComponentGuid,
		std::uint64_t& revision)
	{
		std::vector<AnimationPreviewAttachmentSnapshot> snapshots;
		auto state = Sessions().find(sessionId);
		if (state == Sessions().end())
			return snapshots;
		revision = state->second.revision;
		for (const VansScriptObject* object : scene.GetSceneObjects())
		{
			if (!object || object->m_EntityGuid == entityGuid)
				continue;
			VansSceneParentReference parent;
			bool hasParent = false;
			if (!scene.TryGetEntityParentReferenceByGuid(
				object->m_EntityGuid, parent, hasParent))
				continue;
			const bool boundToTarget = hasParent && parent.IsAnchor()
				&& parent.entityGuid.ToString() == entityGuid
				&& parent.animationComponentGuid.ToString() == animationComponentGuid;
			const bool dirtyInSession = state->second.dirtyEntities.find(
				object->m_EntityGuid) != state->second.dirtyEntities.end();
			if (!boundToTarget && !dirtyInSession)
				continue;
			VansLocalTransform local;
			VansLocalTransform world;
			if (!scene.TryGetEntityLocalTransformByGuid(object->m_EntityGuid, local)
				|| !scene.TryGetEntityWorldTransformByGuid(object->m_EntityGuid, world))
				continue;
			AnimationPreviewAttachmentSnapshot item;
			item.previewAttachmentId = std::hash<std::string>{}(object->m_EntityGuid);
			item.name = object->m_ObjectName;
			item.entityGuid = object->m_EntityGuid;
			item.modelGuid = object->m_ModelAssetGuid;
			item.parent = ToDTO(parent, hasParent);
			item.localTransform = ToDTO(local, RuntimeTransformSpace::Local, item.entityGuid);
			item.worldTransform = ToDTO(world, RuntimeTransformSpace::World, item.entityGuid);
			item.editable = true;
			item.dirty = state->second.dirtyEntities.find(item.entityGuid)
				!= state->second.dirtyEntities.end();
			snapshots.push_back(std::move(item));
		}
		return snapshots;
	}

	AnimationPreviewAttachmentEditResult
	AnimationPreviewAttachmentAuthoringService::SetTransform(
		const AnimationPreviewAttachmentTransformRequest& request,
		VansGraphics::VansScene& scene,
		const std::string& targetEntityGuid,
		const std::string& targetAnimationComponentGuid)
	{
		AnimationPreviewAttachmentEditResult result;
		auto found = Sessions().find(request.sessionId);
		if (found == Sessions().end())
		{
			result.message = "Attachment preview session is unavailable";
			return result;
		}
		result.acceptedRevision = found->second.revision;
		if (request.expectedAttachmentRevision != found->second.revision)
		{
			result.message = "Stale attachment transform revision was rejected";
			return result;
		}
		VansSceneParentReference parent;
		bool hasParent = false;
		if (!scene.TryGetEntityParentReferenceByGuid(request.entityGuid, parent, hasParent)
			|| !hasParent || !parent.IsAnchor()
			|| parent.entityGuid.ToString() != targetEntityGuid
			|| parent.animationComponentGuid.ToString() != targetAnimationComponentGuid)
		{
			result.message = "Attachment is not bound to this Animation Component";
			return result;
		}
		if (request.space == RuntimeTransformSpace::Model)
		{
			result.message = "Attachment entity transforms support Local or World space";
			return result;
		}
		if (!CaptureOriginal(found->second, scene, request.entityGuid, result.message))
			return result;
		VansLocalTransform transform;
		if (!ToNative(request.transform, transform, result.message))
			return result;
		const bool applied = request.space == RuntimeTransformSpace::Local
			? scene.SetEntityLocalTransformByGuid(request.entityGuid, transform)
			: scene.SetEntityWorldTransformByGuid(request.entityGuid, transform);
		if (!applied)
		{
			result.message = "Attachment transform could not be applied";
			return result;
		}
		found->second.revision++;
		found->second.dirtyEntities.insert(request.entityGuid);
		result.success = true;
		result.acceptedRevision = found->second.revision;
		VansLocalTransform local;
		if (scene.TryGetEntityLocalTransformByGuid(request.entityGuid, local))
			result.localTransform = ToDTO(local, RuntimeTransformSpace::Local, request.entityGuid);
		result.message = "Attachment preview transform updated";
		return result;
	}

	AnimationPreviewAttachmentEditResult
	AnimationPreviewAttachmentAuthoringService::SetBinding(
		const AnimationPreviewAttachmentBindingRequest& request,
		VansGraphics::VansScene& scene,
		const std::string& targetEntityGuid,
		const std::string& targetAnimationComponentGuid)
	{
		AnimationPreviewAttachmentEditResult result;
		auto found = Sessions().find(request.sessionId);
		if (found == Sessions().end())
		{
			result.message = "Attachment preview session is unavailable";
			return result;
		}
		result.acceptedRevision = found->second.revision;
		if (request.expectedAttachmentRevision != found->second.revision)
		{
			result.message = "Stale attachment binding revision was rejected";
			return result;
		}
		if (request.parent.kind != RuntimeParentKind::None
			&& (request.parent.entityGuid != targetEntityGuid
				|| request.parent.animationComponentGuid != targetAnimationComponentGuid
				|| (request.parent.kind != RuntimeParentKind::Bone
					&& request.parent.kind != RuntimeParentKind::Socket)))
		{
			result.message = "Attachment binding must target this preview Bone or Socket";
			return result;
		}
		if (!CaptureOriginal(found->second, scene, request.entityGuid, result.message))
			return result;
		std::optional<VansSceneParentReference> parent;
		if (!ToNative(request.parent, parent, result.message))
			return result;
		if (!scene.SetEntityParentReferenceByGuid(
			request.entityGuid, parent ? &*parent : nullptr,
			ToNative(request.transformPolicy)))
		{
			result.message = "Attachment binding could not be applied";
			return result;
		}
		ApplyPreviewVisibility(
			found->second.originals.at(request.entityGuid), scene, parent.has_value());
		found->second.revision++;
		found->second.dirtyEntities.insert(request.entityGuid);
		result.success = true;
		result.acceptedRevision = found->second.revision;
		VansLocalTransform local;
		if (scene.TryGetEntityLocalTransformByGuid(request.entityGuid, local))
			result.localTransform = ToDTO(local, RuntimeTransformSpace::Local, request.entityGuid);
		result.message = "Attachment preview binding updated";
		return result;
	}

	bool AnimationPreviewAttachmentAuthoringService::EndSession(
		AnimationPreviewSessionId sessionId,
		VansGraphics::VansScene* scene,
		std::string& error)
	{
		error.clear();
		auto found = Sessions().find(sessionId);
		if (found == Sessions().end())
			return true;
		bool success = true;
		if (scene)
		{
			for (const auto& [entityGuid, original] : found->second.originals)
			{
				const bool parentRestored = scene->SetEntityParentReferenceByGuid(
					entityGuid, original.hasParent ? &original.parent : nullptr,
					VansTransformReparentMode::KeepLocal);
				const bool localRestored = parentRestored
					&& scene->SetEntityLocalTransformByGuid(entityGuid, original.local);
				if (!localRestored)
				{
					success = false;
					error = "Failed to restore attachment Scene state for entity '" + entityGuid + "'";
				}
				ApplyPreviewVisibility(original, *scene, false);
			}
		}
		Sessions().erase(found);
		return success;
	}
}
