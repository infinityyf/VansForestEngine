#include "VansSceneAnimationComponentBuilder.h"

#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../AnimationCore/VansAnimatorIO.h"
#include "../../AnimationCore/VansAnimatorRuntimeCompiler.h"
#include "../../AnimationCore/VansAnimGraph.h"
#include "../../AnimationCore/VansAnimationClipLoader.h"
#include "../../AnimationCore/VansSkinnedMeshLoader.h"
#include "../../AssetCore/VansAssetMeta.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../PhysicsCore/VansCharacterControllerNode.h"
#include "../../PhysicsCore/VansRagdollSystem.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <filesystem>
#include <memory>

namespace VansGraphics
{
	namespace
	{
		uint32_t CountVerticesWithBoneInfluence(const std::vector<VertexBoneData>& boneData)
		{
			uint32_t count = 0;
			for (const VertexBoneData& vertex : boneData)
			{
				for (int influence = 0; influence < MAX_BONE_INFLUENCE; ++influence)
				{
					if (vertex.boneIDs[influence] >= 0 && vertex.weights[influence] > 0.0f)
					{
						++count;
						break;
					}
				}
			}
			return count;
		}

		void ApplyRetargetProfile(
			const VansRetargetProfileAsset& profile,
			VansRetargetRuntimeDesc& desc,
			const std::string& profileGuid)
		{
			desc.profileAssetGuid = profileGuid;
			desc.translationScaleMode = profile.translationScaleMode;
			desc.translationScale = profile.explicitTranslationScale;
			desc.rootAlignment = profile.rootAlignment;
			desc.targetModelSpaceAlignment = profile.targetModelSpaceAlignment;
			desc.limbChains = profile.limbChains;
		}

		template <typename T>
		std::shared_ptr<const T> ResolveConfigAssetObject(
			const std::string& guidText,
			Vans::VansAssetType expectedType,
			const std::string& label,
			std::string& error)
		{
			Vans::VansAssetGuid guid;
			if (!Vans::VansAssetGuid::TryParse(guidText, guid))
			{
				error = label + " has an invalid asset GUID '" + guidText + "'";
				return nullptr;
			}
			Vans::VansAssetObjectSnapshotInfo info;
			const Vans::VansAssetObjectRepository& repository =
				Vans::VansProjectManager::Get().GetAssetObjectRepository();
			if (!repository.FindInfo(guid, info) || info.assetType != expectedType)
			{
				error = label + " is not loaded in the object repository: " + guidText;
				return nullptr;
			}
			auto object = repository.ResolveLatest<T>(guid);
			if (!object)
				error = label + " decoded object type does not match its repository entry";
			return object;
		}

		bool ResolveAnimationAssetPath(
			const std::string& guidText,
			Vans::VansAssetType expectedType,
			const std::string& label,
			const std::string& pathHint,
			std::filesystem::path& outPath,
			std::string& error)
		{
			outPath.clear();
			error.clear();
			Vans::VansAssetGuid guid;
			if (!Vans::VansAssetGuid::TryParse(guidText, guid))
			{
				error = label + " has an invalid asset GUID '" + guidText + "'";
				return false;
			}
			const auto record = Vans::VansProjectManager::Get().FindAssetRecord(guid);
			if (!record)
			{
				error = label + " cannot resolve asset GUID '" + guidText + "' (hint: '" + pathHint + "')";
				return false;
			}
			if (record->type != expectedType)
			{
				error = label + " GUID '" + guidText + "' resolves to the wrong asset type";
				return false;
			}
			if (record->state == Vans::VansAssetState::Missing)
			{
				error = label + " GUID '" + guidText + "' is marked missing";
				return false;
			}
			outPath = !record->artifactPath.empty() ? record->artifactPath : record->sourcePath;
			if (outPath.is_relative() && Vans::VansProjectManager::Get().IsProjectLoaded())
			{
				const std::filesystem::path projectRelative =
					std::filesystem::path(Vans::VansProjectManager::Get().GetProjectRootPath()) / outPath;
				if (std::filesystem::exists(projectRelative))
					outPath = projectRelative.lexically_normal();
			}
			if (outPath.empty() || !std::filesystem::exists(outPath))
			{
				error = label + " GUID '" + guidText + "' has no readable runtime asset (hint: '" + pathHint + "')";
				outPath.clear();
				return false;
			}
			return true;
		}

		bool LoadSkeletonFromModel(
			const std::string& modelGuid,
			const std::string& fullModelPath,
			Skeleton& outSkeleton)
		{
			std::string error;
			const auto meta = ResolveConfigAssetObject<Vans::VansAssetMeta>(
				modelGuid, Vans::VansAssetType::Model, "Skeleton Model", error);
			if (!meta)
			{
				VANS_LOG_WARN("[Retarget] failed to resolve source Skeleton metadata: "
					<< modelGuid << " (" << error << ")");
				return false;
			}
			if (!VansSkinnedMeshLoader::LoadSkeletonFromModelAsset(
				fullModelPath, Vans::ReadSkeletalMeshImportSettings(*meta), outSkeleton, error))
			{
				VANS_LOG_WARN("[Retarget] failed to load source Skeleton asset: "
					<< fullModelPath << " (" << error << ")");
				return false;
			}
			return true;
		}

		std::unique_ptr<VansAnimationController> LoadAnimatorController(
			const std::string& animatorGuid,
			const Skeleton& skeleton,
			const MotionMatchingSettings* motionMatchingSettings,
			bool enableRootMotion,
			const std::string& logOwner)
		{
			std::string animatorError;
			const auto assetData = ResolveConfigAssetObject<AnimatorAssetData>(
				animatorGuid,
				Vans::VansAssetType::AnimatorController,
				"Animator",
				animatorError);
			if (!assetData)
			{
				VANS_LOG_WARN("[Retarget] source Animator is unavailable for '"
					<< logOwner << "': " << animatorError);
				return nullptr;
			}

			std::string compileError;
			VansAnimatorRuntimeCompileOptions options;
			options.enableTargetPostProcess = false;
			options.enableRootMotion = enableRootMotion;
			options.rigResolver = [](const std::string& guid, std::string& error)
			{
				return ResolveConfigAssetObject<VansAnimationRigAsset>(guid,
					Vans::VansAssetType::AnimationRig, "Animation Rig", error);
			};
			options.queryProfileResolver = [](const std::string& profile, std::uint32_t& mask, std::string& error)
			{
				return Vans::VansProjectManager::Get().GetProjectSettings()
					.ResolvePhysicsQueryProfile(profile, mask, error);
			};
			auto controller = VansAnimatorRuntimeCompiler::Compile(
				*assetData,
				skeleton,
				[](const AnimatorClipRef& ref,
					std::shared_ptr<const VansAnimationClipAsset>& clip,
					std::string& error)
				{
					clip = ResolveConfigAssetObject<VansAnimationClipAsset>(ref.assetGuid,
						Vans::VansAssetType::AnimationClip,
						"Animation Clip '" + ref.name + "'", error);
					return clip != nullptr;
				},
				[](const VansAnimationLayerDefinition& layer,
					std::shared_ptr<const VansBoneMaskAsset>& mask, std::string& error)
				{
					mask = ResolveConfigAssetObject<VansBoneMaskAsset>(layer.maskGuid,
						Vans::VansAssetType::BoneMask,
						"Bone Mask for Layer '" + layer.name + "'", error);
					return mask != nullptr;
				},
				options,
				compileError);
			if (!controller)
			{
				VANS_LOG_WARN("[Retarget] source Layer Stack failed for '" << logOwner
					<< "': " << compileError);
				return nullptr;
			}

			if (motionMatchingSettings)
			{
				std::string motionMatchingError;
				if (!controller->ConfigureMotionMatching(*motionMatchingSettings, motionMatchingError))
				{
					VANS_LOG_WARN("[Retarget] invalid Motion Matching configuration for '"
						<< logOwner << "': " << motionMatchingError);
					return nullptr;
				}
			}

			VANS_LOG("[Retarget] loaded source controller for '" << logOwner
				<< "': " << animatorGuid
				<< " clips=" << controller->GetClipNames().size());
			return controller;
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
		const std::string& animatorGuid = animConfig.animatorGuid;
		const std::string& externClips = animConfig.externClips;
		const bool enableRootMotion = animConfig.rootMotion;
		const std::string& rootBone = animConfig.rootBone;
		const std::string nodeName = animConfig.name.empty() ? objectName : animConfig.name;
		const bool retargetRequested = animConfig.retarget && animConfig.retarget->enabled;

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

		const bool hasSkeleton = !meshAsset->m_AnimImportResult.skeleton.bones.empty();
		bool hasNodeTransformClips = false;
		for (const VansAnimationClip& clip : meshAsset->m_AnimImportResult.clips)
		{
			if (!clip.nodeTransformChannels.empty())
			{
				hasNodeTransformClips = true;
				break;
			}
		}

		if (!hasSkeleton && !hasNodeTransformClips)
		{
			VANS_LOG_WARN("[LoadAnimComp] mesh_group '" << meshGroupName
				<< "' has no skeleton or node transform animation. Skipping '" << objectName << "'");
			return nullptr;
		}

		VansAnimationController* controller = nullptr;
		if (!animatorGuid.empty())
		{
			std::string resolveError;
			const auto assetData = ResolveConfigAssetObject<AnimatorAssetData>(
				animatorGuid,
				Vans::VansAssetType::AnimatorController,
				"Animator",
				resolveError);
			if (!assetData)
			{
				VANS_LOG_WARN("[LoadAnimComp] " << resolveError);
				return nullptr;
			}

			std::string compileError;
			VansAnimatorRuntimeCompileOptions options;
			options.mode = retargetRequested
				? VansAnimatorRuntimeCompileMode::ExternalPoseTarget
				: VansAnimatorRuntimeCompileMode::FullGraph;
			options.enableTargetPostProcess = true;
			options.enableRootMotion = enableRootMotion && !retargetRequested;
			options.animationRigGuidOverride = animConfig.rigGuid;
			options.rigResolver = [](const std::string& guid, std::string& error)
			{
				return ResolveConfigAssetObject<VansAnimationRigAsset>(guid,
					Vans::VansAssetType::AnimationRig, "Animation Rig", error);
			};
			options.queryProfileResolver = [](const std::string& profile, std::uint32_t& mask, std::string& error)
			{
				return Vans::VansProjectManager::Get().GetProjectSettings()
					.ResolvePhysicsQueryProfile(profile, mask, error);
			};
			auto compiledController = VansAnimatorRuntimeCompiler::Compile(
				*assetData,
				meshAsset->m_AnimImportResult.skeleton,
				[](const AnimatorClipRef& ref,
					std::shared_ptr<const VansAnimationClipAsset>& clip,
					std::string& error)
				{
					clip = ResolveConfigAssetObject<VansAnimationClipAsset>(ref.assetGuid,
						Vans::VansAssetType::AnimationClip,
						"Animation Clip '" + ref.name + "'", error);
					return clip != nullptr;
				},
				[](const VansAnimationLayerDefinition& layer,
					std::shared_ptr<const VansBoneMaskAsset>& mask, std::string& error)
				{
					mask = ResolveConfigAssetObject<VansBoneMaskAsset>(layer.maskGuid,
						Vans::VansAssetType::BoneMask,
						"Bone Mask for Layer '" + layer.name + "'", error);
					return mask != nullptr;
				},
				options,
				compileError);
			if (!compiledController)
			{
				VANS_LOG_WARN("[LoadAnimComp] Failed to compile Animator runtime definition: " << compileError);
				return nullptr;
			}
			controller = compiledController.release();

			VANS_LOG("[LoadAnimComp] Loaded controller from memory Animator: " << animatorGuid);
		}
		else
		{
			controller = new VansAnimationController();
			controller->SetName(meshGroupName + "_Controller");

			if (!retargetRequested)
			{
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
				for (const auto& clipName : clipNames)
				{
					AnimatorState state;
					state.name = clipName;
					state.clipName = clipName;
					state.speed = 1.0f;
					state.loop = animConfig.loop;
					state.rootMotion = enableRootMotion;
					smNode->m_States.push_back(state);
				}

				auto graph = std::make_unique<VansAnimGraph>();
				const int smId = graph->AddNode(std::move(smNode));
				const int outId = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
				graph->AddLink(smId, 0, outId, 0);
				VansAnimationLayerSetup baseLayer;
				baseLayer.definition.id = "layer-base";
				baseLayer.definition.name = "Base";
				baseLayer.definition.kind = VansAnimationLayerKind::Base;
				baseLayer.definition.rootMotion = VansLayerRootMotionMode::Base;
				baseLayer.definition.nodeTracks = VansLayerNodeTrackMode::Override;
				std::vector<VansAnimationLayerSetup> generatedLayers;
				generatedLayers.push_back(std::move(baseLayer));
				VansAnimationGraphSetSetup graphSet;
				graphSet.definition.id = "graph-set-default";
				graphSet.definition.name = "Default";
				graphSet.definition.bindings.push_back({ "layer-base", "graph-base", true });
				VansAnimationGraphBindingSetup binding;
				binding.definition = graphSet.definition.bindings.front();
				binding.graph = std::move(graph);
				graphSet.bindings.push_back(std::move(binding));
				std::vector<VansAnimationGraphSetSetup> generatedGraphSets;
				generatedGraphSets.push_back(std::move(graphSet));
				std::string layerError;
				if (!controller->SetAnimationGraphSets(
					std::move(generatedLayers), std::move(generatedGraphSets),
					"graph-set-default", {}, {}, layerError))
				{
					VANS_LOG_WARN("[LoadAnimComp] Failed to create generated Base Layer: " << layerError);
					delete controller;
					return nullptr;
				}

				VANS_LOG("[LoadAnimComp] Auto-generated graph controller for '" << meshGroupName
					<< "' with " << clipNames.size() << " clip(s)");
			}
			else
			{
				VANS_LOG("[LoadAnimComp] Created external-pose target controller for '"
					<< meshGroupName << "'");
			}
		}

		if (enableRootMotion && !retargetRequested)
			controller->EnableRootMotion(true);

		if (animConfig.motionMatching && !retargetRequested)
		{
			MotionMatchingSettings mmSettings = *animConfig.motionMatching;
			std::string motionMatchingError;
			if (!controller->ConfigureMotionMatching(mmSettings, motionMatchingError))
			{
				VANS_LOG_WARN("[LoadAnimComp] Invalid Motion Matching configuration for '"
					<< objectName << "': " << motionMatchingError);
				delete controller;
				return nullptr;
			}
			VANS_LOG("[LoadAnimComp] Motion Matching configured for '" << objectName
				<< "' enabled=" << mmSettings.enabled
				<< " driveMode=" << static_cast<int>(mmSettings.motionModel.driveMode)
				<< " sampleRate=" << mmSettings.sampleRate
				<< " searchThrottle=" << mmSettings.searchThrottle
				<< " speedScale=" << mmSettings.desiredSpeedScale
				<< " speedMatching=" << mmSettings.enableSpeedMatching
				<< " playbackRate=[" << mmSettings.minPlaybackRate << "," << mmSettings.maxPlaybackRate << "]"
				<< " databases=" << mmSettings.databases.size()
				<< " selectorRows=" << mmSettings.selectorRows.size());
		}

		VansAnimationNode* animNode = new VansAnimationNode(nodeName);
		animNode->SetSkeleton(meshAsset->m_AnimImportResult.skeleton);
		animNode->SetRenderNodes(group.childNodes);
		animNode->InitGPUResources(device, 1);
		animNode->UploadPerSubmeshBoneBuffers(meshAsset->m_SubMeshBoneData);
		animNode->SetTransformID(group.sharedTransformID);
		if (!animNode->SetController(controller))
		{
			VANS_LOG_WARN("[LoadAnimComp] '" << objectName
				<< "' controller Rig is incompatible with the target model Skeleton");
			delete controller;
			delete animNode;
			return nullptr;
		}

		if (retargetRequested)
		{
			const Vans::VansSceneAnimationRetargetConfig& retargetConfig = *animConfig.retarget;
			auto failRetarget = [&]() -> VansAnimationNode*
			{
				delete controller;
				delete animNode;
				return nullptr;
			};
			if (retargetConfig.profileGuid.empty() ||
				retargetConfig.sourceModelGuid.empty() ||
				retargetConfig.sourceAnimatorGuid.empty())
			{
				VANS_LOG_WARN("[Retarget] '" << objectName
					<< "' retarget is enabled but source_model/source_animator is missing");
				return failRetarget();
			}

			std::filesystem::path sourceModelPath;
			std::string resolveError;
			if (!ResolveAnimationAssetPath(
				retargetConfig.sourceModelGuid,
				Vans::VansAssetType::Model,
				"Retarget Source Model",
				"",
				sourceModelPath,
				resolveError))
			{
				VANS_LOG_WARN("[Retarget] '" << objectName << "' " << resolveError);
				return failRetarget();
			}
			const std::string fullSourceModelPath = sourceModelPath.string();

			Skeleton sourceSkeleton;
			if (!LoadSkeletonFromModel(
				retargetConfig.sourceModelGuid, fullSourceModelPath, sourceSkeleton))
			{
				VANS_LOG_WARN("[Retarget] '" << objectName << "' failed to load source skeleton: "
					<< fullSourceModelPath);
				return failRetarget();
			}

			const MotionMatchingSettings* mmSettings =
				animConfig.motionMatching ? &(*animConfig.motionMatching) : nullptr;
			std::unique_ptr<VansAnimationController> sourceController =
				LoadAnimatorController(
					retargetConfig.sourceAnimatorGuid,
					sourceSkeleton,
					mmSettings,
					enableRootMotion,
					objectName);
			if (!sourceController)
			{
				VANS_LOG_WARN("[Retarget] '" << objectName << "' failed to compile source Animator: "
					<< retargetConfig.sourceAnimatorGuid);
				return failRetarget();
			}

			VansRetargetRuntimeDesc retargetDesc;
			retargetDesc.sourceModelAssetGuid = retargetConfig.sourceModelGuid;
			retargetDesc.sourceAnimatorAssetGuid = retargetConfig.sourceAnimatorGuid;
			retargetDesc.debugDraw = retargetConfig.debugDraw;
			std::string retargetProfileError;
			const auto retargetProfile = ResolveConfigAssetObject<VansRetargetProfileAsset>(
				retargetConfig.profileGuid,
				Vans::VansAssetType::RetargetProfile,
				"Retarget Profile",
				retargetProfileError);
			if (!retargetProfile)
			{
				VANS_LOG_WARN("[Retarget] '" << objectName << "' has an invalid profile: "
					<< retargetProfileError);
				return failRetarget();
			}
			ApplyRetargetProfile(*retargetProfile, retargetDesc, retargetConfig.profileGuid);
			std::string retargetBuildError;
			if (!animNode->ConfigureRetargetSource(
				sourceSkeleton,
				std::move(sourceController),
				retargetDesc,
				retargetBuildError))
			{
				VANS_LOG_WARN("[Retarget] '" << objectName
					<< "' could not establish the required Source -> Target runtime: "
					<< retargetBuildError);
				return failRetarget();
			}
		}

		if (!animatorGuid.empty())
			animNode->SetAnimatorAssetGuid(animatorGuid);

		if (!rootBone.empty())
			animNode->SetRootBone(rootBone);

		for (size_t ci = 0; ci < group.childNodes.size(); ci++)
		{
			VansRenderNode* childNode = group.childNodes[ci];
			const uint32_t submeshIndex = childNode->m_SubmeshIndex != UINT32_MAX
				? childNode->m_SubmeshIndex
				: static_cast<uint32_t>(ci);
			childNode->m_AnimSubmeshIndex = submeshIndex;
			const bool hasSubmeshBoneDataSlot =
				submeshIndex < meshAsset->m_SubMeshBoneData.size();
			const uint32_t influencedVertexCount = hasSubmeshBoneDataSlot
				? CountVerticesWithBoneInfluence(meshAsset->m_SubMeshBoneData[submeshIndex])
				: 0u;
			const uint32_t submeshBoneVertexCount = hasSubmeshBoneDataSlot
				? static_cast<uint32_t>(meshAsset->m_SubMeshBoneData[submeshIndex].size())
				: 0u;
			const bool hasSubmeshBoneData =
				influencedVertexCount > 0;
			if (animConfig.retarget && animConfig.retarget->debugDraw)
			{
				VANS_LOG("[LoadAnimComp] " << nodeName
					<< " bind submesh[" << submeshIndex << "] node='" << childNode->m_NodeName
					<< "' boneVertices=" << influencedVertexCount << "/" << submeshBoneVertexCount
					<< " animEnabled=" << (hasSubmeshBoneData ? 1 : 0));
			}
			if (hasSubmeshBoneData && submeshIndex < animNode->GetSubmeshBufferCount())
			{
				childNode->m_HasSkeletonBone = true;
				childNode->m_AnimationEnabled = true;
				childNode->m_AnimOwner = animNode;
				childNode->m_AnimSubmeshIndex = submeshIndex;
				childNode->m_AnimBoneIDBuffer = &animNode->GetBoneIDBuffer(submeshIndex);
				childNode->m_AnimBoneWeightBuffer = &animNode->GetBoneWeightBuffer(submeshIndex);
				childNode->m_VertexDeformationState.skinningOwner = animNode;
				childNode->m_VertexDeformationState.submeshIndex = submeshIndex;
				childNode->m_VertexDeformationState.boneIDBuffer = childNode->m_AnimBoneIDBuffer;
				childNode->m_VertexDeformationState.boneWeightBuffer = childNode->m_AnimBoneWeightBuffer;
				childNode->m_VertexDeformationState.featureMask = VANS_VERTEX_FEATURE_SKELETAL_SKINNING;
				childNode->MarkAnimationDescriptorDirty();
			}
			else
			{
				childNode->m_HasSkeletonBone = false;
				childNode->m_AnimationEnabled = false;
				childNode->m_AnimOwner = nullptr;
				childNode->m_AnimSubmeshIndex = submeshIndex;
				childNode->m_AnimBoneIDBuffer = nullptr;
				childNode->m_AnimBoneWeightBuffer = nullptr;
				childNode->m_VertexDeformationState = VansVertexDeformationState{};
				if (hasSkeleton && submeshIndex >= animNode->GetSubmeshBufferCount())
				{
					VANS_LOG_WARN("[LoadAnimComp] submesh index " << submeshIndex
						<< " has no bone buffer for node '" << childNode->m_NodeName << "'");
				}
			}
		}

		scene.RegisterAnimationRuntime(animNode, controller);
		if (animConfig.autoPlay)
			animNode->Play(scene.GetLoadMode() == VansSceneLoadMode::Runtime
				? VansAnimationEvaluationPurpose::Gameplay
				: VansAnimationEvaluationPurpose::EditorPreview);

		VANS_LOG("[LoadAnimComp] Created animation component '" << nodeName
			<< "' with " << controller->GetClipNames().size() << " clip(s), "
			<< meshAsset->m_AnimImportResult.skeleton.bones.size() << " bones, "
			<< group.childNodes.size() << " render node(s), autoPlay=" << (animConfig.autoPlay ? 1 : 0));

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

		const std::string& profileGuid = ragdollConfig.profileGuid;
		if (profileGuid.empty())
		{
			VANS_LOG_WARN("[LoadRagdollComp] object '" << obj->m_ObjectName << "' ragdoll missing profile");
			return false;
		}

		std::string error;
		const auto profile = ResolveConfigAssetObject<VansEngine::RagdollProfile>(
			profileGuid,
			Vans::VansAssetType::RagdollProfile,
			"Ragdoll Profile",
			error);
		if (!profile)
		{
			VANS_LOG_WARN("[LoadRagdollComp] " << error);
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

		if (!VansEngine::VansRagdollSystem::GetInstance().CreateRagdoll(animNode, *profile))
			return false;

		const VansEngine::RagdollDriveMode mode = ParseRagdollDriveMode(ragdollConfig.driveMode);
		const float blendWeight = ragdollConfig.blendWeight;

		VansEngine::VansRagdollSystem::GetInstance().SetBlendWeight(animNode, blendWeight);
		VansEngine::VansRagdollSystem::GetInstance().SetDriveMode(animNode, mode);

		auto* ragdollComp = new VansScriptRagdollComponent();
		ragdollComp->m_AnimNode = animNode;
		ragdollComp->m_InitialDriveMode = mode;
		ragdollComp->m_ProfileAssetGuid = profileGuid;
		ragdollComp->m_ProfileName = profile->name;
		ragdollComp->m_ConfiguredBodyCount = static_cast<int>(profile->bodies.size());
		ragdollComp->m_ConfiguredJointCount = static_cast<int>(profile->joints.size());
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
			<< "' profile='" << profile->name << "' bodies=" << profile->bodies.size());
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
				const bool componentEnabled = pending.animationConfig->enabled;
				if (!componentEnabled)
					animationNode->SetEnabled(false);
				pending.component->m_Enabled = componentEnabled;
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
