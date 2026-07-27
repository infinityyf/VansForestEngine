#include "VansSceneAnimationComponentBuilder.h"

#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../AnimationCore/VansAnimatorIO.h"
#include "../../AnimationCore/VansAnimGraph.h"
#include "../../AnimationCore/VansAnimationClipLoader.h"
#include "../../AnimationCore/VansBoneAttachmentSystem.h"
#include "../../PhysicsCore/VansCharacterControllerNode.h"
#include "../../PhysicsCore/Storage/VansRagdollProfileStorage.h"
#include "../../PhysicsCore/VansRagdollSystem.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"

namespace VansGraphics
{
	namespace
	{
		VansEngine::PhysicsColliderType ParseShapeType(const std::string& value)
		{
			if (value == "box") return VansEngine::PhysicsColliderType::Box;
			if (value == "sphere") return VansEngine::PhysicsColliderType::Sphere;
			if (value == "capsule") return VansEngine::PhysicsColliderType::Capsule;
			if (value == "mesh") return VansEngine::PhysicsColliderType::Mesh;
			if (value == "convex") return VansEngine::PhysicsColliderType::ConvexMesh;
			return VansEngine::PhysicsColliderType::Capsule;
		}

		VansEngine::RagdollDriveMode ParseRagdollDriveMode(const std::string& value)
		{
			if (value == "physics") return VansEngine::RagdollDriveMode::Physics;
			if (value == "blend") return VansEngine::RagdollDriveMode::Blend;
			return VansEngine::RagdollDriveMode::Animation;
		}
	}

	void VansSceneAnimationComponentBuilder::AddAnimationPlaceholder(
		VansScriptObject& object,
		const Vans::VansSceneAnimationComponentConfig& animationConfig,
		std::vector<PendingAnimationComponent>& pendingAnimations)
	{
		auto* animationComponent = new VansScriptAnimationComponent();
		animationComponent->m_ComponentName = "animation";
		object.AddComponent(animationComponent);

		PendingAnimationComponent pending;
		pending.obj = &object;
		pending.component = animationComponent;
		pending.animationConfig = std::make_shared<Vans::VansSceneAnimationComponentConfig>(animationConfig);
		pending.objectName = object.m_ObjectName;
		pendingAnimations.push_back(std::move(pending));
	}

	VansAnimationNode* VansSceneAnimationComponentBuilder::LoadAnimationComponent(
		VansScene& scene,
		const Vans::VansSceneAnimationComponentConfig& animConfig,
		const std::string& objectName,
		const std::string& projectRoot)
	{
		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice device = vkDevice->GetLogicDevice();

		const std::string& meshGroupName = animConfig.meshGroup;
		const std::string& animatorPath = animConfig.animator;
		const std::string& externClips = animConfig.externClips;
		const bool enableRootMotion = animConfig.rootMotion;
		const std::string& rootBone = animConfig.rootBone;
		const std::string nodeName = animConfig.name.empty() ? objectName : animConfig.name;

		if (meshGroupName.empty())
		{
			VANS_LOG_WARN("[LoadAnimComp] animation component on '" << objectName << "' has no mesh_group, skipping");
			return nullptr;
		}

		MultiMeshGroup* animationGroup = scene.FindAnimationMultiMeshGroup(meshGroupName, objectName);
		if (!animationGroup)
		{
			VANS_LOG_WARN("[LoadAnimComp] mesh_group '" << meshGroupName << "' not found for object '" << objectName << "'");
			return nullptr;
		}

		MultiMeshGroup& group = *animationGroup;
		if (group.childNodes.empty())
			return nullptr;

		VansMesh* meshAsset = group.sourceMesh;
		for (auto* asset : scene.GetMeshAssets())
		{
			if (meshAsset == nullptr && asset->m_AssetName == meshGroupName)
			{
				meshAsset = dynamic_cast<VansMesh*>(asset);
				break;
			}
		}

		if (!meshAsset)
		{
			VANS_LOG_WARN("[LoadAnimComp] mesh_group '" << meshGroupName
				<< "' has no source mesh. Skipping '" << objectName << "'");
			return nullptr;
		}

		if (meshAsset->m_AnimImportResult.skeleton.bones.empty())
		{
			VANS_LOG_WARN("[LoadAnimComp] mesh_group '" << meshGroupName
				<< "' has no skeleton. Skipping '" << objectName << "'");
			return nullptr;
		}

		VansAnimationController* controller = nullptr;
		if (!animatorPath.empty())
		{
			const std::string fullAnimatorPath = projectRoot + animatorPath;

			AnimatorAssetData assetData;
			if (!VansAnimatorIO::Load(fullAnimatorPath, assetData))
			{
				VANS_LOG_WARN("[LoadAnimComp] Failed to load .vanimator: " << fullAnimatorPath);
				return nullptr;
			}

			auto clipsMap = VansAnimationClipLoader::LoadClipsFromRefs(
				assetData.clipRefs,
				projectRoot,
				&meshAsset->m_AnimImportResult.skeleton);

			controller = new VansAnimationController();
			controller->SetName(assetData.name);

			for (const auto& param : assetData.parameters)
			{
				controller->AddParameter(param.name, param.type);
				switch (param.type)
				{
				case AnimatorParamType::Float: controller->SetFloat(param.name, param.floatVal); break;
				case AnimatorParamType::Bool: controller->SetBool(param.name, param.boolVal); break;
				case AnimatorParamType::Int: controller->SetInt(param.name, param.intVal); break;
				case AnimatorParamType::Trigger: break;
				case AnimatorParamType::Vector3: controller->SetVector3(param.name, param.vec3Val); break;
				case AnimatorParamType::Quaternion: controller->SetQuaternion(param.name, param.quatVal); break;
				}
			}

			for (auto& [name, clip] : clipsMap)
				controller->AddClip(name, std::move(clip));

			if (assetData.animGraph)
				controller->SetGraph(std::move(assetData.animGraph));
			else
				VANS_LOG_WARN("[LoadAnimComp] .vanimator file has no graph node: " << fullAnimatorPath);

			VANS_LOG("[LoadAnimComp] Loaded controller from .vanimator: " << fullAnimatorPath);
		}
		else
		{
			controller = new VansAnimationController();
			controller->SetName(meshGroupName + "_Controller");

			bool usedExternClips = false;
			if (!externClips.empty())
			{
				const std::string fullExternPath = projectRoot + externClips;
				std::vector<VansAnimationClip> extClips;
				if (VansAnimationClipLoader::ExtractClipsFromFBX(
					fullExternPath,
					meshAsset->m_AnimImportResult.skeleton,
					extClips))
				{
					for (auto& extClip : extClips)
						controller->AddClip(extClip.clipName, std::move(extClip));

					usedExternClips = true;
					VANS_LOG("[LoadAnimComp] Loaded " << extClips.size()
						<< " extern clip(s) from: " << fullExternPath);
				}
				else
				{
					VANS_LOG_WARN("[LoadAnimComp] Failed to extract clips from: " << fullExternPath);
				}
			}

			if (!usedExternClips)
			{
				for (auto& clip : meshAsset->m_AnimImportResult.clips)
					controller->AddClip(clip.clipName, clip);
			}

			auto clipNames = controller->GetClipNames();
			auto smNode = std::make_unique<AnimGraphStateMachineNode>();
			smNode->m_DefaultStateName = clipNames.empty() ? "" : clipNames.front();
			smNode->m_CurrentStateName = smNode->m_DefaultStateName;
			for (const auto& clipName : clipNames)
			{
				AnimatorState state;
				state.name = clipName;
				state.clipName = clipName;
				state.speed = 1.0f;
				state.loop = true;
				state.rootMotion = enableRootMotion;
				smNode->m_States.push_back(state);
			}

			auto graph = std::make_unique<VansAnimGraph>();
			const int smId = graph->AddNode(std::move(smNode));
			const int outId = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
			graph->AddLink(smId, 0, outId, 0);
			controller->SetGraph(std::move(graph));

			VANS_LOG("[LoadAnimComp] Auto-generated graph controller for '" << meshGroupName
				<< "' with " << clipNames.size() << " clip(s)");
		}

		if (enableRootMotion)
			controller->EnableRootMotion(true);

		if (animConfig.motionMatching)
		{
			MotionMatchingSettings mmSettings = *animConfig.motionMatching;
			controller->ConfigureMotionMatching(mmSettings);
			VANS_LOG("[LoadAnimComp] Motion Matching configured for '" << objectName
				<< "' enabled=" << mmSettings.enabled
				<< " externallyDriven=" << mmSettings.externallyDriven
				<< " sampleRate=" << mmSettings.sampleRate
				<< " searchThrottle=" << mmSettings.searchThrottle
				<< " speedScale=" << mmSettings.desiredSpeedScale
				<< " speedMatching=" << mmSettings.enableSpeedMatching
				<< " playbackRate=[" << mmSettings.minPlaybackRate << "," << mmSettings.maxPlaybackRate << "]"
				<< " searchGroups=" << mmSettings.searchGroups.size());
		}

		if (animConfig.footPlacement)
		{
			FootPlacementSettings fpSettings = *animConfig.footPlacement;
			controller->ConfigureFootPlacement(fpSettings, meshAsset->m_AnimImportResult.skeleton);
			controller->SetFootPlacementEnabled(fpSettings.enabled);
			VANS_LOG("[LoadAnimComp] FootPlacement configured for '" << objectName
				<< "' enabled=" << fpSettings.enabled
				<< " poseRelative=true"
				<< " probeHeight=" << fpSettings.probeOriginHeight
				<< " probeLength=" << fpSettings.probeLength);
		}

		VansAnimationNode* animNode = new VansAnimationNode(nodeName);
		animNode->SetSkeleton(meshAsset->m_AnimImportResult.skeleton);
		animNode->SetRenderNodes(group.childNodes);
		animNode->InitGPUResources(device, 1);
		animNode->UploadPerSubmeshBoneBuffers(meshAsset->m_SubMeshBoneData);
		animNode->SetTransformID(group.sharedTransformID);
		animNode->SetController(controller);

		if (!animatorPath.empty())
			animNode->SetAnimatorFilePath(projectRoot + animatorPath);

		if (!rootBone.empty())
			animNode->SetRootBone(rootBone);

		for (size_t ci = 0; ci < group.childNodes.size(); ci++)
		{
			VansRenderNode* childNode = group.childNodes[ci];
			const uint32_t submeshIndex = childNode->m_SubmeshIndex != UINT32_MAX
				? childNode->m_SubmeshIndex
				: static_cast<uint32_t>(ci);
			childNode->m_HasSkeletonBone = true;
			childNode->m_AnimationEnabled = true;
			childNode->m_AnimOwner = animNode;
			childNode->m_AnimSubmeshIndex = submeshIndex;
			if (submeshIndex < animNode->GetSubmeshBufferCount())
			{
				childNode->m_AnimBoneIDBuffer = &animNode->GetBoneIDBuffer(submeshIndex);
				childNode->m_AnimBoneWeightBuffer = &animNode->GetBoneWeightBuffer(submeshIndex);
				childNode->MarkAnimationDescriptorDirty();
			}
			else
			{
				childNode->m_AnimationEnabled = false;
				childNode->m_AnimBoneIDBuffer = nullptr;
				childNode->m_AnimBoneWeightBuffer = nullptr;
				VANS_LOG_WARN("[LoadAnimComp] submesh index " << submeshIndex
					<< " has no bone buffer for node '" << childNode->m_NodeName << "'");
			}
		}

		scene.RegisterAnimationRuntime(animNode, controller);
		controller->Play();

		if (!animConfig.boneBindings.empty())
		{
			VansEngine::BoneColliderBindingSet bindingSet;
			bindingSet.animNode = animNode;

			const Skeleton& skeleton = meshAsset->m_AnimImportResult.skeleton;
			for (const Vans::VansSceneAnimationBoneBindingConfig& bindingConfig : animConfig.boneBindings)
			{
				VansEngine::BoneColliderBinding binding;
				binding.boneName = bindingConfig.boneName;
				binding.physicsObjectName = bindingConfig.physicsObjectName;
				binding.offsetPosition = bindingConfig.offsetPosition;
				binding.offsetRotation = bindingConfig.offsetRotation;
				binding.offsetScale = bindingConfig.offsetScale;
				binding.syncRotation = bindingConfig.syncRotation;
				binding.syncScale = bindingConfig.syncScale;
				binding.layerName = bindingConfig.layerName;
				binding.isTrigger = bindingConfig.isTrigger;
				binding.enabled = bindingConfig.enabled;
				binding.autoCreateNode = bindingConfig.autoCreateNode;
				binding.shapeExtents = bindingConfig.shapeExtents;
				binding.shapeType = ParseShapeType(bindingConfig.shapeType);

				auto boneIt = skeleton.boneNameToIndex.find(binding.boneName);
				if (boneIt != skeleton.boneNameToIndex.end())
					binding.boneIndex = boneIt->second;
				else
					VANS_LOG_WARN("[LoadAnimComp] bone binding references missing bone '" << binding.boneName << "'");

				if (!binding.physicsObjectName.empty())
				{
					for (auto* physicsNode : scene.GetPhysicsNodes())
					{
						if (physicsNode && physicsNode->GetName() == binding.physicsObjectName)
						{
							binding.physicsNode = physicsNode;
							binding.attachmentTransformID = physicsNode->GetTransformID();
							binding.ownsAttachmentTransform = false;
							break;
						}
					}

					if (binding.physicsNode == nullptr)
					{
						VansScriptObject* physicsObject = scene.FindSceneObjectByName(binding.physicsObjectName);
						auto* physicsComp = physicsObject ? physicsObject->GetComponent<VansScriptPhysicsComponent>() : nullptr;
						if (physicsComp && physicsComp->m_PhysicsNode)
						{
							binding.physicsNode = physicsComp->m_PhysicsNode;
							binding.attachmentTransformID = binding.physicsNode->GetTransformID();
							binding.ownsAttachmentTransform = false;
						}
					}

					if (binding.physicsNode == nullptr)
					{
						VANS_LOG_WARN("[LoadAnimComp] bone binding physics object not found: " << binding.physicsObjectName);
					}
					else if (binding.physicsNode->GetProperties().bodyType != VansEngine::PhysicsBodyType::Kinematic &&
						!binding.physicsNode->GetProperties().isTrigger)
					{
						VANS_LOG_WARN("[LoadAnimComp] bone binding physics object '" << binding.physicsObjectName
							<< "' is not kinematic/trigger; PhysX may override its transform");
					}
				}

				if (binding.attachmentTransformID == UINT32_MAX)
				{
					binding.attachmentTransformID = VansTransformStore::AllocateTransform();
					binding.ownsAttachmentTransform = true;
				}

				bindingSet.bindings.push_back(std::move(binding));
			}

			VansEngine::VansBoneAttachmentSystem::GetInstance().RegisterBindingSet(std::move(bindingSet));
			VANS_LOG("[LoadAnimComp] Registered " << animConfig.boneBindings.size()
				<< " bone collider binding(s) for '" << nodeName << "'");
		}

		VANS_LOG("[LoadAnimComp] Created animation component '" << nodeName
			<< "' with " << controller->GetClipNames().size() << " clip(s), "
			<< meshAsset->m_AnimImportResult.skeleton.bones.size() << " bones, "
			<< group.childNodes.size() << " render node(s)");

		return animNode;
	}

	bool VansSceneAnimationComponentBuilder::LoadRagdollComponent(
		VansScene& scene,
		VansScriptObject* obj,
		VansAnimationNode* animNode,
		const Vans::VansSceneRagdollComponentConfig& ragdollConfig,
		const std::string& projectRoot)
	{
		if (obj == nullptr || animNode == nullptr)
			return false;

		const std::string& profilePath = ragdollConfig.profile;
		if (profilePath.empty())
		{
			VANS_LOG_WARN("[LoadRagdollComp] object '" << obj->m_ObjectName << "' ragdoll missing profile");
			return false;
		}

		const std::string fullProfilePath = projectRoot + profilePath;
		VansEngine::RagdollProfile profile;
		std::string error;
		if (!VansEngine::VansRagdollProfileStorage::Load(fullProfilePath, profile, error))
		{
			VANS_LOG_WARN("[LoadRagdollComp] failed to load profile: " << fullProfilePath << " (" << error << ")");
			return false;
		}

		VansAnimationController* controller = animNode->GetController();
		if (controller == nullptr)
			return false;

		if (controller->GetCachedGlobalTransforms().empty())
			controller->Update(0.0f, animNode->GetSkeleton());

		if (controller->GetCachedGlobalTransforms().empty())
		{
			VANS_LOG_WARN("[LoadRagdollComp] controller did not produce bind pose for '" << obj->m_ObjectName << "'");
			return false;
		}

		if (VansEngine::VansBoneAttachmentSystem::GetInstance().FindBindingSet(animNode) != nullptr)
		{
			VANS_LOG_WARN("[LoadRagdollComp] object '" << obj->m_ObjectName
				<< "' has both bone_bindings and ragdoll configured; avoid binding the same bone in Physics/Blend mode");
		}

		if (!VansEngine::VansRagdollSystem::GetInstance().CreateRagdoll(animNode, profile))
			return false;

		const VansEngine::RagdollDriveMode mode = ParseRagdollDriveMode(ragdollConfig.driveMode);
		const float blendWeight = ragdollConfig.blendWeight;

		VansEngine::VansRagdollSystem::GetInstance().SetBlendWeight(animNode, blendWeight);
		VansEngine::VansRagdollSystem::GetInstance().SetDriveMode(animNode, mode);

		auto* ragdollComp = new VansScriptRagdollComponent();
		ragdollComp->m_AnimNode = animNode;
		ragdollComp->m_InitialDriveMode = mode;
		ragdollComp->m_ProfilePath = profilePath;
		ragdollComp->m_ProfileName = profile.name;
		ragdollComp->m_ConfiguredBodyCount = static_cast<int>(profile.bodies.size());
		ragdollComp->m_ConfiguredJointCount = static_cast<int>(profile.joints.size());
		obj->AddComponent(ragdollComp);

		auto* cctComp = obj->GetComponent<VansScriptCharacterControllerComponent>();
		if (cctComp && cctComp->m_ControllerNode &&
			cctComp->m_ControllerNode->HasPendingFollowRagdoll())
		{
			cctComp->m_ControllerNode->SetFollowRagdoll(
				animNode,
				cctComp->m_ControllerNode->GetPendingFollowRagdollBone());
			cctComp->m_ControllerNode->ConsumePendingFollowRagdoll();
			VANS_LOG("[LoadRagdollComp] CCT followRagdoll binding completed, objName='" << obj->m_ObjectName
				<< "' bone='" << cctComp->m_ControllerNode->GetFollowRagdollBone() << "'");
		}

		VANS_LOG("[LoadRagdollComp] Created ragdoll component for '" << obj->m_ObjectName
			<< "' profile='" << profile.name << "' bodies=" << profile.bodies.size());
		return true;
	}

	void VansSceneAnimationComponentBuilder::ResolveAnimations(
		VansScene& scene,
		const std::vector<PendingAnimationComponent>& pendingAnimations,
		const std::string& projectRoot)
	{
		for (const PendingAnimationComponent& pending : pendingAnimations)
		{
			if (!pending.component || !pending.animationConfig)
				continue;

			VansAnimationNode* animationNode = LoadAnimationComponent(
				scene,
				*pending.animationConfig,
				pending.objectName,
				projectRoot);
			pending.component->m_AnimNode = animationNode;

			if (animationNode)
			{
				const bool animationEnabled = pending.animationConfig->enabled;
				if (!animationEnabled)
					animationNode->SetEnabled(false);
				pending.component->m_Enabled = animationEnabled;
			}

			if (!animationNode)
			{
				VANS_LOG_WARN("[LoadSceneObjects] Animation component for '"
					<< pending.objectName << "' could not be created");
			}
			else if (pending.obj && pending.animationConfig->ragdoll)
			{
				LoadRagdollComponent(
					scene,
					pending.obj,
					animationNode,
					*pending.animationConfig->ragdoll,
					projectRoot);
			}
		}
	}
}
