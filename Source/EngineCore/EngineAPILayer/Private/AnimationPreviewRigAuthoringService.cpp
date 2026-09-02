#include "AnimationPreviewRigAuthoringService.h"

#include "../../AnimationCore/Storage/VansAnimationRigStorage.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../SceneRuntime/Transform/VansTransformGraph.h"

#include <../../GLM/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
#include <utility>

namespace Vans::EditorAPI
{
	namespace
	{
		struct RigSessionState
		{
			VansGraphics::VansAnimationRigAsset workingAsset;
			std::optional<VansGraphics::VansCompiledAnimationRig> originalCompiledRig;
			std::string rigAssetGuid;
			std::string rigAssetPath;
			std::uint64_t revision = 0;
			bool overrideActive = false;
		};

		std::unordered_map<AnimationPreviewSessionId, RigSessionState>& Sessions()
		{
			static std::unordered_map<AnimationPreviewSessionId, RigSessionState> sessions;
			return sessions;
		}

		Vec3 ToDTO(const glm::vec3& value)
		{
			return { value.x, value.y, value.z };
		}

		glm::vec3 ToNative(const Vec3& value)
		{
			return { value.x, value.y, value.z };
		}

		RuntimeTransformSnapshot MakeSnapshot(
			const glm::mat4& matrix,
			RuntimeTransformSpace space)
		{
			RuntimeTransformSnapshot result;
			VansLocalTransform transform;
			if (!VansLocalTransform::TryFromMatrix(matrix, transform))
				return result;
			result.available = true;
			result.space = space;
			result.position = ToDTO(transform.position);
			result.rotationDegrees = ToDTO(
				glm::degrees(glm::eulerAngles(transform.rotation)));
			result.scale = ToDTO(transform.scale);
			return result;
		}

		bool MakeMatrix(
			const RuntimeTransformSnapshot& snapshot,
			glm::mat4& matrix,
			std::string& error)
		{
			const glm::vec3 position = ToNative(snapshot.position);
			const glm::vec3 rotationDegrees = ToNative(snapshot.rotationDegrees);
			const glm::vec3 scale = ToNative(snapshot.scale);
			auto finite = [](const glm::vec3& value)
			{
				return std::isfinite(value.x) && std::isfinite(value.y)
					&& std::isfinite(value.z);
			};
			if (!finite(position) || !finite(rotationDegrees) || !finite(scale))
			{
				error = "Transform contains a non-finite value";
				return false;
			}
			VansLocalTransform transform;
			transform.position = position;
			transform.rotation = glm::quat(glm::radians(rotationDegrees));
			transform.scale = scale;
			matrix = transform.ToMatrix();
			return true;
		}

		std::vector<glm::mat4> ResolveGlobals(
			const VansGraphics::Skeleton& skeleton,
			const VansGraphics::VansAnimationController& controller)
		{
			if (controller.GetCachedGlobalTransforms().size() == skeleton.bones.size())
				return controller.GetCachedGlobalTransforms();
			std::vector<glm::mat4> globals(skeleton.bones.size(), glm::mat4(1.0f));
			auto accumulate = [&](int index)
			{
				if (index < 0 || index >= static_cast<int>(skeleton.bones.size()))
					return;
				const auto& bone = skeleton.bones[static_cast<std::size_t>(index)];
				globals[static_cast<std::size_t>(index)] = bone.parentIndex >= 0
					&& bone.parentIndex < static_cast<int>(globals.size())
					? globals[static_cast<std::size_t>(bone.parentIndex)] * bone.localTransform
					: bone.localTransform;
			};
			if (!skeleton.topologicalOrder.empty())
				for (const int index : skeleton.topologicalOrder) accumulate(index);
			else
				for (int index = 0; index < static_cast<int>(skeleton.bones.size()); ++index)
					accumulate(index);
			return globals;
		}
	}

	bool AnimationPreviewRigAuthoringService::BeginSession(
		AnimationPreviewSessionId sessionId,
		VansGraphics::VansAnimationController& controller,
		std::string& error)
	{
		error.clear();
		const auto* compiledRig = controller.GetAnimationRig();
		if (!compiledRig)
		{
			error = "Animation preview target has no compiled Animation Rig";
			return false;
		}
		if (controller.GetAnimationRigAssetGuid().empty()
			|| controller.GetAnimationRigAssetPath().empty())
		{
			error = "Animation preview target has no Animation Rig asset identity";
			return false;
		}
		RigSessionState state;
		if (!VansGraphics::VansAnimationRigStorage::Load(
			controller.GetAnimationRigAssetPath(), state.workingAsset, error))
			return false;
		state.originalCompiledRig = *compiledRig;
		state.rigAssetGuid = controller.GetAnimationRigAssetGuid();
		state.rigAssetPath = controller.GetAnimationRigAssetPath();
		Sessions().insert_or_assign(sessionId, std::move(state));
		return true;
	}

	AnimationPreviewRigSnapshot AnimationPreviewRigAuthoringService::GetSnapshot(
		const AnimationPreviewRigContext& context)
	{
		AnimationPreviewRigSnapshot snapshot;
		snapshot.sessionId = context.sessionId;
		snapshot.sceneContentRevision = context.sceneContentRevision;
		snapshot.entityGuid = context.entityGuid;
		snapshot.animationComponentGuid = context.animationComponentGuid;
		auto found = Sessions().find(context.sessionId);
		if (found == Sessions().end())
		{
			snapshot.diagnostic = "Animation preview Rig session is unavailable";
			return snapshot;
		}
		const RigSessionState& state = found->second;
		snapshot.rigRevision = state.revision;
		if (!context.controller || !context.skeleton || context.skeleton->bones.empty()
			|| !context.controller->GetAnimationRig())
		{
			snapshot.diagnostic = "Animation preview Rig target is unavailable";
			return snapshot;
		}
		snapshot.available = true;
		snapshot.targetSkeletonGuid = context.skeleton->sourceSkeletonGuid;
		snapshot.rigAssetGuid = state.rigAssetGuid;
		snapshot.rigAssetPath = state.rigAssetPath;
		snapshot.retargetEnabled = context.retargetEnabled;
		snapshot.retargetProfilePath = context.retargetProfilePath;
		snapshot.retargetSourceModelPath = context.retargetSourceModelPath;
		snapshot.retargetSourceAnimatorPath = context.retargetSourceAnimatorPath;
		const auto globals = ResolveGlobals(*context.skeleton, *context.controller);
		const auto* compiledRig = context.controller->GetAnimationRig();
		snapshot.sockets.reserve(state.workingAsset.sockets.size());
		for (const auto& definition : state.workingAsset.sockets)
		{
			const int index = compiledRig->FindSocketByGuid(definition.guid);
			if (index < 0 || index >= static_cast<int>(compiledRig->sockets.size()))
				continue;
			const auto& compiled = compiledRig->sockets[static_cast<std::size_t>(index)];
			if (compiled.boneIndex < 0 || compiled.boneIndex >= static_cast<int>(globals.size()))
				continue;
			AnimationPreviewSocketSnapshot item;
			item.guid = definition.guid;
			item.name = definition.name;
			item.boneGuid = definition.boneGuid;
			item.parentBoneIndex = compiled.boneIndex;
			const glm::mat4 local = compiled.localTransform;
			const glm::mat4 model = globals[static_cast<std::size_t>(compiled.boneIndex)] * local;
			item.localTransform = MakeSnapshot(local, RuntimeTransformSpace::Local);
			item.modelTransform = MakeSnapshot(model, RuntimeTransformSpace::Model);
			item.worldTransform = MakeSnapshot(context.ownerWorld * model, RuntimeTransformSpace::World);
			snapshot.sockets.push_back(std::move(item));
		}
		snapshot.attachmentProfiles.reserve(state.workingAsset.attachmentProfiles.size());
		for (const auto& definition : state.workingAsset.attachmentProfiles)
		{
			VansLocalTransform local;
			local.position = definition.positionLocal;
			local.rotation = definition.rotationLocal;
			local.scale = definition.scaleLocal;
			AnimationPreviewAttachmentProfileSnapshot item;
			item.modelGuid = definition.modelGuid;
			item.parentKind = definition.parentKind ==
				VansGraphics::VansRigAttachmentParentKind::Bone
				? RuntimeParentKind::Bone : RuntimeParentKind::Socket;
			item.anchorGuid = definition.anchorGuid;
			item.localTransform = MakeSnapshot(
				local.ToMatrix(), RuntimeTransformSpace::Local);
			snapshot.attachmentProfiles.push_back(std::move(item));
		}
		return snapshot;
	}

	bool AnimationPreviewRigAuthoringService::GetWorkingCanonicalJson(
		AnimationPreviewSessionId sessionId,
		std::string& canonicalJson,
		std::string& error)
	{
		canonicalJson.clear();
		error.clear();
		auto found = Sessions().find(sessionId);
		if (found == Sessions().end())
		{
			error = "Animation preview Rig session is unavailable";
			return false;
		}
		nlohmann::json root;
		if (!VansGraphics::VansAnimationRigStorage::SerializeToJsonObject(
			found->second.workingAsset, root, error))
			return false;
		canonicalJson = root.dump(4);
		return true;
	}

	AnimationPreviewRigEditResult AnimationPreviewRigAuthoringService::SetSocketTransform(
		const AnimationPreviewRigContext& context,
		const AnimationPreviewRigSocketTransformRequest& request)
	{
		AnimationPreviewRigEditResult result;
		auto found = Sessions().find(context.sessionId);
		if (found == Sessions().end())
		{
			result.message = "Animation preview Rig session is unavailable";
			return result;
		}
		RigSessionState& state = found->second;
		result.acceptedRevision = state.revision;
		if (request.expectedRigRevision != state.revision)
		{
			result.message = "Stale Animation Rig edit revision was rejected";
			result.usingLastGoodRig = state.overrideActive;
			return result;
		}
		if (!context.controller || !context.skeleton)
		{
			result.message = "Animation preview Rig target is unavailable";
			return result;
		}
		auto definition = std::find_if(
			state.workingAsset.sockets.begin(), state.workingAsset.sockets.end(),
			[&](const auto& socket) { return socket.guid == request.socketGuid; });
		if (definition == state.workingAsset.sockets.end())
		{
			result.message = "Animation Rig socket does not exist";
			return result;
		}
		const auto bone = context.skeleton->boneGuidToIndex.find(definition->boneGuid);
		if (bone == context.skeleton->boneGuidToIndex.end())
		{
			result.message = "Animation Rig socket parent bone is not in the target Skeleton";
			return result;
		}
		glm::mat4 desired;
		if (!MakeMatrix(request.transform, desired, result.message))
			return result;
		const auto globals = ResolveGlobals(*context.skeleton, *context.controller);
		glm::mat4 parent = globals[static_cast<std::size_t>(bone->second)];
		if (request.space == RuntimeTransformSpace::World)
			parent = context.ownerWorld * parent;
		const glm::mat4 localMatrix = request.space == RuntimeTransformSpace::Local
			? desired : glm::inverse(parent) * desired;
		VansLocalTransform local;
		if (!VansLocalTransform::TryFromMatrix(localMatrix, local))
		{
			result.message = "Socket transform cannot be represented as Local TRS without shear";
			result.usingLastGoodRig = true;
			return result;
		}
		auto candidateAsset = state.workingAsset;
		auto candidate = std::find_if(
			candidateAsset.sockets.begin(), candidateAsset.sockets.end(),
			[&](const auto& socket) { return socket.guid == request.socketGuid; });
		candidate->positionLocal = local.position;
		candidate->rotationLocal = local.rotation;
		candidate->scaleLocal = local.scale;
		VansGraphics::VansCompiledAnimationRig candidateRig;
		if (!VansGraphics::VansAnimationRigCompiler::Compile(
			candidateAsset, *context.skeleton, candidateRig, result.message)
			|| !context.controller->ReplaceAnimationRig(std::move(candidateRig), result.message))
		{
			result.usingLastGoodRig = true;
			return result;
		}
		state.workingAsset = std::move(candidateAsset);
		state.revision++;
		state.overrideActive = true;
		result.success = true;
		result.acceptedRevision = state.revision;
		result.message = "Animation Rig socket preview updated";
		return result;
	}

	AnimationPreviewRigEditResult AnimationPreviewRigAuthoringService::SetAttachmentProfile(
		const AnimationPreviewRigContext& context,
		const AnimationPreviewRigAttachmentProfileRequest& request)
	{
		AnimationPreviewRigEditResult result;
		auto found = Sessions().find(context.sessionId);
		if (found == Sessions().end())
		{
			result.message = "Animation preview Rig session is unavailable";
			return result;
		}
		RigSessionState& state = found->second;
		result.acceptedRevision = state.revision;
		if (request.expectedRigRevision != state.revision)
		{
			result.message = "Stale Animation Rig attachment profile revision was rejected";
			result.usingLastGoodRig = state.overrideActive;
			return result;
		}
		if (!context.controller || !context.skeleton)
		{
			result.message = "Animation preview Rig target is unavailable";
			return result;
		}
		if (request.modelGuid.empty() || request.anchorGuid.empty()
			|| (request.parentKind != RuntimeParentKind::Bone
				&& request.parentKind != RuntimeParentKind::Socket))
		{
			result.message = "Attachment profile requires a Model and a Bone/Socket anchor";
			return result;
		}
		const bool anchorExists = request.parentKind == RuntimeParentKind::Bone
			? context.skeleton->boneGuidToIndex.find(request.anchorGuid)
				!= context.skeleton->boneGuidToIndex.end()
			: std::any_of(state.workingAsset.sockets.begin(), state.workingAsset.sockets.end(),
				[&](const auto& socket) { return socket.guid == request.anchorGuid; });
		if (!anchorExists)
		{
			result.message = "Attachment profile anchor does not exist in the target Rig";
			return result;
		}
		auto matches = [&](const VansGraphics::VansRigAttachmentProfileDefinition& profile)
		{
			const auto parentKind = request.parentKind == RuntimeParentKind::Bone
				? VansGraphics::VansRigAttachmentParentKind::Bone
				: VansGraphics::VansRigAttachmentParentKind::Socket;
			return profile.modelGuid == request.modelGuid
				&& profile.parentKind == parentKind
				&& profile.anchorGuid == request.anchorGuid;
		};
		auto candidateAsset = state.workingAsset;
		auto profile = std::find_if(candidateAsset.attachmentProfiles.begin(),
			candidateAsset.attachmentProfiles.end(), matches);
		if (request.remove)
		{
			if (profile == candidateAsset.attachmentProfiles.end())
			{
				result.message = "Attachment profile does not exist";
				return result;
			}
			candidateAsset.attachmentProfiles.erase(profile);
		}
		else
		{
			if (request.localTransform.space != RuntimeTransformSpace::Local)
			{
				result.message = "Attachment profile Transform must be authored in Local space";
				return result;
			}
			glm::mat4 localMatrix;
			if (!MakeMatrix(request.localTransform, localMatrix, result.message))
				return result;
			VansLocalTransform local;
			if (!VansLocalTransform::TryFromMatrix(localMatrix, local))
			{
				result.message = "Attachment profile cannot be represented as Local TRS without shear";
				return result;
			}
			if (profile == candidateAsset.attachmentProfiles.end())
			{
				VansGraphics::VansRigAttachmentProfileDefinition definition;
				definition.modelGuid = request.modelGuid;
				definition.parentKind = request.parentKind == RuntimeParentKind::Bone
					? VansGraphics::VansRigAttachmentParentKind::Bone
					: VansGraphics::VansRigAttachmentParentKind::Socket;
				definition.anchorGuid = request.anchorGuid;
				candidateAsset.attachmentProfiles.push_back(std::move(definition));
				profile = std::prev(candidateAsset.attachmentProfiles.end());
			}
			profile->positionLocal = local.position;
			profile->rotationLocal = local.rotation;
			profile->scaleLocal = local.scale;
		}
		VansGraphics::VansCompiledAnimationRig candidateRig;
		if (!VansGraphics::VansAnimationRigCompiler::Compile(
			candidateAsset, *context.skeleton, candidateRig, result.message)
			|| !context.controller->ReplaceAnimationRig(std::move(candidateRig), result.message))
		{
			result.usingLastGoodRig = state.overrideActive;
			return result;
		}
		state.workingAsset = std::move(candidateAsset);
		state.revision++;
		state.overrideActive = true;
		result.success = true;
		result.acceptedRevision = state.revision;
		result.message = request.remove
			? "Animation Rig attachment profile removed"
			: "Animation Rig attachment profile updated";
		return result;
	}

	AnimationPreviewRigEditResult AnimationPreviewRigAuthoringService::Adopt(
		const AnimationPreviewRigAdoptRequest& request,
		VansGraphics::VansAnimationController& controller)
	{
		AnimationPreviewRigEditResult result;
		auto found = Sessions().find(request.sessionId);
		if (found == Sessions().end())
		{
			result.message = "Animation preview Rig session is unavailable";
			return result;
		}
		result.acceptedRevision = found->second.revision;
		if (request.expectedRigRevision != found->second.revision)
		{
			result.message = "Animation Rig adoption revision does not match the preview";
			return result;
		}
		const auto* compiledRig = controller.GetAnimationRig();
		if (!compiledRig)
		{
			result.message = "Animation Rig adoption target is unavailable";
			return result;
		}
		found->second.originalCompiledRig = *compiledRig;
		found->second.revision = 0;
		found->second.overrideActive = false;
		result.success = true;
		result.acceptedRevision = 0;
		result.message = "Animation Rig preview adopted";
		return result;
	}

	bool AnimationPreviewRigAuthoringService::EndSession(
		AnimationPreviewSessionId sessionId,
		VansGraphics::VansAnimationController* controller,
		std::string& error)
	{
		error.clear();
		auto found = Sessions().find(sessionId);
		if (found == Sessions().end())
			return true;
		bool success = true;
		if (controller && found->second.overrideActive
			&& found->second.originalCompiledRig)
		{
			success = controller->ReplaceAnimationRig(
				*found->second.originalCompiledRig, error);
		}
		Sessions().erase(found);
		return success;
	}
}
