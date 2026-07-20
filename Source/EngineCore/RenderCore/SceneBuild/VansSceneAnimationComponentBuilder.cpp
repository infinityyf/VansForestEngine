#include "VansSceneAnimationComponentBuilder.h"

#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../AnimationCore/VansAnimatorIO.h"
#include "../../AnimationCore/VansAnimGraph.h"
#include "../../AnimationCore/VansAnimationClipLoader.h"
#include "../../AnimationCore/VansBoneAttachmentSystem.h"
#include "../../AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../../AnimationCore/FootPlacement/VansFootPlacementTypes.h"
#include "../../PhysicsCore/VansCharacterControllerNode.h"
#include "../../PhysicsCore/VansRagdollSystem.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"

namespace VansGraphics
{
	void VansSceneAnimationComponentBuilder::AddAnimationPlaceholder(
		VansScriptObject& object,
		const json& components,
		std::vector<PendingAnimationComponent>& pendingAnimations)
	{
		if (!components.contains("animation"))
			return;

		auto* animationComponent = new VansScriptAnimationComponent();
		animationComponent->m_ComponentName = "animation";
		object.AddComponent(animationComponent);

		PendingAnimationComponent pending;
		pending.obj = &object;
		pending.component = animationComponent;
		pending.animJson = components["animation"];
		pending.objectName = object.m_ObjectName;
		pendingAnimations.push_back(std::move(pending));
	}

VansAnimationNode* VansSceneAnimationComponentBuilder::LoadAnimationComponent(
    VansScene& scene,
    const json& animJson,
    const std::string& objectName,
    const std::string& projectRoot)
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice device = vkDevice->GetLogicDevice();

    std::string meshGroupName  = animJson.value("mesh_group", "");
    std::string animatorPath   = animJson.value("animator", "");
    std::string externClips    = animJson.value("extern_clips", "");
    bool enableRootMotion      = animJson.value("root_motion", false);
    std::string rootBone       = animJson.value("root_bone", "");

    // node 名称：优先读取字段，其次用 object 名称
    std::string nodeName = animJson.value("name", objectName);

    if (meshGroupName.empty())
    {
        VANS_LOG_WARN("[LoadAnimComp] animation component on '" << objectName << "' has no mesh_group, skipping");
        return nullptr;
    }

    // 查找 MultiMeshGroup
    MultiMeshGroup* animationGroup = scene.FindAnimationMultiMeshGroup(meshGroupName, objectName);
    if (!animationGroup)
    {
        VANS_LOG_WARN("[LoadAnimComp] mesh_group '" << meshGroupName << "' not found for object '" << objectName << "'");
        return nullptr;
    }

    MultiMeshGroup& group = *animationGroup;
    if (group.childNodes.empty())
        return nullptr;

    // 查找 mesh 资产
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

    // A skinned bind-pose model may intentionally contain no embedded clips.
    // In that workflow the .vanimator/.vclip assets provide the animation, so
    // clip presence is not a valid capability check. A skeleton is required by
    // both embedded and external animation paths.
    if (meshAsset->m_AnimImportResult.skeleton.bones.empty())
    {
        VANS_LOG_WARN("[LoadAnimComp] mesh_group '" << meshGroupName
                     << "' has no skeleton. Skipping '" << objectName << "'");
        return nullptr;
    }

    // ── 创建 Controller ──────────────────────────────────────────────────
    VansAnimationController* controller = nullptr;

    if (!animatorPath.empty())
    {
        // 路径 A: 从 .vanimator 文件加载完整 controller 定义
        std::string fullAnimatorPath = projectRoot + animatorPath;

        AnimatorAssetData assetData;
        if (!VansAnimatorIO::Load(fullAnimatorPath, assetData))
        {
            VANS_LOG_WARN("[LoadAnimComp] Failed to load .vanimator: " << fullAnimatorPath);
            return nullptr;
        }

        auto clipsMap = VansAnimationClipLoader::LoadClipsFromRefs(
            assetData.clipRefs, projectRoot,
            &meshAsset->m_AnimImportResult.skeleton);

        controller = new VansAnimationController();
        controller->SetName(assetData.name);

        for (const auto& param : assetData.parameters)
        {
            controller->AddParameter(param.name, param.type);
            switch (param.type)
            {
            case AnimatorParamType::Float:   controller->SetFloat(param.name, param.floatVal); break;
            case AnimatorParamType::Bool:    controller->SetBool(param.name, param.boolVal);   break;
            case AnimatorParamType::Int:     controller->SetInt(param.name, param.intVal);     break;
            case AnimatorParamType::Trigger: break;
            case AnimatorParamType::Vector3: controller->SetVector3(param.name, param.vec3Val); break;
            case AnimatorParamType::Quaternion: controller->SetQuaternion(param.name, param.quatVal); break;
            }
        }

        for (auto& [name, clip] : clipsMap)
            controller->AddClip(name, std::move(clip));

        // 传递 AnimGraph（.vanimator 文件必须包含 graph 节点）
        if (assetData.animGraph)
            controller->SetGraph(std::move(assetData.animGraph));
        else
            VANS_LOG_WARN("[LoadAnimComp] .vanimator 文件不含 graph 节点: " << fullAnimatorPath);

        VANS_LOG("[LoadAnimComp] Loaded controller from .vanimator: " << fullAnimatorPath);
    }
    else
    {
        // 路径 B: 自动生成默认 controller（外部 clip 优先，fallback 使用内嵌 clip）
        controller = new VansAnimationController();
        controller->SetName(meshGroupName + "_Controller");

        bool usedExternClips = false;
        if (!externClips.empty())
        {
            std::string fullExternPath = projectRoot + externClips;
            std::vector<VansAnimationClip> extClips;
            if (VansAnimationClipLoader::ExtractClipsFromFBX(
                    fullExternPath, meshAsset->m_AnimImportResult.skeleton, extClips))
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

        // 将所有 clip 封装为 AnimGraphStateMachineNode，驱动 AnimGraph
        auto clipNames = controller->GetClipNames();

        auto smNode = std::make_unique<AnimGraphStateMachineNode>();
        smNode->m_DefaultStateName  = clipNames.empty() ? "" : clipNames.front();
        smNode->m_CurrentStateName  = smNode->m_DefaultStateName;
        for (const auto& clipName : clipNames)
        {
            AnimatorState state;
            state.name       = clipName;
            state.clipName   = clipName;
            state.speed      = 1.0f;
            state.loop       = true;
            state.rootMotion = enableRootMotion;
            smNode->m_States.push_back(state);
        }

        auto graph   = std::make_unique<VansAnimGraph>();
        int  smId    = graph->AddNode(std::move(smNode));
        int  outId   = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
        graph->AddLink(smId, 0, outId, 0);
        controller->SetGraph(std::move(graph));

        VANS_LOG("[LoadAnimComp] Auto-generated graph controller for '" << meshGroupName
                 << "' with " << clipNames.size() << " clip(s)");
    }

    if (enableRootMotion)
        controller->EnableRootMotion(true);

    // ── 创建 AnimationNode ───────────────────────────────────────────────
    if (animJson.contains("motion_matching") && animJson["motion_matching"].is_object())
    {
        const auto& mmJson = animJson["motion_matching"];
        MotionMatchingSettings mmSettings;
        mmSettings.enabled = mmJson.value("enabled", false);
        mmSettings.autoBuild = mmJson.value("auto_build", true);
        mmSettings.sampleRate = mmJson.value("sample_rate", 30.0f);
        mmSettings.searchThrottle = mmJson.value("search_throttle", 0.15f);
        mmSettings.blendDuration = mmJson.value("blend_duration", 0.18f);
        mmSettings.minSwitchCostImprovement = mmJson.value("min_switch_cost_improvement", 0.02f);
        mmSettings.minSwitchInterval = mmJson.value("min_switch_interval", 0.25f);
        mmSettings.blendInterruptFraction = mmJson.value("blend_interrupt_fraction", 0.75f);
        mmSettings.continuationBias = mmJson.value("continuation_bias", 0.10f);
        mmSettings.loopBias = mmJson.value("loop_bias", 0.04f);
        mmSettings.transitionBias = mmJson.value("transition_bias", 0.08f);
        mmSettings.desiredSpeedScale = mmJson.value("desired_speed_scale", 650.0f);
        mmSettings.trajectoryWeight = mmJson.value("trajectory_weight", 1.0f);
        mmSettings.poseWeight = mmJson.value("pose_weight", 0.7f);
        mmSettings.topCandidateCount = mmJson.value("top_candidates", 8);
        mmSettings.allowLegacyBoneDetection = mmJson.value("allow_legacy_bone_detection", true);

        if (mmJson.contains("parameters") && mmJson["parameters"].is_object())
        {
            const auto& paramsJson = mmJson["parameters"];
            mmSettings.parameters.enabled = paramsJson.value("enabled", mmSettings.parameters.enabled);
            mmSettings.parameters.speed = paramsJson.value("speed", mmSettings.parameters.speed);
            mmSettings.parameters.direction = paramsJson.value("direction", mmSettings.parameters.direction);
            mmSettings.parameters.crouching = paramsJson.value("crouching", mmSettings.parameters.crouching);
            mmSettings.parameters.airborne = paramsJson.value("airborne", mmSettings.parameters.airborne);
            mmSettings.parameters.moveState = paramsJson.value("move_state", mmSettings.parameters.moveState);
            mmSettings.parameters.moveState = paramsJson.value("moveState", mmSettings.parameters.moveState);
        }

        if (mmJson.contains("rig") && mmJson["rig"].is_object())
        {
            const auto& rigJson = mmJson["rig"];
            mmSettings.rig.root = rigJson.value("root", "");
            mmSettings.rig.trajectoryRoot = rigJson.value("trajectory_root", "");
            mmSettings.rig.pelvis = rigJson.value("pelvis", "");
            mmSettings.rig.leftFoot = rigJson.value("left_foot", "");
            mmSettings.rig.rightFoot = rigJson.value("right_foot", "");
            mmSettings.rig.head = rigJson.value("head", "");
            if (rigJson.contains("forward_axis") && rigJson["forward_axis"].is_array() && rigJson["forward_axis"].size() >= 3)
            {
                mmSettings.rig.forwardAxis = glm::vec3(
                    rigJson["forward_axis"][0].get<float>(),
                    rigJson["forward_axis"][1].get<float>(),
                    rigJson["forward_axis"][2].get<float>());
            }
        }

        if (mmJson.contains("schema") && mmJson["schema"].is_object())
        {
            const auto& schemaJson = mmJson["schema"];
            mmSettings.trajectoryWeight = schemaJson.value("trajectory_weight", mmSettings.trajectoryWeight);
            mmSettings.poseWeight = schemaJson.value("pose_weight", mmSettings.poseWeight);
            if (schemaJson.contains("future_times") && schemaJson["future_times"].is_array())
            {
                const auto& timesJson = schemaJson["future_times"];
                for (size_t i = 0; i < mmSettings.schema.futureTimes.size() && i < timesJson.size(); ++i)
                {
                    if (timesJson[i].is_number())
                        mmSettings.schema.futureTimes[i] = timesJson[i].get<float>();
                }
            }
        }

        if (mmJson.contains("include_clip_tokens") && mmJson["include_clip_tokens"].is_array())
        {
            for (const auto& token : mmJson["include_clip_tokens"])
                if (token.is_string()) mmSettings.includeClipNameTokens.push_back(token.get<std::string>());
        }
        if (mmJson.contains("exclude_clip_tokens") && mmJson["exclude_clip_tokens"].is_array())
        {
            for (const auto& token : mmJson["exclude_clip_tokens"])
                if (token.is_string()) mmSettings.excludeClipNameTokens.push_back(token.get<std::string>());
        }
        const auto readStringArray = [](const nlohmann::json& object,
                                        const char* key,
                                        std::vector<std::string>& out)
        {
            if (!object.contains(key) || !object[key].is_array())
                return;
            for (const auto& item : object[key])
                if (item.is_string()) out.push_back(item.get<std::string>());
        };
        const auto readIntArray = [](const nlohmann::json& object,
                                     const char* key,
                                     std::vector<int>& out)
        {
            if (!object.contains(key) || !object[key].is_array())
                return;
            for (const auto& item : object[key])
                if (item.is_number_integer()) out.push_back(item.get<int>());
        };
        const auto readReplacingIntArray = [&](const nlohmann::json& object,
                                               const char* key,
                                               std::vector<int>& out)
        {
            if (!object.contains(key) || !object[key].is_array())
                return;
            out.clear();
            readIntArray(object, key, out);
        };
        if (mmJson.contains("states") && mmJson["states"].is_object())
        {
            const auto& statesJson = mmJson["states"];
            mmSettings.states.idleState = statesJson.value("idle", mmSettings.states.idleState);
            mmSettings.states.idleState = statesJson.value("idle_state", mmSettings.states.idleState);
            mmSettings.states.crouchState = statesJson.value("crouch", mmSettings.states.crouchState);
            mmSettings.states.crouchState = statesJson.value("crouch_state", mmSettings.states.crouchState);
            mmSettings.states.idleSpeedThreshold = statesJson.value("idle_speed_threshold", mmSettings.states.idleSpeedThreshold);
            readReplacingIntArray(statesJson, "moving", mmSettings.states.movingStates);
            readReplacingIntArray(statesJson, "moving_states", mmSettings.states.movingStates);
            readReplacingIntArray(statesJson, "pace_transition", mmSettings.states.paceTransitionStates);
            readReplacingIntArray(statesJson, "pace_transition_states", mmSettings.states.paceTransitionStates);
            readReplacingIntArray(statesJson, "stance", mmSettings.states.stanceStates);
            readReplacingIntArray(statesJson, "stance_states", mmSettings.states.stanceStates);
        }
        const nlohmann::json* searchGroupsJson = nullptr;
        if (mmJson.contains("search_groups") && mmJson["search_groups"].is_array())
            searchGroupsJson = &mmJson["search_groups"];
        else if (mmJson.contains("searchGroups") && mmJson["searchGroups"].is_array())
            searchGroupsJson = &mmJson["searchGroups"];
        if (searchGroupsJson)
        {
            for (const auto& groupJson : *searchGroupsJson)
            {
                if (!groupJson.is_object())
                    continue;

                MotionMatchingSearchGroup group;
                group.name = groupJson.value("name", "");
                group.stance = groupJson.value("stance", group.stance);
                group.phase = groupJson.value("phase", group.phase);
                readIntArray(groupJson, "move_states", group.moveStates);
                readIntArray(groupJson, "moveStates", group.moveStates);
                readStringArray(groupJson, "include", group.includeClipNameTokens);
                readStringArray(groupJson, "include_tokens", group.includeClipNameTokens);
                readStringArray(groupJson, "include_clip_tokens", group.includeClipNameTokens);
                readStringArray(groupJson, "exclude", group.excludeClipNameTokens);
                readStringArray(groupJson, "exclude_tokens", group.excludeClipNameTokens);
                readStringArray(groupJson, "exclude_clip_tokens", group.excludeClipNameTokens);

                if (!group.name.empty() ||
                    !group.includeClipNameTokens.empty() ||
                    !group.excludeClipNameTokens.empty() ||
                    !group.moveStates.empty())
                {
                    mmSettings.searchGroups.push_back(std::move(group));
                }
            }
        }
        const nlohmann::json* clipMetadataJson = nullptr;
        if (mmJson.contains("clip_metadata") && mmJson["clip_metadata"].is_array())
            clipMetadataJson = &mmJson["clip_metadata"];
        else if (mmJson.contains("clipMetadata") && mmJson["clipMetadata"].is_array())
            clipMetadataJson = &mmJson["clipMetadata"];
        if (clipMetadataJson)
        {
            const auto readOptionalBool = [](const nlohmann::json& object,
                                             const char* key,
                                             bool& hasValue,
                                             bool& value)
            {
                if (object.contains(key) && object[key].is_boolean())
                {
                    hasValue = true;
                    value = object[key].get<bool>();
                }
            };
            const auto readOptionalInt = [](const nlohmann::json& object,
                                            const char* key,
                                            bool& hasValue,
                                            int& value)
            {
                if (object.contains(key) && object[key].is_number_integer())
                {
                    hasValue = true;
                    value = object[key].get<int>();
                }
            };
            for (const auto& itemJson : *clipMetadataJson)
            {
                if (!itemJson.is_object())
                    continue;

                MotionMatchingClipMetadata metadata;
                metadata.name = itemJson.value("name", "");
                readStringArray(itemJson, "match", metadata.matchTokens);
                readStringArray(itemJson, "match_tokens", metadata.matchTokens);
                readStringArray(itemJson, "matchTokens", metadata.matchTokens);
                readOptionalBool(itemJson, "loop", metadata.hasLoopLike, metadata.loopLike);
                readOptionalBool(itemJson, "loop_like", metadata.hasLoopLike, metadata.loopLike);
                readOptionalBool(itemJson, "idle", metadata.hasIdleLike, metadata.idleLike);
                readOptionalBool(itemJson, "idle_like", metadata.hasIdleLike, metadata.idleLike);
                readOptionalBool(itemJson, "transition", metadata.hasTransitionLike, metadata.transitionLike);
                readOptionalBool(itemJson, "transition_like", metadata.hasTransitionLike, metadata.transitionLike);
                readOptionalBool(itemJson, "start", metadata.hasStartLike, metadata.startLike);
                readOptionalBool(itemJson, "start_like", metadata.hasStartLike, metadata.startLike);
                readOptionalBool(itemJson, "stop", metadata.hasStopLike, metadata.stopLike);
                readOptionalBool(itemJson, "stop_like", metadata.hasStopLike, metadata.stopLike);
                readOptionalBool(itemJson, "turn", metadata.hasTurnLike, metadata.turnLike);
                readOptionalBool(itemJson, "turn_like", metadata.hasTurnLike, metadata.turnLike);
                readOptionalBool(itemJson, "pace_transition", metadata.hasPaceTransitionLike, metadata.paceTransitionLike);
                readOptionalBool(itemJson, "pace_transition_like", metadata.hasPaceTransitionLike, metadata.paceTransitionLike);
                readOptionalInt(itemJson, "source_move_state", metadata.hasSourceMoveState, metadata.sourceMoveState);
                readOptionalInt(itemJson, "sourceMoveState", metadata.hasSourceMoveState, metadata.sourceMoveState);
                readOptionalInt(itemJson, "target_move_state", metadata.hasTargetMoveState, metadata.targetMoveState);
                readOptionalInt(itemJson, "targetMoveState", metadata.hasTargetMoveState, metadata.targetMoveState);
                readOptionalInt(itemJson, "direction_bucket", metadata.hasDirectionBucket, metadata.directionBucket);
                readOptionalInt(itemJson, "directionBucket", metadata.hasDirectionBucket, metadata.directionBucket);
                readOptionalInt(itemJson, "turn_direction_sign", metadata.hasTurnDirectionSign, metadata.turnDirectionSign);
                readOptionalInt(itemJson, "turnDirectionSign", metadata.hasTurnDirectionSign, metadata.turnDirectionSign);
                readOptionalInt(itemJson, "turn_bucket_delta", metadata.hasTurnBucketDelta, metadata.turnBucketDelta);
                readOptionalInt(itemJson, "turnBucketDelta", metadata.hasTurnBucketDelta, metadata.turnBucketDelta);
                if (!metadata.name.empty() || !metadata.matchTokens.empty())
                    mmSettings.clipMetadata.push_back(std::move(metadata));
            }
        }

        controller->ConfigureMotionMatching(mmSettings);
        VANS_LOG("[LoadAnimComp] Motion Matching configured for '" << objectName
                 << "' enabled=" << mmSettings.enabled
                 << " sampleRate=" << mmSettings.sampleRate
                 << " searchThrottle=" << mmSettings.searchThrottle
                 << " searchGroups=" << mmSettings.searchGroups.size());
    }

    if (animJson.contains("foot_placement") && animJson["foot_placement"].is_object())
    {
        const auto& fpJson = animJson["foot_placement"];
        FootPlacementSettings fpSettings;
        fpSettings.enabled = fpJson.value("enabled", false);
        fpSettings.probeHeightAbove = fpJson.value("probe_height_above", fpSettings.probeHeightAbove);
        fpSettings.probeDistanceBelow = fpJson.value("probe_distance_below", fpSettings.probeDistanceBelow);
        fpSettings.probeFootRadius = fpJson.value("probe_foot_radius", fpSettings.probeFootRadius);
        fpSettings.probeFootForwardExtent = fpJson.value("probe_foot_forward_extent", fpSettings.probeFootForwardExtent);
        fpSettings.probeFootBackwardExtent = fpJson.value("probe_foot_backward_extent", fpSettings.probeFootBackwardExtent);
        fpSettings.probeFootSideExtent = fpJson.value("probe_foot_side_extent", fpSettings.probeFootSideExtent);
        fpSettings.footGroundOffset = fpJson.value("foot_ground_offset", fpSettings.footGroundOffset);
        fpSettings.maxSurfaceAngleDeg = fpJson.value("max_surface_angle_deg", fpSettings.maxSurfaceAngleDeg);
        fpSettings.maxVerticalCorrectionUp = fpJson.value("max_vertical_correction_up", fpSettings.maxVerticalCorrectionUp);
        fpSettings.maxVerticalCorrectionDown = fpJson.value("max_vertical_correction_down", fpSettings.maxVerticalCorrectionDown);
        fpSettings.maxHorizontalFootError = fpJson.value("max_horizontal_foot_error", fpSettings.maxHorizontalFootError);
        fpSettings.minContactQuality = fpJson.value("min_contact_quality", fpSettings.minContactQuality);
        fpSettings.pelvisMaxDown = fpJson.value("pelvis_max_down", fpSettings.pelvisMaxDown);
        fpSettings.pelvisMaxUp = fpJson.value("pelvis_max_up", fpSettings.pelvisMaxUp);
        fpSettings.pelvisInterpSpeed = fpJson.value("pelvis_interp_speed", fpSettings.pelvisInterpSpeed);
        fpSettings.ikWeight = fpJson.value("ik_weight", fpSettings.ikWeight);
        fpSettings.ikWeightSpeed = fpJson.value("ik_weight_speed", fpSettings.ikWeightSpeed);
        fpSettings.crouchWeightScale = fpJson.value("crouch_weight_scale", fpSettings.crouchWeightScale);
        fpSettings.stanceChangeSuppressionTime = fpJson.value("stance_change_suppression_time", fpSettings.stanceChangeSuppressionTime);
        fpSettings.footLockInterpSpeed = fpJson.value("foot_lock_interp_speed", fpSettings.footLockInterpSpeed);
        fpSettings.normalInterpSpeed = fpJson.value("normal_interp_speed", fpSettings.normalInterpSpeed);
        fpSettings.groundHeightInterpSpeed = fpJson.value("ground_height_interp_speed", fpSettings.groundHeightInterpSpeed);
        fpSettings.footPlantFullHeight = fpJson.value("foot_plant_full_height", fpSettings.footPlantFullHeight);
        fpSettings.footPlantFadeHeight = fpJson.value("foot_plant_fade_height", fpSettings.footPlantFadeHeight);
        fpSettings.poleInterpSpeed = fpJson.value("pole_interp_speed", fpSettings.poleInterpSpeed);
        if (animJson.contains("motion_matching") &&
            animJson["motion_matching"].is_object() &&
            animJson["motion_matching"].contains("rig") &&
            animJson["motion_matching"]["rig"].is_object())
        {
            const auto& rigJson = animJson["motion_matching"]["rig"];
            if (rigJson.contains("forward_axis") &&
                rigJson["forward_axis"].is_array() &&
                rigJson["forward_axis"].size() >= 3)
            {
                fpSettings.kneePoleModelDir = glm::vec3(
                    rigJson["forward_axis"][0].get<float>(),
                    rigJson["forward_axis"][1].get<float>(),
                    rigJson["forward_axis"][2].get<float>());
                fpSettings.kneePoleModelWeight = 0.85f;
            }
        }
        fpSettings.kneePoleModelWeight = fpJson.value("knee_pole_model_weight", fpSettings.kneePoleModelWeight);
        if (fpJson.contains("knee_pole_model_dir") &&
            fpJson["knee_pole_model_dir"].is_array() &&
            fpJson["knee_pole_model_dir"].size() >= 3)
        {
            fpSettings.kneePoleModelDir = glm::vec3(
                fpJson["knee_pole_model_dir"][0].get<float>(),
                fpJson["knee_pole_model_dir"][1].get<float>(),
                fpJson["knee_pole_model_dir"][2].get<float>());
        }
        fpSettings.enableFootRotation = fpJson.value("enable_foot_rotation", fpSettings.enableFootRotation);
        fpSettings.footRotationWeight = fpJson.value("foot_rotation_weight", fpSettings.footRotationWeight);
        fpSettings.ankleHeightOffset = fpJson.value("ankle_height_offset", fpSettings.ankleHeightOffset);
        fpSettings.debugVisualization = fpJson.value("debug_visualization", fpSettings.debugVisualization);
        fpSettings.collisionMask = fpJson.value("collision_mask", fpSettings.collisionMask);

        if (fpJson.contains("bones") && fpJson["bones"].is_object())
        {
            const auto& bonesJson = fpJson["bones"];
            fpSettings.bones.pelvis = bonesJson.value("pelvis", fpSettings.bones.pelvis);
            fpSettings.bones.leftHip = bonesJson.value("left_hip", fpSettings.bones.leftHip);
            fpSettings.bones.leftKnee = bonesJson.value("left_knee", fpSettings.bones.leftKnee);
            fpSettings.bones.leftFoot = bonesJson.value("left_foot", fpSettings.bones.leftFoot);
            fpSettings.bones.rightHip = bonesJson.value("right_hip", fpSettings.bones.rightHip);
            fpSettings.bones.rightKnee = bonesJson.value("right_knee", fpSettings.bones.rightKnee);
            fpSettings.bones.rightFoot = bonesJson.value("right_foot", fpSettings.bones.rightFoot);
        }

        controller->ConfigureFootPlacement(fpSettings, meshAsset->m_AnimImportResult.skeleton);
        controller->SetFootPlacementEnabled(fpSettings.enabled);
        VANS_LOG("[LoadAnimComp] FootPlacement configured for '" << objectName
                 << "' enabled=" << fpSettings.enabled
                 << " probe=(" << fpSettings.probeHeightAbove << "+" << fpSettings.probeDistanceBelow << ")");
    }

    VansAnimationNode* animNode = new VansAnimationNode(nodeName);
    animNode->SetSkeleton(meshAsset->m_AnimImportResult.skeleton);
    animNode->SetRenderNodes(group.childNodes);
    animNode->InitGPUResources(device, 1);
    animNode->UploadPerSubmeshBoneBuffers(meshAsset->m_SubMeshBoneData);
    animNode->SetTransformID(group.sharedTransformID);
    animNode->SetController(controller);

    // 记录 .vanimator 文件路径供编辑器使用
    if (!animatorPath.empty())
        animNode->SetAnimatorFilePath(projectRoot + animatorPath);

    if (!rootBone.empty())
        animNode->SetRootBone(rootBone);

    // 标记渲染节点
    for (size_t ci = 0; ci < group.childNodes.size(); ci++)
    {
        VansRenderNode* childNode = group.childNodes[ci];
        const uint32_t submeshIndex = childNode->m_SubmeshIndex != UINT32_MAX
            ? childNode->m_SubmeshIndex
            : static_cast<uint32_t>(ci);
        childNode->m_HasSkeletonBone  = true;
        childNode->m_AnimationEnabled = true;
        childNode->m_AnimOwner        = animNode;
        childNode->m_AnimSubmeshIndex = submeshIndex;
        if (submeshIndex < animNode->GetSubmeshBufferCount())
        {
            childNode->m_AnimBoneIDBuffer    = &animNode->GetBoneIDBuffer(submeshIndex);
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

    if (animJson.contains("bone_bindings") && animJson["bone_bindings"].is_array())
    {
        using namespace VansEngine;

        auto readVec3 = [](const json& source, const char* key, const glm::vec3& defaultValue) -> glm::vec3
        {
            if (!source.contains(key) || !source[key].is_array() || source[key].size() < 3)
                return defaultValue;

            return glm::vec3(
                source[key][0].get<float>(),
                source[key][1].get<float>(),
                source[key][2].get<float>());
        };

        auto parseShapeType = [](const std::string& value) -> PhysicsColliderType
        {
            if (value == "box") return PhysicsColliderType::Box;
            if (value == "sphere") return PhysicsColliderType::Sphere;
            if (value == "capsule") return PhysicsColliderType::Capsule;
            if (value == "mesh") return PhysicsColliderType::Mesh;
            if (value == "convex") return PhysicsColliderType::ConvexMesh;
            return PhysicsColliderType::Capsule;
        };

        BoneColliderBindingSet bindingSet;
        bindingSet.animNode = animNode;

        const Skeleton& skeleton = meshAsset->m_AnimImportResult.skeleton;
        for (const auto& bindJson : animJson["bone_bindings"])
        {
            BoneColliderBinding binding;
            binding.boneName          = bindJson.value("bone_name", "");
            binding.physicsObjectName = bindJson.value("physics_object", "");
            binding.offsetPosition    = readVec3(bindJson, "offset_position", glm::vec3(0.0f));
            binding.offsetRotation    = readVec3(bindJson, "offset_rotation", glm::vec3(0.0f));
            binding.offsetScale       = readVec3(bindJson, "offset_scale", glm::vec3(1.0f));
            binding.syncRotation      = bindJson.value("sync_rotation", true);
            binding.syncScale         = bindJson.value("sync_scale", false);
            binding.layerName         = bindJson.value("layer", "Default");
            binding.isTrigger         = bindJson.value("is_trigger", false);
            binding.enabled           = bindJson.value("enabled", true);
            binding.autoCreateNode    = bindJson.value("auto_create_node", false);
            binding.shapeExtents      = readVec3(bindJson, "shape_extents", glm::vec3(0.1f, 0.25f, 0.1f));
            binding.shapeType         = parseShapeType(bindJson.value("shape_type", "capsule"));

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
                else if (binding.physicsNode->GetProperties().bodyType != PhysicsBodyType::Kinematic &&
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

        VansBoneAttachmentSystem::GetInstance().RegisterBindingSet(std::move(bindingSet));
        VANS_LOG("[LoadAnimComp] Registered " << animJson["bone_bindings"].size()
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
    const json& ragdollJson,
    const std::string& projectRoot)
{
    if (obj == nullptr || animNode == nullptr || !ragdollJson.is_object())
        return false;

    std::string profilePath = ragdollJson.value("profile", "");
    if (profilePath.empty())
    {
        VANS_LOG_WARN("[LoadRagdollComp] object '" << obj->m_ObjectName << "' ragdoll missing profile");
        return false;
    }

    std::string fullProfilePath = projectRoot + profilePath;
    VansEngine::RagdollProfile profile;
    if (!VansEngine::RagdollProfile::LoadFromFile(fullProfilePath, profile))
    {
        VANS_LOG_WARN("[LoadRagdollComp] failed to load profile: " << fullProfilePath);
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
            << "' 同时配置了 bone_bindings 与 ragdoll；Physics/Blend 模式下请避免绑定同一骨骼");
    }

    if (!VansEngine::VansRagdollSystem::GetInstance().CreateRagdoll(animNode, profile))
        return false;

    auto parseMode = [](const std::string& value) -> VansEngine::RagdollDriveMode
    {
        if (value == "physics") return VansEngine::RagdollDriveMode::Physics;
        if (value == "blend") return VansEngine::RagdollDriveMode::Blend;
        return VansEngine::RagdollDriveMode::Animation;
    };

    VansEngine::RagdollDriveMode mode = parseMode(ragdollJson.value("drive_mode", "animation"));
    float blendWeight = ragdollJson.value("blend_weight", 0.0f);

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

    // ── 延迟绑定消费：若同对象 CCT 配置了 followRagdoll，绑定刚创建的 animNode ──
    auto* cctComp = obj->GetComponent<VansScriptCharacterControllerComponent>();
    if (cctComp && cctComp->m_ControllerNode &&
        cctComp->m_ControllerNode->HasPendingFollowRagdoll())
    {
        cctComp->m_ControllerNode->SetFollowRagdoll(
            animNode, cctComp->m_ControllerNode->GetPendingFollowRagdollBone());
        cctComp->m_ControllerNode->ConsumePendingFollowRagdoll();
        VANS_LOG("[LoadRagdollComp] CCT followRagdoll \u7ed1\u5b9a\u5b8c\u6210\uff0cobjName='" << obj->m_ObjectName
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
			if (!pending.component)
				continue;

			VansAnimationNode* animationNode = LoadAnimationComponent(
				scene,
				pending.animJson,
				pending.objectName,
				projectRoot);
			pending.component->m_AnimNode = animationNode;

			if (animationNode)
			{
				const bool animationEnabled = pending.animJson.value("enabled", true);
				if (!animationEnabled)
					animationNode->SetEnabled(false);
				pending.component->m_Enabled = animationEnabled;
			}

			if (!animationNode)
			{
				VANS_LOG_WARN("[LoadSceneObjects] Animation component for '"
					<< pending.objectName << "' could not be created");
			}
			else if (pending.obj && pending.animJson.contains("ragdoll"))
			{
				LoadRagdollComponent(
					scene,
					pending.obj,
					animationNode,
					pending.animJson["ragdoll"],
					projectRoot);
			}
		}
	}
}
