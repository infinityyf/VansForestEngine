#include "../EngineCore/SceneRuntime/Transform/VansTransformStore.h"
#include "ProceduralAnimationContractTests.h"
#include "../EngineCore/AnimationCore/VansAnimGraph.h"
#include "../EngineCore/AnimationCore/Storage/VansBoneMaskStorage.h"
#include "../EngineCore/AnimationCore/VansAnimationLayer.h"
#include "../EngineCore/SceneRuntime/Animation/VansAnimationTargetResolver.h"
#include "../EngineCore/SceneCore/VansSceneParentReference.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"

#include "../EngineCore/AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../EngineCore/AnimationCore/Procedural/Grounding/VansGroundingRuntime.h"
#include "../EngineCore/AnimationCore/Procedural/Solvers/VansAimConstraintSolver.h"
#include "../EngineCore/AnimationCore/Procedural/Solvers/VansChainIKSolver.h"
#include "../EngineCore/AnimationCore/Procedural/Solvers/VansLimbIKSolver.h"
#include "../EngineCore/AnimationCore/Procedural/Solvers/VansRotationDistributionSolver.h"
#include "../EngineCore/EngineAPILayer/Private/AnimationAuthoringBridge.h"
#include "../EngineCore/AnimationCore/Storage/VansAnimationRigStorage.h"
#include "../EngineCore/AnimationCore/Serialization/VansRetargetProfileJsonCodec.h"
#include "../EngineCore/AnimationCore/VansAnimationController.h"
#include "../EngineCore/AnimationCore/VansAnimationNode.h"
#include "../EngineCore/RuntimeCore/VansCharacterLocomotionResolver.h"
#include "../EngineCore/AnimationCore/VansPoseMath.h"
#include "../EngineCore/AnimationCore/Retargeting/VansAnimationRetargetBaker.h"
#include "../EngineCore/AnimationCore/VansSkinnedMeshLoader.h"
#include "../EngineCore/AnimationCore/Storage/VansRetargetProfileStorage.h"
#include "../EngineCore/AssetCore/Storage/VansAssetMetaStorage.h"
#include "../EngineCore/AnimationCore/VansAnimationSampler.h"
#include "../EngineCore/AnimationCore/VansAnimatorRuntimeCompiler.h"
#include "../EngineCore/AnimationCore/VansAnimatorValidator.h"
#include "../EngineCore/RenderCore/Animation/VansAnimationGpuBinding.h"
#include <fstream>
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EngineCore/AssetCore/VansAssetDocument.h"
#include "../EngineCore/SceneCore/VansSceneAnimationComponentReader.h"
#include "../EngineCore/SceneCore/Serialization/VansMotionMatchingConfigCodec.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	using namespace VansGraphics;

	template<typename Pose, typename = void>
	struct HasLegacyMatrixPoseBlend : std::false_type {};

	template<typename Pose>
	struct HasLegacyMatrixPoseBlend<Pose, std::void_t<decltype(
		VansPoseMath::BlendPoses(
			std::declval<const Pose&>(),
			std::declval<const Pose&>(),
			0.5f,
			std::declval<Pose&>()))>> : std::true_type {};

	template<typename Pose, typename = void>
	struct HasLegacyMatrixPoseAdditive : std::false_type {};

	template<typename Pose>
	struct HasLegacyMatrixPoseAdditive<Pose, std::void_t<decltype(
		VansPoseMath::ApplyAdditivePose(
			std::declval<const Pose&>(),
			std::declval<const Pose&>(),
			0.5f,
			std::declval<Pose&>()))>> : std::true_type {};

	static_assert(!HasLegacyMatrixPoseBlend<std::vector<glm::mat4>>::value,
		"Animation pose blending must use the TRS frame-pose representation");
	static_assert(!HasLegacyMatrixPoseAdditive<std::vector<glm::mat4>>::value,
		"Animation additive poses must use the TRS frame-pose representation");

	bool Check(bool condition, const char* message)
	{
		if (!condition)
			std::cerr << "Procedural animation contract failed: " << message << '\n';
		return condition;
	}

	bool HasLimit(const VansProceduralSolverResult& result, VansProceduralLimitReason reason)
	{
		return (static_cast<std::uint32_t>(result.limitReason)
			& static_cast<std::uint32_t>(reason)) != 0;
	}

	bool TestTrsPoseMathContract()
	{
		VansAnimationFrameVector<VansBoneTransform> first(2);
		VansAnimationFrameVector<VansBoneTransform> second(2);
		VansAnimationFrameVector<VansBoneTransform> blended;

		first[0].translation = glm::vec3(2.0f, 0.0f, 0.0f);
		first[0].scale = glm::vec3(1.0f);
		second[0].translation = glm::vec3(6.0f, 0.0f, 0.0f);
		second[0].rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		second[0].scale = glm::vec3(3.0f);

		VansPoseMath::BlendPoses(first, second, 0.25f, blended);
		if (!Check(blended.size() == 2
			&& glm::length(blended[0].translation - glm::vec3(3.0f, 0.0f, 0.0f)) < 1.0e-6f
			&& glm::length(blended[0].scale - glm::vec3(1.5f)) < 1.0e-6f
			&& std::abs(glm::length(blended[0].rotation) - 1.0f) < 1.0e-6f,
			"TRS frame-pose blending changed translation, scale, or rotation normalization"))
			return false;

		VansAnimationFrameVector<VansBoneTransform> additive(2);
		VansAnimationFrameVector<VansBoneTransform> reference(2);
		VansAnimationFrameVector<VansBoneTransform> result;
		first[0].translation = glm::vec3(10.0f, 0.0f, 0.0f);
		additive[0].translation = glm::vec3(5.0f, 0.0f, 0.0f);
		additive[0].scale = glm::vec3(2.0f);
		reference[0].translation = glm::vec3(1.0f, 0.0f, 0.0f);

		VansPoseMath::ApplyAdditivePose(first, additive, 0.5f, result, &reference);
		return Check(result.size() == 2
			&& glm::length(result[0].translation - glm::vec3(12.0f, 0.0f, 0.0f)) < 1.0e-6f
			&& glm::length(result[0].scale - glm::vec3(1.5f)) < 1.0e-6f,
			"TRS frame-pose additive evaluation changed reference-relative semantics");
	}

	bool TestSkeletonIdentityAndHierarchyContract()
	{
		Skeleton skeleton;
		skeleton.sourceSkeletonGuid = "skeleton-contract";
		skeleton.bones.resize(3);
		skeleton.bones[0].name = "leaf";
		skeleton.bones[0].guid = "bone-leaf";
		skeleton.bones[0].canonicalPath = "root/parent/leaf";
		skeleton.bones[0].parentIndex = 1;
		skeleton.bones[0].localTransform = glm::translate(
			glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		skeleton.bones[1].name = "parent";
		skeleton.bones[1].guid = "bone-parent";
		skeleton.bones[1].canonicalPath = "root/parent";
		skeleton.bones[1].parentIndex = 2;
		skeleton.bones[1].children = { 0 };
		skeleton.bones[1].localTransform = glm::translate(
			glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f))
			* glm::toMat4(glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
		skeleton.bones[2].name = "root";
		skeleton.bones[2].guid = "bone-root";
		skeleton.bones[2].canonicalPath = "root";
		skeleton.bones[2].parentIndex = -1;
		skeleton.bones[2].children = { 1 };
		skeleton.bones[2].localTransform = glm::translate(
			glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
		skeleton.BuildTopologicalOrder();
		skeleton.RebuildIdentityMapsAndSignature();

		std::string topologyError;
		if (!Check(skeleton.ValidateTopology(&topologyError)
			&& skeleton.topologicalOrder == std::vector<int>({ 2, 1, 0 }),
			"Skeleton topology did not preserve parent-before-child order for non-index hierarchy"))
			return false;
		if (!Check(skeleton.FindBoneIndex("bone-leaf") == 0
			&& skeleton.FindBoneIndex("root/parent") == 1
			&& skeleton.FindBoneIndex("root") == 2,
			"Skeleton identity lookup did not resolve GUID, canonical path, and unique name"))
			return false;

		Skeleton ambiguous = skeleton;
		ambiguous.bones[0].name = "joint";
		ambiguous.bones[1].name = "joint";
		ambiguous.RebuildIdentityMapsAndSignature();
		if (!Check(ambiguous.FindBoneIndex("joint") < 0
			&& ambiguous.FindBoneIndex("root/parent/leaf") == 0,
			"Ambiguous bone names must require stable path or GUID identity"))
			return false;

		std::vector<glm::mat4> localTransforms;
		for (const BoneInfo& bone : skeleton.bones)
			localTransforms.push_back(bone.localTransform);
		std::vector<glm::mat4> modelTransforms;
		if (!Check(VansPoseMath::BuildModelTransforms(
			localTransforms, skeleton, modelTransforms, &topologyError),
			"Canonical local-to-model propagation rejected a valid topology"))
			return false;
		const glm::mat4 expectedLeaf = skeleton.bones[2].localTransform
			* skeleton.bones[1].localTransform * skeleton.bones[0].localTransform;
		if (!Check(glm::length(glm::vec4(modelTransforms[0][0] - expectedLeaf[0])) < 1.0e-5f
			&& glm::length(glm::vec4(modelTransforms[0][1] - expectedLeaf[1])) < 1.0e-5f
			&& glm::length(glm::vec4(modelTransforms[0][2] - expectedLeaf[2])) < 1.0e-5f
			&& glm::length(glm::vec4(modelTransforms[0][3] - expectedLeaf[3])) < 1.0e-5f,
			"Canonical local-to-model propagation changed the golden non-index pose"))
			return false;

		std::vector<glm::mat4> roundTrip;
		if (!Check(VansPoseMath::BuildLocalTransforms(
			modelTransforms, skeleton, roundTrip, &topologyError),
			"Canonical model-to-local propagation rejected a valid topology"))
			return false;
		for (std::size_t index = 0; index < roundTrip.size(); ++index)
		{
			for (int column = 0; column < 4; ++column)
			{
				if (!Check(glm::length(glm::vec4(roundTrip[index][column]
					- localTransforms[index][column])) < 1.0e-5f,
					"Local/model round-trip changed a bone transform"))
					return false;
			}
		}

		VansBoneMaskAsset mask;
		mask.id = "signature-contract";
		mask.defaultWeight = 1.0f;
		const VansCompiledBoneMask compiled = VansBoneMaskCompiler::Compile(mask, skeleton);
		if (!Check(compiled.skeletonSignature == skeleton.ComputeSignature(),
			"Bone Mask cache key did not use the canonical Skeleton signature"))
			return false;

		Skeleton invalid = skeleton;
		invalid.topologicalOrder = { 0, 1, 2 };
		std::vector<glm::mat4> untouched = { glm::mat4(7.0f) };
		topologyError.clear();
		return Check(!VansPoseMath::BuildModelTransforms(
			localTransforms, invalid, untouched, &topologyError)
			&& !topologyError.empty() && untouched.size() == 1
			&& untouched[0][0][0] == 7.0f,
			"Invalid topology must fail explicitly without mutating the output pose");
	}

	int AddBone(Skeleton& skeleton, const char* name, int parent, const glm::vec3& translation)
	{
		BoneInfo bone;
		bone.id = static_cast<int>(skeleton.bones.size());
		bone.name = name;
		bone.parentIndex = parent;
		bone.localTransform = glm::translate(glm::mat4(1.0f), translation);
		bone.offsetMatrix = glm::mat4(1.0f);
		skeleton.bones.push_back(std::move(bone));
		const int index = static_cast<int>(skeleton.bones.size()) - 1;
		skeleton.boneNameToIndex[name] = index;
		if (parent >= 0)
			skeleton.bones[static_cast<std::size_t>(parent)].children.push_back(index);
		return index;
	}

	std::vector<VansBoneTransform> BuildLocalPose(const Skeleton& skeleton)
	{
		std::vector<VansBoneTransform> pose(skeleton.bones.size());
		for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
			VansPoseMath::TryDecompose(skeleton.bones[index].localTransform, pose[index]);
		return pose;
	}

	struct LegFixture
	{
		Skeleton skeleton;
		VansAnimationRigAsset asset;
		VansCompiledAnimationRig rig;
		std::vector<VansBoneTransform> localPose;
	};

	bool BuildLegFixture(LegFixture& fixture, bool straight)
	{
		fixture = {};
		const int root = AddBone(fixture.skeleton, "root", -1, glm::vec3(0.0f));
		const int pelvis = AddBone(fixture.skeleton, "pelvis", root, glm::vec3(0.0f, 1.0f, 0.0f));
		const int thigh = AddBone(fixture.skeleton, "thigh_l", pelvis, glm::vec3(0.0f));
		const int calf = AddBone(fixture.skeleton, "calf_l", thigh,
			glm::vec3(0.0f, -0.45f, straight ? 0.0f : 0.08f));
		const int foot = AddBone(fixture.skeleton, "foot_l", calf,
			glm::vec3(0.0f, -0.45f, straight ? 0.0f : -0.08f));
		AddBone(fixture.skeleton, "ball_l", foot, glm::vec3(0.0f, 0.0f, 0.15f));
		fixture.skeleton.BuildTopologicalOrder();

		fixture.asset.name = "Procedural Leg Fixture";
		fixture.asset.skeletonGuid = "00000000-0000-4000-8000-000000000001";
		fixture.skeleton.sourceSkeletonGuid = fixture.asset.skeletonGuid;
		fixture.asset.modelForward = glm::vec3(0.0f, 0.0f, 1.0f);
		fixture.asset.modelUp = glm::vec3(0.0f, 1.0f, 0.0f);
		fixture.asset.semanticBones.emplace("pelvis", "pelvis");
		fixture.asset.goals.push_back({ "leftFoot", "foot_l" });
		VansRigChainDefinition chain;
		chain.id = "leftLeg";
		chain.solver = VansRigSolverKind::Limb;
		chain.bones = { "thigh_l", "calf_l", "foot_l" };
		chain.goal = "leftFoot";
		chain.poleAxisLocal = glm::vec3(1.0f, 0.0f, 0.0f);
		fixture.asset.chains.push_back(chain);
		VansRigContactDefinition contact;
		contact.id = "leftFoot";
		contact.chain = "leftLeg";
		contact.footBone = "foot_l";
		contact.ballBone = "ball_l";
		contact.soleForwardLocal = glm::vec3(0.0f, 0.0f, 1.0f);
		contact.soleNormalLocal = glm::vec3(0.0f, 1.0f, 0.0f);
		contact.soleSamplesLocal = {
			{ "heelOuter", glm::vec3(-0.08f, -0.10f, -0.16f) },
			{ "heelInner", glm::vec3(0.08f, -0.10f, -0.16f) },
			{ "toeOuter", glm::vec3(-0.08f, -0.10f, 0.20f) },
			{ "toeInner", glm::vec3(0.08f, -0.10f, 0.20f) }
		};
		contact.heelPivotLocal = glm::vec3(0.0f, -0.10f, -0.16f);
		contact.ballPivotLocal = glm::vec3(0.0f, -0.10f, 0.15f);
		contact.anklePivotLocal = glm::vec3(0.0f);
		contact.sweepRadius = 0.025f;
		fixture.asset.contacts.push_back(contact);
		std::string error;
		if (!VansAnimationRigCompiler::Compile(
			fixture.asset, fixture.skeleton, fixture.rig, error))
			return Check(false, error.c_str());
		fixture.localPose = BuildLocalPose(fixture.skeleton);
		return true;
	}

	VansGroundingSettings GroundingSettings(bool plantLock)
	{
		VansGroundingSettings settings;
		settings.contacts = { "leftFoot" };
		settings.query.profile = "testGround";
		settings.query.collisionMask = 1u;
		settings.query.maxPlaneResidual = 0.015f;
		settings.query.maxNormalDeviationDegrees = 15.0f;
		settings.query.maxSlopeDegrees = 55.0f;
		settings.plant.lockEnabled = plantLock;
		settings.plantSignal = plantLock ? "locomotion" : "";
		settings.plant.weightHalfLife = 0.0f;
		settings.alignment.normalHalfLife = 0.0f;
		settings.pelvis.maxUpOffset = 0.32f;
		settings.pelvis.halfLife = 0.0f;
		return settings;
	}

	std::vector<VansWorldQueryResult> BuildPlaneResults(
		const std::vector<VansWorldQueryRequest>& requests,
		const glm::vec3& normal,
		const glm::vec3& point,
		VansSupportHandle support = {},
		const glm::vec3& supportPosition = glm::vec3(0.0f),
		bool hasSupportTransform = false)
	{
		std::vector<VansWorldQueryResult> results;
		results.reserve(requests.size());
		for (const VansWorldQueryRequest& request : requests)
		{
			const float y = point.y - (normal.x * (request.originWorld.x - point.x)
				+ normal.z * (request.originWorld.z - point.z)) / normal.y;
			VansWorldQueryResult result;
			result.requestId = request.requestId;
			result.hit = true;
			result.positionWorld = glm::vec3(request.originWorld.x, y, request.originWorld.z);
			result.normalWorld = normal;
			result.support = support;
			result.supportPositionWorld = supportPosition;
			result.supportRotationWorld = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			result.hasSupportTransform = hasSupportTransform;
			result.supportMovable = support.IsValid();
			results.push_back(result);
		}
		return results;
	}

	bool ResolveGround(VansGroundingRuntime& runtime,
		VansPoseWorkspace& workspace,
		const VansAnimationExternalInputSnapshot& input,
		const glm::vec3& normal,
		const glm::vec3& point,
		std::vector<VansProceduralGoal>& goals,
		VansProceduralSolverResult& result,
		VansSupportHandle support = {},
		const glm::vec3& supportPosition = glm::vec3(0.0f),
		bool hasSupportTransform = false)
	{
		std::vector<VansWorldQueryRequest> requests;
		if (!runtime.Prepare(workspace, input, requests))
			return false;
		const bool resolved = runtime.Resolve(1.0f / 60.0f, workspace, input,
			BuildPlaneResults(requests, normal, point, support,
				supportPosition, hasSupportTransform), goals, result);
		if (resolved) runtime.CommitResolvedState();
		else runtime.RollbackResolvedState();
		return resolved;
	}

	bool TestGroundingPlanesAndAirborne()
	{
		LegFixture fixture;
		if (!BuildLegFixture(fixture, false)) return false;
		VansCompiledGroundingSettings compiled;
		std::string error;
		if (!Check(VansCompileGroundingSettings(
			GroundingSettings(false), fixture.rig, compiled, error), error.c_str()))
			return false;
		VansGroundingRuntime runtime;
		if (!Check(runtime.Configure(fixture.rig, compiled, error), error.c_str())) return false;
		VansPoseWorkspace workspace;
		if (!Check(workspace.Initialize(fixture.skeleton, fixture.localPose),
			"Grounding fixture workspace failed")) return false;
		VansAnimationExternalInputSnapshot input;
		input.ownerId = 7;
		input.grounded = true;
		VansAnimationExternalInputSnapshot invalidInput = input;
		invalidInput.contacts = {
			{ "locomotion", "leftFoot", 0.5f, 1.0f, true },
			{ "locomotion", "leftFoot", 0.5f, 1.0f, true }
		};
		std::vector<VansWorldQueryRequest> invalidRequests;
		if (!Check(!runtime.Prepare(workspace, invalidInput, invalidRequests),
			"Grounding accepted duplicate contact attributes in an input snapshot")) return false;
		input.approachDirectionWorld = glm::normalize(glm::vec3(0.0f, -1.0f, -0.25f));
		std::vector<VansWorldQueryRequest> approachRequests;
		if (!Check(runtime.Prepare(workspace, input, approachRequests)
			&& !approachRequests.empty()
			&& glm::dot(approachRequests.front().directionWorld,
				input.approachDirectionWorld) > 0.9999f,
			"Grounding query ignored the explicit Approach Direction")) return false;
		input.approachDirectionWorld = glm::vec3(0.0f, -1.0f, 0.0f);
		std::vector<VansProceduralGoal> goals;
		VansProceduralSolverResult result;
		if (!Check(ResolveGround(runtime, workspace, input, glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f), goals, result), "Flat Grounding resolve failed")) return false;
		const VansProceduralGoal& flatGoal = goals[static_cast<std::size_t>(fixture.rig.FindGoal("leftFoot"))];
		if (!Check(flatGoal.valid && result.status == VansProceduralSolverStatus::Solved,
			"Flat plane did not produce a solved foot goal")) return false;
		float minimumHeight = 1000.0f;
		for (const VansRigSoleSample& sample : fixture.rig.contacts.front().soleSamplesLocal)
			minimumHeight = std::min(minimumHeight,
				(flatGoal.positionModel + flatGoal.rotationModel * sample.positionLocal).y);
		if (!Check(minimumHeight >= -1.0e-4f && minimumHeight <= 1.0e-3f,
			"Flat Grounding did not clear all rotated sole samples")) return false;
		const VansCompiledRigChain& groundedChain = fixture.rig.chains.front();
		const VansProceduralSolverResult groundedLimbResult = VansLimbIKSolver::Solve(
			workspace, fixture.rig, groundedChain, flatGoal);
		if (!Check(groundedLimbResult.status == VansProceduralSolverStatus::Solved
			&& groundedLimbResult.requestedPositionError <= 0.002f
			&& !groundedLimbResult.softReachApplied,
			"Grounding left its foot goal inside the Limb soft-reach attenuation zone"))
			return false;

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		const glm::vec3 slopeNormal = glm::normalize(glm::vec3(0.0f, 1.0f, -0.35f));
		if (!Check(ResolveGround(runtime, workspace, input, slopeNormal,
			glm::vec3(0.0f), goals, result), "Slope Grounding resolve failed")) return false;
		const VansProceduralGoal& slopeGoal = goals[static_cast<std::size_t>(fixture.rig.FindGoal("leftFoot"))];
		if (!Check(slopeGoal.valid && glm::dot(
			glm::normalize(slopeGoal.rotationModel * fixture.rig.contacts.front().soleNormalLocal),
			slopeNormal) > 0.999f, "Slope Grounding did not align the authored sole normal")) return false;
		for (const VansRigSoleSample& sample : fixture.rig.contacts.front().soleSamplesLocal)
		{
			const glm::vec3 samplePosition = slopeGoal.positionModel
				+ slopeGoal.rotationModel * sample.positionLocal;
			if (!Check(glm::dot(samplePosition, slopeNormal) >= -1.0e-4f,
				"Slope Grounding left a rotated sole sample below the support plane")) return false;
		}

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		input.ownerWorld = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 0.5f));
		if (!Check(ResolveGround(runtime, workspace, input, slopeNormal,
			glm::vec3(0.0f), goals, result),
			"Non-uniform-scale Grounding resolve failed")) return false;
		const VansProceduralGoal& scaledSlopeGoal = goals[
			static_cast<std::size_t>(fixture.rig.FindGoal("leftFoot"))];
		const glm::mat3 ownerLinear(input.ownerWorld);
		const glm::vec3 scaledSoleNormalWorld = glm::normalize(
			glm::transpose(glm::inverse(ownerLinear))
			* (scaledSlopeGoal.rotationModel
				* fixture.rig.contacts.front().soleNormalLocal));
		if (!Check(scaledSlopeGoal.valid
			&& glm::dot(scaledSoleNormalWorld, slopeNormal) > 0.999f,
			"Grounding transformed a world plane normal incorrectly under non-uniform owner scale"))
			return false;
		for (const VansRigSoleSample& sample : fixture.rig.contacts.front().soleSamplesLocal)
		{
			const glm::vec3 sampleModel = scaledSlopeGoal.positionModel
				+ scaledSlopeGoal.rotationModel * sample.positionLocal;
			const glm::vec3 sampleWorld = glm::vec3(
				input.ownerWorld * glm::vec4(sampleModel, 1.0f));
			if (!Check(glm::dot(sampleWorld, slopeNormal) >= -1.0e-4f,
				"Non-uniform-scale Grounding left a sole sample below the world plane"))
				return false;
		}
		input.ownerWorld = glm::mat4(1.0f);

		VansGroundingSettings unreachableSettings = GroundingSettings(false);
		unreachableSettings.query.maxStepDown = 3.0f;
		// Keep this fixture focused on the post-pelvis reach envelope. The
		// production clearance gate would otherwise discard a support one metre
		// below the sole before a limb goal is submitted.
		unreachableSettings.alignment.contactFadeHeight = 3.0f;
		unreachableSettings.pelvis.maxUpOffset = 0.0f;
		unreachableSettings.pelvis.maxDownOffset = 0.0f;
		VansCompiledGroundingSettings unreachableCompiled;
		if (!VansCompileGroundingSettings(
			unreachableSettings, fixture.rig, unreachableCompiled, error))
			return Check(false, error.c_str());
		VansGroundingRuntime unreachableRuntime;
		if (!unreachableRuntime.Configure(fixture.rig, unreachableCompiled, error))
			return Check(false, error.c_str());
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		if (!Check(ResolveGround(unreachableRuntime, workspace, input,
			glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), goals, result)
			&& !goals[static_cast<std::size_t>(fixture.rig.FindGoal("leftFoot"))].valid
			&& result.status == VansProceduralSolverStatus::Clamped
			&& HasLimit(result, VansProceduralLimitReason::Reach),
			"Grounding submitted a foot goal outside the post-pelvis limb reach envelope"))
			return false;

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		std::vector<VansWorldQueryRequest> requests;
		runtime.Prepare(workspace, input, requests);
		auto stairResults = BuildPlaneResults(requests, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f));
		for (std::size_t index = stairResults.size() / 2; index < stairResults.size(); ++index)
			stairResults[index].positionWorld.y = 0.20f;
		const bool stairResolved = runtime.Resolve(
			1.0f / 60.0f, workspace, input, stairResults, goals, result);
		if (stairResolved) runtime.CommitResolvedState();
		else runtime.RollbackResolvedState();
		if (!Check(stairResolved
			&& !goals[static_cast<std::size_t>(fixture.rig.FindGoal("leftFoot"))].valid
			&& HasLimit(result, VansProceduralLimitReason::Query),
			"Stair-edge samples fabricated a false support plane")) return false;

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		runtime.Prepare(workspace, input, requests);
		auto incompleteResults = BuildPlaneResults(
			requests, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f));
		incompleteResults.pop_back();
		const bool incompleteResolved = runtime.Resolve(
			1.0f / 60.0f, workspace, input, incompleteResults, goals, result);
		if (incompleteResolved) runtime.CommitResolvedState();
		else runtime.RollbackResolvedState();
		if (!Check(incompleteResolved
			&& !goals[static_cast<std::size_t>(fixture.rig.FindGoal("leftFoot"))].valid
			&& HasLimit(result, VansProceduralLimitReason::Query),
			"Grounding accepted a support plane without every authored sole sample"))
			return false;

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		if (!Check(ResolveGround(runtime, workspace, input,
			glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.30f, 0.0f), goals, result)
			&& workspace.GetComponentPosition(
				fixture.rig.semanticBoneIndices.at("pelvis")).y > 1.20f,
			"Grounding pelvis feasibility did not permit an upward step correction"))
			return false;

		// Grounding distances are world units. A centimetre-authored character with
		// owner scale 0.01 must still receive the configured 0.30 m pelvis range.
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		input.ownerWorld = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
		if (!Check(ResolveGround(runtime, workspace, input,
			glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.30f, 0.0f), goals, result)
			&& workspace.GetComponentPosition(
				fixture.rig.semanticBoneIndices.at("pelvis")).y * 0.01f > 0.25f,
			"Grounding pelvis bounds were interpreted in skeleton units instead of world units"))
			return false;
		input.ownerWorld = glm::mat4(1.0f);

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		input.grounded = false;
		input.airborne = true;
		std::vector<VansWorldQueryRequest> airborneRequests;
		const bool airbornePrepared = runtime.Prepare(workspace, input, airborneRequests);
		const bool airborneResolved = airbornePrepared
			&& runtime.Resolve(1.0f / 60.0f, workspace, input, {}, goals, result);
		if (airborneResolved) runtime.CommitResolvedState();
		else runtime.RollbackResolvedState();
		if (!Check(airbornePrepared && airborneRequests.empty() && airborneResolved
			&& result.status == VansProceduralSolverStatus::NoEffect
			&& !goals[static_cast<std::size_t>(fixture.rig.FindGoal("leftFoot"))].valid,
			"Airborne Grounding issued queries, reported a false query clamp, or retained a foot goal"))
			return false;
		return true;
	}

	bool TestGroundingContactWeightingAndStaticSeams()
	{
		LegFixture fixture;
		if (!BuildLegFixture(fixture, false)) return false;
		std::string error;
		VansPoseWorkspace workspace;
		std::vector<VansProceduralGoal> goals;
		VansProceduralSolverResult result;

		VansGroundingSettings swingSettings = GroundingSettings(true);
		swingSettings.alignment.rotationWeight = 0.65f;
		VansCompiledGroundingSettings swingCompiled;
		if (!VansCompileGroundingSettings(swingSettings, fixture.rig, swingCompiled, error))
			return Check(false, error.c_str());
		VansGroundingRuntime swingRuntime;
		if (!swingRuntime.Configure(fixture.rig, swingCompiled, error))
			return Check(false, error.c_str());
		std::vector<VansBoneTransform> swingPose = fixture.localPose;
		swingPose[static_cast<std::size_t>(fixture.rig.contacts.front().footBoneIndex)]
			.translation.y += 0.40f;
		VansAnimationExternalInputSnapshot swingInput;
		swingInput.ownerId = 11;
		swingInput.contacts.push_back({ "locomotion", "leftFoot", 0.0f, 0.0f, true });
		workspace.Initialize(fixture.skeleton, swingPose);
		if (!ResolveGround(swingRuntime, workspace, swingInput,
			glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.10f, 0.0f), goals, result))
			return Check(false, "Swing-foot Grounding resolve failed");
		const int footGoalIndex = fixture.rig.FindGoal("leftFoot");
		if (!Check(!goals[static_cast<std::size_t>(footGoalIndex)].valid,
			"Low-confidence swing foot retained a Grounding goal beyond clearance fade"))
			return false;

		swingInput.contacts.front().phase = 1.0f;
		swingInput.contacts.front().confidence = 1.0f;
		workspace.Initialize(fixture.skeleton, swingPose);
		if (!ResolveGround(swingRuntime, workspace, swingInput,
			glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.10f, 0.0f), goals, result))
			return Check(false, "High-confidence contact Grounding resolve failed");
		const VansProceduralGoal& contactGoal = goals[static_cast<std::size_t>(footGoalIndex)];
		if (!Check(contactGoal.valid && contactGoal.positionWeight > 0.99f
			&& std::abs(contactGoal.rotationWeight
				- contactGoal.positionWeight * swingSettings.alignment.rotationWeight) <= 1.0e-5f,
			"Contact confidence or independent Grounding rotation weight was ignored"))
			return false;

		VansGroundingSettings smoothSettings = GroundingSettings(false);
		smoothSettings.alignment.normalHalfLife = 0.10f;
		smoothSettings.alignment.rotationWeight = 1.0f;
		VansCompiledGroundingSettings smoothCompiled;
		if (!VansCompileGroundingSettings(smoothSettings, fixture.rig, smoothCompiled, error))
			return Check(false, error.c_str());
		VansGroundingRuntime smoothRuntime;
		if (!smoothRuntime.Configure(fixture.rig, smoothCompiled, error))
			return Check(false, error.c_str());
		VansAnimationExternalInputSnapshot smoothInput;
		smoothInput.ownerId = 13;
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		if (!ResolveGround(smoothRuntime, workspace, smoothInput,
			glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f), goals, result))
			return Check(false, "Ground normal smoothing setup failed");
		const glm::vec3 rawSlope = glm::normalize(glm::vec3(0.0f, 1.0f, -0.35f));
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		if (!ResolveGround(smoothRuntime, workspace, smoothInput,
			rawSlope, glm::vec3(0.0f), goals, result))
			return Check(false, "Ground normal smoothing step failed");
		glm::vec3 solvedNormal = glm::normalize(
			goals[static_cast<std::size_t>(footGoalIndex)].rotationModel
			* fixture.rig.contacts.front().soleNormalLocal);
		const float flatSlopeAgreement = glm::dot(glm::vec3(0.0f, 1.0f, 0.0f), rawSlope);
		if (!Check(glm::dot(solvedNormal, rawSlope) > flatSlopeAgreement
			&& glm::dot(solvedNormal, rawSlope) < 0.999f,
			"Grounding normal target snapped instead of using its configured half-life"))
			return false;
		for (int frame = 0; frame < 60; ++frame)
		{
			workspace.Initialize(fixture.skeleton, fixture.localPose);
			if (!ResolveGround(smoothRuntime, workspace, smoothInput,
				rawSlope, glm::vec3(0.0f), goals, result))
				return Check(false, "Ground normal smoothing convergence failed");
		}
		solvedNormal = glm::normalize(
			goals[static_cast<std::size_t>(footGoalIndex)].rotationModel
			* fixture.rig.contacts.front().soleNormalLocal);
		if (!Check(glm::dot(solvedNormal, rawSlope) > 0.999f,
			"Grounding normal half-life did not converge to the support normal"))
			return false;

		VansGroundingSettings seamSettings = GroundingSettings(false);
		VansCompiledGroundingSettings seamCompiled;
		if (!VansCompileGroundingSettings(seamSettings, fixture.rig, seamCompiled, error))
			return Check(false, error.c_str());
		VansGroundingRuntime seamRuntime;
		if (!seamRuntime.Configure(fixture.rig, seamCompiled, error))
			return Check(false, error.c_str());
		VansAnimationExternalInputSnapshot seamInput;
		seamInput.ownerId = 12;
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		std::vector<VansWorldQueryRequest> requests;
		if (!seamRuntime.Prepare(workspace, seamInput, requests))
			return Check(false, "Static seam Grounding Prepare failed");
		const VansSupportHandle staticA{ 100, 1 };
		const VansSupportHandle staticB{ 101, 1 };
		auto staticSeam = BuildPlaneResults(requests, glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f), staticA, glm::vec3(0.0f), true);
		for (std::size_t index = 0; index < staticSeam.size(); ++index)
		{
			if (index >= staticSeam.size() / 2) staticSeam[index].support = staticB;
			staticSeam[index].supportMovable = false;
		}
		if (!seamRuntime.Resolve(1.0f / 60.0f, workspace, seamInput, staticSeam, goals, result))
			return Check(false, "Static seam Grounding Resolve failed");
		seamRuntime.CommitResolvedState();
		if (!Check(goals[static_cast<std::size_t>(footGoalIndex)].valid
			&& !HasLimit(result, VansProceduralLimitReason::Query),
			"Coplanar static floor actors were treated as different moving supports"))
			return false;

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		if (!seamRuntime.Prepare(workspace, seamInput, requests))
			return Check(false, "Moving seam Grounding Prepare failed");
		auto movingSeam = BuildPlaneResults(requests, glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f), staticA, glm::vec3(0.0f), true);
		for (std::size_t index = 0; index < movingSeam.size(); ++index)
		{
			if (index >= movingSeam.size() / 2) movingSeam[index].support = staticB;
			movingSeam[index].supportMovable = true;
		}
		if (!seamRuntime.Resolve(1.0f / 60.0f, workspace, seamInput, movingSeam, goals, result))
			return Check(false, "Moving seam Grounding Resolve failed");
		seamRuntime.CommitResolvedState();
		return Check(!goals[static_cast<std::size_t>(footGoalIndex)].valid
			&& HasLimit(result, VansProceduralLimitReason::Query),
			"Grounding averaged sole samples across distinct movable supports");
	}

	bool TestGroundingMovingSupport()
	{
		LegFixture fixture;
		if (!BuildLegFixture(fixture, false)) return false;
		VansCompiledGroundingSettings compiled;
		std::string error;
		VansGroundingSettings movingSettings = GroundingSettings(true);
		movingSettings.plant.weightHalfLife = 0.05f;
		if (!VansCompileGroundingSettings(movingSettings, fixture.rig, compiled, error))
			return Check(false, error.c_str());
		VansGroundingRuntime runtime;
		if (!runtime.Configure(fixture.rig, compiled, error)) return Check(false, error.c_str());
		VansPoseWorkspace workspace;
		VansAnimationExternalInputSnapshot input;
		input.ownerId = 9;
		input.contacts.push_back({ "locomotion", "leftFoot", 1.0f, 1.0f, true });
		const VansSupportHandle support{ 42, 1 };
		std::vector<VansProceduralGoal> goals;
		VansProceduralSolverResult result;

		VansGroundingRuntime zeroStepRuntime;
		if (!zeroStepRuntime.Configure(fixture.rig, compiled, error))
			return Check(false, error.c_str());
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		std::vector<VansWorldQueryRequest> zeroStepRequests;
		if (!zeroStepRuntime.Prepare(workspace, input, zeroStepRequests)
			|| !zeroStepRuntime.Resolve(0.0f, workspace, input,
				BuildPlaneResults(zeroStepRequests, glm::vec3(0.0f, 1.0f, 0.0f),
					glm::vec3(0.0f), support, glm::vec3(0.0f), true),
				goals, result))
			return Check(false, "Zero-duration Grounding evaluation failed");
		zeroStepRuntime.CommitResolvedState();
		const int footGoalIndex = fixture.rig.FindGoal("leftFoot");
		if (!Check(result.status == VansProceduralSolverStatus::NoEffect
			&& !goals[static_cast<std::size_t>(footGoalIndex)].valid,
			"Zero-duration Grounding advanced contact weight or emitted a persistent goal"))
			return false;

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		if (!zeroStepRuntime.Prepare(workspace, input, zeroStepRequests)
			|| !zeroStepRuntime.Resolve(1.0f / 60.0f, workspace, input,
				BuildPlaneResults(zeroStepRequests, glm::vec3(0.0f, 1.0f, 0.0f),
					glm::vec3(0.0f), support, glm::vec3(0.0f), true),
				goals, result))
			return Check(false, "Post-zero-duration Grounding evaluation failed");
		zeroStepRuntime.CommitResolvedState();
		const float postZeroWeight = goals[static_cast<std::size_t>(footGoalIndex)].positionWeight;

		VansGroundingRuntime freshRuntime;
		if (!freshRuntime.Configure(fixture.rig, compiled, error))
			return Check(false, error.c_str());
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		std::vector<VansWorldQueryRequest> freshRequests;
		std::vector<VansProceduralGoal> freshGoals;
		VansProceduralSolverResult freshResult;
		if (!freshRuntime.Prepare(workspace, input, freshRequests)
			|| !freshRuntime.Resolve(1.0f / 60.0f, workspace, input,
				BuildPlaneResults(freshRequests, glm::vec3(0.0f, 1.0f, 0.0f),
					glm::vec3(0.0f), support, glm::vec3(0.0f), true),
				freshGoals, freshResult))
			return Check(false, "Fresh Grounding comparison failed");
		freshRuntime.CommitResolvedState();
		if (!Check(freshGoals[static_cast<std::size_t>(footGoalIndex)].valid
			&& std::abs(postZeroWeight
				- freshGoals[static_cast<std::size_t>(footGoalIndex)].positionWeight) <= 1.0e-6f,
			"Zero-duration Grounding changed the next persistent state transition"))
			return false;

		for (int frame = 0; frame < 2; ++frame)
		{
			workspace.Initialize(fixture.skeleton, fixture.localPose);
			if (!ResolveGround(runtime, workspace, input, glm::vec3(0.0f, 1.0f, 0.0f),
				glm::vec3(0.0f), goals, result, support, glm::vec3(0.0f), true))
				return Check(false, "Moving support plant setup failed");
		}
		const float lockedHeight = goals[static_cast<std::size_t>(
			fixture.rig.FindGoal("leftFoot"))].positionModel.y;
		const float lockedWeight = goals[static_cast<std::size_t>(
			fixture.rig.FindGoal("leftFoot"))].positionWeight;
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		std::vector<VansWorldQueryRequest> requests;
		if (!runtime.Prepare(workspace, input, requests))
			return Check(false, "Grounding transaction Prepare failed");
		const VansSupportHandle wrongSupport{ 77, 1 };
		if (!runtime.Resolve(1.0f / 60.0f, workspace, input,
			BuildPlaneResults(requests, glm::vec3(0.0f, 1.0f, 0.0f),
				glm::vec3(0.0f, 0.20f, 0.0f), wrongSupport, glm::vec3(0.0f), true),
			goals, result))
			return Check(false, "Grounding transaction Resolve failed");
		const VansProceduralGoal& replantGoal = goals[static_cast<std::size_t>(
			fixture.rig.FindGoal("leftFoot"))];
		if (!Check(replantGoal.valid
			&& std::abs(replantGoal.positionModel.y - lockedHeight) <= 1.0e-3f
			&& replantGoal.positionWeight < lockedWeight
			&& replantGoal.positionWeight > 0.0f,
			"Grounding support change popped to a new target instead of fading the stable lock"))
			return false;
		runtime.RollbackResolvedState();
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		if (!runtime.Prepare(workspace, input, requests)
			|| !runtime.Resolve(1.0f / 60.0f, workspace, input,
				BuildPlaneResults(requests, glm::vec3(0.0f, 1.0f, 0.0f),
					glm::vec3(0.0f, 0.20f, 0.0f), support, glm::vec3(0.0f), true),
				goals, result))
			return Check(false, "Grounding transaction restore failed");
		const float restoredHeight = goals[static_cast<std::size_t>(
			fixture.rig.FindGoal("leftFoot"))].positionModel.y;
		runtime.CommitResolvedState();
		if (!Check(std::abs(restoredHeight - lockedHeight) <= 1.0e-3f,
			"Grounding rollback retained a failed downstream replant state")) return false;
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		if (!ResolveGround(runtime, workspace, input, glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f), goals, result, support, glm::vec3(0.10f, 0.0f, 0.0f), true))
			return Check(false, "Moving support update failed");
		const VansProceduralGoal& movedGoal = goals[static_cast<std::size_t>(fixture.rig.FindGoal("leftFoot"))];
		if (!Check(movedGoal.valid && std::abs(movedGoal.positionModel.x - 0.10f) <= 1.0e-3f,
			"Planted foot did not follow its stable moving-support handle")) return false;

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		if (!runtime.Prepare(workspace, input, requests)
			|| !runtime.Resolve(1.0f / 60.0f, workspace, input,
				BuildPlaneResults(requests, glm::vec3(0.0f, 1.0f, 0.0f),
					glm::vec3(0.0f), support, glm::vec3(0.20f, 0.0f, 0.0f), true),
				goals, result))
			return Check(false, "Grounding downstream-result setup failed");
		const float rejectedLockX = goals[static_cast<std::size_t>(
			fixture.rig.FindGoal("leftFoot"))].positionModel.x;
		const float rejectedWeight = goals[static_cast<std::size_t>(
			fixture.rig.FindGoal("leftFoot"))].positionWeight;
		VansProceduralSolverResult rejectedSolve;
		rejectedSolve.status = VansProceduralSolverStatus::Clamped;
		rejectedSolve.limitReason = VansProceduralLimitReason::Joint;
		runtime.ReportLimbSolve(fixture.rig.contacts.front().chainIndex, rejectedSolve);
		runtime.CommitResolvedState();

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		if (!ResolveGround(runtime, workspace, input, glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f), goals, result, support, glm::vec3(0.30f, 0.0f, 0.0f), true))
			return Check(false, "Grounding downstream-result feedback failed");
		const VansProceduralGoal& rejectedGoal = goals[static_cast<std::size_t>(
			fixture.rig.FindGoal("leftFoot"))];
		return Check(rejectedGoal.valid
			&& std::abs(rejectedGoal.positionModel.x - rejectedLockX) <= 1.0e-3f
			&& rejectedGoal.positionWeight < rejectedWeight,
			"Grounding committed a false Plant lock after downstream Limb clamping");
	}

	bool TestJointConstraintMath()
	{
		VansCompiledRigJointLimit limit;
		limit.kind = VansJointLimitKind::Locked;
		limit.restLocalRotation = glm::angleAxis(
			glm::radians(15.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		VansConstraintResult result = VansApplyJointLimit(
			glm::angleAxis(glm::radians(75.0f), glm::vec3(0.0f, 1.0f, 0.0f)), &limit);
		if (!Check(result.valid && result.limited
			&& VansQuaternionAngleDegrees(glm::inverse(limit.restLocalRotation)
				* result.rotation) <= 1.0e-3f,
			"Locked joint did not resolve to its compiled rest rotation")) return false;

		limit = {};
		limit.kind = VansJointLimitKind::Hinge;
		limit.axisLocal = glm::vec3(0.0f, 1.0f, 0.0f);
		limit.minDegrees = -10.0f;
		limit.maxDegrees = 20.0f;
		result = VansApplyJointLimit(
			glm::angleAxis(glm::radians(40.0f), limit.axisLocal), &limit);
		if (!Check(result.valid && result.limited
			&& std::abs(VansQuaternionAngleDegrees(result.rotation) - 20.0f) <= 1.0e-3f,
			"Hinge joint did not clamp twist to its authored interval")) return false;

		limit = {};
		limit.kind = VansJointLimitKind::SwingTwist;
		limit.axisLocal = glm::vec3(0.0f, 1.0f, 0.0f);
		limit.swingReferenceAxisLocal = glm::vec3(1.0f, 0.0f, 0.0f);
		limit.swingLimitDegrees = glm::vec2(30.0f, 10.0f);
		result = VansApplyJointLimit(
			glm::angleAxis(glm::radians(60.0f), glm::vec3(1.0f, 0.0f, 0.0f)), &limit);
		if (!Check(result.valid && result.limited
			&& std::abs(VansQuaternionAngleDegrees(result.rotation) - 30.0f) <= 1.0e-3f,
			"Swing-Twist joint did not clamp its swing cone")) return false;

		limit.kind = static_cast<VansJointLimitKind>(255);
		if (!Check(!VansApplyJointLimit(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), &limit).valid,
			"Joint constraint math silently interpreted an invalid kind")) return false;
		limit.kind = VansJointLimitKind::Hinge;
		limit.minDegrees = std::numeric_limits<float>::quiet_NaN();
		return Check(!VansApplyJointLimit(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), &limit).valid,
			"Joint constraint math accepted non-finite authored bounds");
	}

	bool TestRigValidationAndLimbSolver()
	{
		LegFixture fixture;
		if (!BuildLegFixture(fixture, true)) return false;
		VansAnimationRigAsset invalid = fixture.asset;
		invalid.contacts.front().soleNormalLocal = glm::vec3(1.0f, 0.0f, 0.0f);
		VansCompiledAnimationRig rejectedRig;
		std::string error;
		if (!Check(!VansAnimationRigCompiler::Compile(
			invalid, fixture.skeleton, rejectedRig, error),
			"Rig compiler accepted a sole normal that disagrees with its sample plane")) return false;
		invalid = fixture.asset;
		invalid.goals.push_back({ "otherFootGoal", "foot_l" });
		invalid.chains.front().goal = "otherFootGoal";
		if (!Check(!VansAnimationRigCompiler::Compile(
			invalid, fixture.skeleton, rejectedRig, error),
			"Rig compiler accepted a contact whose ID does not own the chain goal")) return false;

		nlohmann::json rigJson;
		if (!VansAnimationRigStorage::SerializeToJsonObject(fixture.asset, rigJson, error))
			return Check(false, error.c_str());
		VansAnimationRigAsset socketRig = fixture.asset;
		const int socketBoneIndex = fixture.skeleton.boneNameToIndex.at("foot_l");
		fixture.skeleton.bones[static_cast<std::size_t>(socketBoneIndex)].guid =
			"00000000-0000-4000-8000-000000000003";
		fixture.skeleton.RebuildIdentityMapsAndSignature();
		VansRigSocketDefinition socket;
		socket.guid = "00000000-0000-4000-8000-000000000002";
		socket.name = "Weapon";
		socket.boneGuid = fixture.skeleton.bones[static_cast<std::size_t>(socketBoneIndex)].guid;
		socket.positionLocal = glm::vec3(0.12f, -0.34f, 0.56f);
		socket.rotationLocal = glm::angleAxis(
			glm::radians(37.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));
		socket.scaleLocal = glm::vec3(0.8f, 1.1f, 1.25f);
		socketRig.sockets.push_back(socket);
		VansRigAttachmentProfileDefinition attachmentProfile;
		attachmentProfile.modelGuid = "00000000-0000-4000-8000-000000000004";
		attachmentProfile.parentKind = VansRigAttachmentParentKind::Socket;
		attachmentProfile.anchorGuid = socket.guid;
		attachmentProfile.positionLocal = glm::vec3(-0.4f, 0.2f, 0.7f);
		attachmentProfile.rotationLocal = glm::angleAxis(
			glm::radians(-24.0f), glm::normalize(glm::vec3(0.5f, 1.0f, -0.25f)));
		attachmentProfile.scaleLocal = glm::vec3(1.2f, 0.9f, 1.1f);
		socketRig.attachmentProfiles.push_back(attachmentProfile);
		VansCompiledAnimationRig attachmentRig;
		if (!VansAnimationRigCompiler::Compile(
			socketRig, fixture.skeleton, attachmentRig, error))
			return Check(false, error.c_str());
		nlohmann::json socketRigJson;
		if (!VansAnimationRigStorage::SerializeToJsonObject(socketRig, socketRigJson, error))
			return Check(false, error.c_str());
		VansAnimationRigAsset parsedSocketRig;
		if (!VansAnimationRigStorage::DeserializeFromJsonObject(
			socketRigJson, parsedSocketRig, error))
			return Check(false, error.c_str());
		if (!Check(parsedSocketRig.sockets.size() == 1
			&& parsedSocketRig.sockets.front().guid == socket.guid
			&& parsedSocketRig.sockets.front().boneGuid == socket.boneGuid
			&& glm::length(parsedSocketRig.sockets.front().positionLocal
				- socket.positionLocal) <= 1.0e-6f
			&& std::abs(glm::dot(parsedSocketRig.sockets.front().rotationLocal,
				socket.rotationLocal)) >= 1.0f - 1.0e-6f
			&& glm::length(parsedSocketRig.sockets.front().scaleLocal
				- socket.scaleLocal) <= 1.0e-6f
			&& parsedSocketRig.attachmentProfiles.size() == 1
			&& parsedSocketRig.attachmentProfiles.front().modelGuid
				== attachmentProfile.modelGuid
			&& parsedSocketRig.attachmentProfiles.front().parentKind
				== VansRigAttachmentParentKind::Socket
			&& parsedSocketRig.attachmentProfiles.front().anchorGuid == socket.guid
			&& glm::length(parsedSocketRig.attachmentProfiles.front().positionLocal
				- attachmentProfile.positionLocal) <= 1.0e-6f
			&& std::abs(glm::dot(
				parsedSocketRig.attachmentProfiles.front().rotationLocal,
				attachmentProfile.rotationLocal)) >= 1.0f - 1.0e-6f
			&& glm::length(parsedSocketRig.attachmentProfiles.front().scaleLocal
				- attachmentProfile.scaleLocal) <= 1.0e-6f,
			"Rig storage did not round-trip Socket and attachment-profile local transforms"))
			return false;
		nlohmann::json missingAttachmentProfiles = rigJson;
		missingAttachmentProfiles.erase("attachmentProfiles");
		VansAnimationRigAsset missingAttachmentProfilesRig;
		if (!Check(!VansAnimationRigStorage::DeserializeFromJsonObject(
			missingAttachmentProfiles, missingAttachmentProfilesRig, error),
			"Rig storage defaulted the canonical attachmentProfiles field")) return false;
		nlohmann::json incompleteRigJson = rigJson;
		incompleteRigJson["chains"][0].erase("softReachStartRatio");
		VansAnimationRigAsset incompleteRig;
		if (!Check(!VansAnimationRigStorage::DeserializeFromJsonObject(
			incompleteRigJson, incompleteRig, error),
			"Rig storage defaulted a missing canonical Limb field")) return false;
		invalid = fixture.asset;
		invalid.chains.front().solver = static_cast<VansRigSolverKind>(255);
		if (!Check(!VansAnimationRigCompiler::Compile(
			invalid, fixture.skeleton, rejectedRig, error),
			"Rig compiler silently interpreted an invalid solver enum")) return false;
		if (!Check(!VansAnimationRigStorage::SerializeToJsonObject(invalid, rigJson, error),
			"Rig storage silently rewrote an invalid solver enum")) return false;
		invalid = fixture.asset;
		invalid.jointLimits.push_back({});
		invalid.jointLimits.back().kind = static_cast<VansJointLimitKind>(255);
		if (!Check(!VansAnimationRigCompiler::Compile(
			invalid, fixture.skeleton, rejectedRig, error),
			"Rig compiler silently interpreted an invalid joint limit enum")) return false;
		if (!Check(!VansAnimationRigStorage::SerializeToJsonObject(invalid, rigJson, error),
			"Rig storage silently rewrote an invalid joint limit enum")) return false;
		if (!VansAnimationRigStorage::SerializeToJsonObject(fixture.asset, rigJson, error))
			return Check(false, error.c_str());
		rigJson["schemaVersion"] = 1;
		VansAnimationRigAsset parsedRig;
		if (!Check(!VansAnimationRigStorage::DeserializeFromJsonObject(rigJson, parsedRig, error),
			"Rig storage accepted a generation/version field")) return false;

		VansPoseWorkspace workspace;
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		const VansCompiledRigChain& chain = fixture.rig.chains.front();
		VansProceduralGoal reachable;
		reachable.valid = true;
		reachable.positionModel = workspace.GetComponentPosition(chain.boneIndices.front())
			+ glm::vec3(0.25f, -0.70f, 0.05f);
		const VansProceduralSolverResult singularResult = VansLimbIKSolver::Solve(
			workspace, fixture.rig, chain, reachable);
		if (!Check((singularResult.status == VansProceduralSolverStatus::Solved
			|| singularResult.status == VansProceduralSolverStatus::Clamped)
			&& workspace.IsFinite(),
			"Limb IK failed a straight-chain singularity with an authored pole axis")) return false;

		LegFixture bentFixture;
		if (!BuildLegFixture(bentFixture, false)) return false;
		workspace.Initialize(bentFixture.skeleton, bentFixture.localPose);
		const VansCompiledRigChain& bentChain = bentFixture.rig.chains.front();
		const glm::vec3 bentRoot = workspace.GetComponentPosition(bentChain.boneIndices.front());
		const int bentRootParent = bentFixture.skeleton.bones[static_cast<std::size_t>(
			bentChain.boneIndices.front())].parentIndex;
		const glm::vec3 authoredPole = workspace.GetComponentRotation(bentRootParent)
			* bentChain.poleAxisParentLocal;
		VansProceduralGoal poleGoal;
		poleGoal.valid = true;
		poleGoal.positionModel = bentRoot + glm::vec3(0.0f, -0.75f, 0.0f);
		const VansProceduralSolverResult poleResult = VansLimbIKSolver::Solve(
			workspace, bentFixture.rig, bentChain, poleGoal);
		const glm::vec3 targetDirection = glm::normalize(poleGoal.positionModel - bentRoot);
		glm::vec3 solvedBend = workspace.GetComponentPosition(bentChain.boneIndices[1]) - bentRoot;
		solvedBend -= targetDirection * glm::dot(solvedBend, targetDirection);
		if (!Check((poleResult.status == VansProceduralSolverStatus::Solved
			|| poleResult.status == VansProceduralSolverStatus::Clamped)
			&& glm::length(solvedBend) > 1.0e-4f
			&& glm::dot(glm::normalize(solvedBend), glm::normalize(authoredPole)) > 0.99f,
			"Limb IK let the animated bend override the Rig-authored pole sidedness"))
			return false;

		workspace.Initialize(bentFixture.skeleton, bentFixture.localPose);
		const glm::vec3 upperAxisLocal = glm::normalize(
			workspace.GetLocal(bentChain.boneIndices[1]).translation);
		if (!workspace.SetLocalRotation(bentChain.boneIndices.front(),
			glm::angleAxis(glm::radians(60.0f), upperAxisLocal)))
			return Check(false, "Limb IK pole-stability fixture could not twist its source thigh");
		const VansProceduralSolverResult twistedPoleResult = VansLimbIKSolver::Solve(
			workspace, bentFixture.rig, bentChain, poleGoal);
		solvedBend = workspace.GetComponentPosition(bentChain.boneIndices[1]) - bentRoot;
		solvedBend -= targetDirection * glm::dot(solvedBend, targetDirection);
		glm::vec3 stablePole = authoredPole
			- targetDirection * glm::dot(authoredPole, targetDirection);
		if (!Check((twistedPoleResult.status == VansProceduralSolverStatus::Solved
			|| twistedPoleResult.status == VansProceduralSolverStatus::Clamped)
			&& glm::length(solvedBend) > 1.0e-4f && glm::length(stablePole) > 1.0e-4f
			&& glm::dot(glm::normalize(solvedBend), glm::normalize(stablePole)) > 0.99f,
			"Limb IK let source-pose thigh twist move the stable parent-space pole"))
			return false;

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		VansProceduralGoal unreachable = reachable;
		unreachable.positionModel = glm::vec3(0.0f, -10.0f, 0.0f);
		const VansProceduralSolverResult unreachableResult = VansLimbIKSolver::Solve(
			workspace, fixture.rig, chain, unreachable);
		if (!Check(unreachableResult.status == VansProceduralSolverStatus::Unreachable
			&& HasLimit(unreachableResult, VansProceduralLimitReason::Reach)
			&& workspace.IsFinite(), "Limb IK did not report a finite unreachable result")) return false;

		LegFixture unequal;
		if (!BuildLegFixture(unequal, false)) return false;
		unequal.skeleton.bones[static_cast<std::size_t>(
			unequal.skeleton.boneNameToIndex.at("calf_l"))].localTransform =
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.60f, 0.0f));
		unequal.skeleton.bones[static_cast<std::size_t>(
			unequal.skeleton.boneNameToIndex.at("foot_l"))].localTransform =
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.25f, 0.0f));
		if (!VansAnimationRigCompiler::Compile(
			unequal.asset, unequal.skeleton, unequal.rig, error))
			return Check(false, error.c_str());
		unequal.localPose = BuildLocalPose(unequal.skeleton);
		workspace.Initialize(unequal.skeleton, unequal.localPose);
		const VansCompiledRigChain& unequalChain = unequal.rig.chains.front();
		VansProceduralGoal innerUnreachable;
		innerUnreachable.valid = true;
		innerUnreachable.positionModel = workspace.GetComponentPosition(
			unequalChain.boneIndices.front()) + glm::vec3(0.0f, -0.05f, 0.0f);
		const VansProceduralSolverResult innerResult = VansLimbIKSolver::Solve(
			workspace, unequal.rig, unequalChain, innerUnreachable);
		if (!Check(innerResult.status == VansProceduralSolverStatus::Unreachable
			&& HasLimit(innerResult, VansProceduralLimitReason::Reach)
			&& workspace.IsFinite(),
			"Limb IK did not report a target inside the unequal limb's minimum reach")) return false;

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		const glm::vec3 rootPosition = workspace.GetComponentPosition(chain.boneIndices.front());
		const glm::vec3 initialTipPosition = workspace.GetComponentPosition(chain.boneIndices.back());
		const float naturalReach = glm::length(
			workspace.GetComponentPosition(chain.boneIndices[1]) - rootPosition)
			+ glm::length(initialTipPosition
				- workspace.GetComponentPosition(chain.boneIndices[1]));
		VansProceduralGoal softGoal;
		softGoal.valid = true;
		softGoal.positionModel = rootPosition
			+ glm::normalize(initialTipPosition - rootPosition) * naturalReach;
		const VansProceduralSolverResult softResult = VansLimbIKSolver::Solve(
			workspace, fixture.rig, chain, softGoal);
		if (!Check(softResult.status == VansProceduralSolverStatus::Solved
			&& softResult.softReachApplied && !softResult.stretchApplied
			&& softResult.effectivePositionError <= 1.0e-3f
			&& softResult.requestedPositionError > softResult.effectivePositionError,
			"Limb IK soft reach did not preserve bend while solving its effective target"))
			return false;

		VansAnimationRigAsset hardReachAsset = fixture.asset;
		hardReachAsset.chains.front().softReachStartRatio = 1.0f;
		VansCompiledAnimationRig hardReachRig;
		if (!VansAnimationRigCompiler::Compile(
			hardReachAsset, fixture.skeleton, hardReachRig, error))
			return Check(false, error.c_str());
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		const VansProceduralSolverResult hardReachResult = VansLimbIKSolver::Solve(
			workspace, hardReachRig, hardReachRig.chains.front(), softGoal);
		if (!Check(!hardReachResult.softReachApplied && workspace.IsFinite(),
			"Limb IK hard reach should not soften an attainable target")) return false;

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		VansProceduralGoal partialGoal = reachable;
		partialGoal.positionWeight = 0.35f;
		const VansProceduralSolverResult partialResult = VansLimbIKSolver::Solve(
			workspace, fixture.rig, chain, partialGoal);
		if (!Check(partialResult.status == VansProceduralSolverStatus::Solved
			&& partialResult.effectivePositionError > 1.0e-3f,
			"Limb IK misclassified an authored partial weight as a solver clamp")) return false;

		VansLimbIKSettings linearBlendSettings;
		linearBlendSettings.linearPoseBlend = true;
		VansProceduralGoal linearBlendGoal = reachable;
		linearBlendGoal.rotationModel = glm::angleAxis(
			glm::radians(48.0f), glm::normalize(glm::vec3(0.2f, 1.0f, 0.4f)));
		linearBlendGoal.rotationWeight = 1.0f;
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		std::array<VansBoneTransform, 3> originalLeg;
		for (std::size_t bone = 0; bone < originalLeg.size(); ++bone)
			originalLeg[bone] = workspace.GetLocal(chain.boneIndices[bone]);
		const VansProceduralSolverResult fullLinearResult = VansLimbIKSolver::Solve(
			workspace, fixture.rig, chain, linearBlendGoal, linearBlendSettings);
		if (!Check(fullLinearResult.status == VansProceduralSolverStatus::Solved,
			"Limb IK could not produce a full pose for linear blending")) return false;
		std::array<VansBoneTransform, 3> solvedLeg;
		for (std::size_t bone = 0; bone < solvedLeg.size(); ++bone)
			solvedLeg[bone] = workspace.GetLocal(chain.boneIndices[bone]);
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		constexpr float blendWeight = 0.35f;
		linearBlendGoal.positionWeight = blendWeight;
		linearBlendGoal.rotationWeight = blendWeight;
		const VansProceduralSolverResult blendedLinearResult = VansLimbIKSolver::Solve(
			workspace, fixture.rig, chain, linearBlendGoal, linearBlendSettings);
		if (!Check(blendedLinearResult.status == VansProceduralSolverStatus::Solved,
			"Limb IK rejected an authored linear pose blend")) return false;
		for (std::size_t bone = 0; bone < solvedLeg.size(); ++bone)
		{
			const VansBoneTransform actual = workspace.GetLocal(chain.boneIndices[bone]);
			glm::quat solvedRotation = solvedLeg[bone].rotation;
			if (glm::dot(solvedRotation, originalLeg[bone].rotation) < 0.0f)
				solvedRotation = -solvedRotation;
			const glm::quat expectedRotation = glm::normalize(
				solvedRotation * blendWeight
				+ originalLeg[bone].rotation * (1.0f - blendWeight));
			const float translationError = glm::length(actual.translation - glm::mix(
				originalLeg[bone].translation, solvedLeg[bone].translation, blendWeight));
			const float scaleError = glm::length(actual.scale - glm::mix(
				originalLeg[bone].scale, solvedLeg[bone].scale, blendWeight));
			const float rotationDot = std::abs(glm::dot(actual.rotation, expectedRotation));
			if (!Check(translationError < 1.0e-5f && scaleError < 1.0e-5f
				&& rotationDot > 1.0f - 1.0e-5f,
				"Limb IK partial weight did not blend the solved local leg pose linearly")) return false;
		}

		VansAnimationRigAsset stretchAsset = fixture.asset;
		stretchAsset.chains.front().maxStretchScale = 1.2f;
		VansCompiledAnimationRig stretchRig;
		if (!VansAnimationRigCompiler::Compile(
			stretchAsset, fixture.skeleton, stretchRig, error))
			return Check(false, error.c_str());
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		const VansCompiledRigChain& stretchChain = stretchRig.chains.front();
		VansProceduralGoal stretchGoal;
		stretchGoal.valid = true;
		stretchGoal.positionModel = workspace.GetComponentPosition(stretchChain.boneIndices.front())
			+ glm::normalize(initialTipPosition - rootPosition) * (naturalReach + 0.10f);
		const VansProceduralSolverResult stretchResult = VansLimbIKSolver::Solve(
			workspace, stretchRig, stretchChain, stretchGoal);
		if (!Check(stretchResult.status == VansProceduralSolverStatus::Solved
			&& stretchResult.softReachApplied && stretchResult.stretchApplied
			&& stretchResult.effectivePositionError <= 1.0e-3f,
			"Limb IK did not combine soft reach and explicit stretch continuously")) return false;

		VansAnimationRigAsset sourceGeometryAsset = bentFixture.asset;
		sourceGeometryAsset.chains.front().softReachStartRatio = 1.0f;
		sourceGeometryAsset.chains.front().maxStretchScale = 1.5f;
		VansCompiledAnimationRig sourceGeometryRig;
		if (!VansAnimationRigCompiler::Compile(sourceGeometryAsset,
			bentFixture.skeleton, sourceGeometryRig, error))
			return Check(false, error.c_str());
		const VansCompiledRigChain& sourceGeometryChain = sourceGeometryRig.chains.front();
		VansLimbIKSettings sourceGeometrySettings;
		sourceGeometrySettings.linearPoseBlend = true;
		const glm::vec3 sourceDirection = glm::normalize(glm::vec3(0.25f, -0.9f, 0.1f));
		for (float reachRatio : { 0.4f, 0.8f, 1.0f, 1.2f, 1.6f })
		{
			workspace.Initialize(bentFixture.skeleton, bentFixture.localPose);
			const glm::vec3 sourceRoot = workspace.GetComponentPosition(sourceGeometryChain.boneIndices[0]);
			const glm::vec3 sourceJoint = workspace.GetComponentPosition(sourceGeometryChain.boneIndices[1]);
			const glm::vec3 sourceEnd = workspace.GetComponentPosition(sourceGeometryChain.boneIndices[2]);
			float sourceUpper = glm::length(sourceJoint - sourceRoot);
			float sourceLower = glm::length(sourceEnd - sourceJoint);
			const float sourceReach = sourceUpper + sourceLower;
			const float desiredLength = sourceReach * reachRatio;
			VansProceduralGoal sourceGoal;
			sourceGoal.valid = true;
			sourceGoal.positionModel = sourceRoot + sourceDirection * desiredLength;
			sourceGoal.rotationWeight = 1.0f;
			sourceGoal.rotationModel = linearBlendGoal.rotationModel;
			sourceGoal.hasPoleTarget = true;
			sourceGoal.poleTargetModel = sourceRoot + glm::vec3(1.0f, 0.0f, 0.0f);
			const float scaleFactor = 1.0f + 0.5f * std::clamp(
				(reachRatio - 1.0f) / 0.5f, 0.0f, 1.0f);
			sourceUpper *= scaleFactor;
			sourceLower *= scaleFactor;
			const float scaledReach = sourceUpper + sourceLower;
			const float endDistance = std::min(desiredLength, scaledReach);
			glm::vec3 expectedJoint;
			if (desiredLength >= scaledReach)
				expectedJoint = sourceRoot + sourceDirection * sourceUpper;
			else
			{
				const glm::vec3 jointOffset = sourceGoal.poleTargetModel - sourceRoot;
				const glm::vec3 bend = glm::normalize(jointOffset
					- sourceDirection * glm::dot(jointOffset, sourceDirection));
				const float along = (sourceUpper * sourceUpper + desiredLength * desiredLength
					- sourceLower * sourceLower) / (2.0f * desiredLength);
				const float height = std::sqrt(std::max(0.0f,
					sourceUpper * sourceUpper - along * along));
				expectedJoint = sourceRoot + sourceDirection * along + bend * height;
			}
			const VansProceduralSolverResult geometryResult = VansLimbIKSolver::Solve(
				workspace, sourceGeometryRig, sourceGeometryChain, sourceGoal, sourceGeometrySettings);
			const float jointError = glm::length(
				workspace.GetComponentPosition(sourceGeometryChain.boneIndices[1]) - expectedJoint);
			const float endError = glm::length(
				workspace.GetComponentPosition(sourceGeometryChain.boneIndices[2])
				- (sourceRoot + sourceDirection * endDistance));
			if (jointError >= 2.0e-4f || endError >= 2.0e-4f)
				std::cerr << "Two-bone geometry reachRatio=" << reachRatio << " status="
					<< static_cast<int>(geometryResult.status) << " jointError=" << jointError
					<< " endError=" << endError << '\n';
			if (!Check((geometryResult.status == VansProceduralSolverStatus::Solved
					|| geometryResult.status == VansProceduralSolverStatus::Unreachable)
				&& jointError < 2.0e-4f && endError < 2.0e-4f,
				"Limb IK joint/end geometry diverged from the two-bone source equations")) return false;
		}

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		VansProceduralGoal rotationOnly;
		rotationOnly.valid = true;
		rotationOnly.positionWeight = 0.0f;
		rotationOnly.rotationWeight = 1.0f;
		rotationOnly.rotationModel = glm::angleAxis(glm::radians(70.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		const VansProceduralSolverResult rotationResult = VansLimbIKSolver::Solve(
			workspace, fixture.rig, chain, rotationOnly);
		if (!Check(rotationResult.status == VansProceduralSolverStatus::Solved
			&& rotationResult.rotationErrorDegrees <= 1.0e-3f,
			"Limb IK rotation-only goal was not applied exactly")) return false;

		VansAnimationRigAsset tipLimitedAsset = fixture.asset;
		VansRigJointLimitDefinition tipLimit;
		tipLimit.bone = "foot_l";
		tipLimit.kind = VansJointLimitKind::Hinge;
		tipLimit.axisLocal = glm::vec3(0.0f, 1.0f, 0.0f);
		tipLimit.minDegrees = -10.0f;
		tipLimit.maxDegrees = 10.0f;
		tipLimitedAsset.jointLimits.push_back(tipLimit);
		VansCompiledAnimationRig tipLimitedRig;
		if (!VansAnimationRigCompiler::Compile(
			tipLimitedAsset, fixture.skeleton, tipLimitedRig, error))
			return Check(false, error.c_str());
		workspace.Initialize(fixture.skeleton, fixture.localPose);
		const VansProceduralSolverResult limitedRotationResult = VansLimbIKSolver::Solve(
			workspace, tipLimitedRig, tipLimitedRig.chains.front(), rotationOnly);
		return Check(limitedRotationResult.status == VansProceduralSolverStatus::Clamped
			&& HasLimit(limitedRotationResult, VansProceduralLimitReason::Joint)
			&& limitedRotationResult.rotationErrorDegrees >= 59.0f,
			"Limb IK MatchGoal rotation bypassed the terminal joint constraint");
	}

	bool TestChainAndAimSolvers()
	{
		Skeleton skeleton;
		const int root = AddBone(skeleton, "root", -1, glm::vec3(0.0f));
		const int a = AddBone(skeleton, "a", root, glm::vec3(0.0f));
		const int b = AddBone(skeleton, "b", a, glm::vec3(1.0f, 0.0f, 0.0f));
		const int c = AddBone(skeleton, "c", b, glm::vec3(1.0f, 0.0f, 0.0f));
		AddBone(skeleton, "tip", c, glm::vec3(1.0f, 0.0f, 0.0f));
		skeleton.BuildTopologicalOrder();
		VansAnimationRigAsset asset;
		asset.name = "Chain Fixture";
		asset.skeletonGuid = "00000000-0000-4000-8000-000000000002";
		skeleton.sourceSkeletonGuid = asset.skeletonGuid;
		asset.goals.push_back({ "hand", "tip" });
		for (const auto& entry : { std::pair{ "ccd", VansRigSolverKind::CCD },
			std::pair{ "fabrik", VansRigSolverKind::FABRIK } })
		{
			VansRigChainDefinition chain;
			chain.id = entry.first;
			chain.solver = entry.second;
			chain.bones = { "a", "b", "c", "tip" };
			chain.goal = "hand";
			chain.solveWeights = { 1.0f, 1.0f, 1.0f };
			if (entry.second == VansRigSolverKind::CCD) chain.maxStepDegrees = 180.0f;
			asset.chains.push_back(chain);
		}
		VansCompiledAnimationRig rig;
		std::string error;
		if (!VansAnimationRigCompiler::Compile(asset, skeleton, rig, error))
			return Check(false, error.c_str());
		const std::vector<VansBoneTransform> pose = BuildLocalPose(skeleton);
		VansProceduralGoal goal;
		goal.valid = true;
		goal.positionModel = glm::vec3(1.6f, 1.2f, 0.0f);
		for (const char* chainId : { "ccd", "fabrik" })
		{
			VansPoseWorkspace workspace;
			workspace.Initialize(skeleton, pose);
			const VansCompiledRigChain& chain = rig.chains[static_cast<std::size_t>(rig.FindChain(chainId))];
			VansChainIKSettings settings;
			settings.maxIterations = 32;
			settings.positionTolerance = 0.002f;
			const VansProceduralSolverResult chainResult = VansChainIKSolver::Solve(
				workspace, rig, chain, goal, settings);
			if (!((chainResult.status == VansProceduralSolverStatus::Solved
				|| chainResult.status == VansProceduralSolverStatus::Clamped)
				&& workspace.IsFinite()
				&& glm::length(workspace.GetComponentPosition(chain.boneIndices.back())
					- goal.positionModel) <= 0.01f))
			{
				std::cerr << "Procedural animation contract failed: " << chainId
					<< " reachable solve status=" << static_cast<int>(chainResult.status)
					<< " error=" << chainResult.requestedPositionError << '\n';
				return false;
			}
		}
		VansAnimationRigAsset constrainedAsset = asset;
		VansRigJointLimitDefinition lockedRoot;
		lockedRoot.bone = "a";
		lockedRoot.kind = VansJointLimitKind::Locked;
		constrainedAsset.jointLimits.push_back(lockedRoot);
		VansCompiledAnimationRig constrainedRig;
		if (!VansAnimationRigCompiler::Compile(
			constrainedAsset, skeleton, constrainedRig, error))
			return Check(false, error.c_str());
		VansPoseWorkspace constrainedWorkspace;
		constrainedWorkspace.Initialize(skeleton, pose);
		VansProceduralGoal constrainedGoal;
		constrainedGoal.valid = true;
		constrainedGoal.positionModel = glm::vec3(1.0f, 1.5f, 0.0f);
		VansChainIKSettings constrainedSettings;
		constrainedSettings.maxIterations = 64;
		constrainedSettings.positionTolerance = 0.002f;
		const VansCompiledRigChain& constrainedChain = constrainedRig.chains[
			static_cast<std::size_t>(constrainedRig.FindChain("fabrik"))];
		const VansProceduralSolverResult constrainedResult = VansChainIKSolver::Solve(
			constrainedWorkspace, constrainedRig, constrainedChain,
			constrainedGoal, constrainedSettings);
		if (!(constrainedResult.status == VansProceduralSolverStatus::Clamped
			&& HasLimit(constrainedResult, VansProceduralLimitReason::Joint)
			&& constrainedResult.iterations > 1
			&& constrainedResult.requestedPositionError <= 0.01f))
		{
			std::cerr << "Procedural animation contract failed: constrained FABRIK status="
				<< static_cast<int>(constrainedResult.status)
				<< " error=" << constrainedResult.requestedPositionError
				<< " iterations=" << constrainedResult.iterations << '\n';
			return false;
		}
		for (const char* chainId : { "ccd", "fabrik" })
		{
			VansPoseWorkspace workspace;
			workspace.Initialize(skeleton, pose);
			const VansCompiledRigChain& chain = rig.chains[static_cast<std::size_t>(rig.FindChain(chainId))];
			VansProceduralGoal unreachableGoal = goal;
			unreachableGoal.positionModel = glm::vec3(100.0f, 0.0f, 0.0f);
			const VansProceduralSolverResult chainResult = VansChainIKSolver::Solve(
				workspace, rig, chain, unreachableGoal);
			if (!Check(chainResult.status == VansProceduralSolverStatus::Unreachable
				&& HasLimit(chainResult, VansProceduralLimitReason::Reach)
				&& workspace.IsFinite(),
				"CCD/FABRIK did not identify a target outside maximum reach")) return false;
		}
		Skeleton unequalSkeleton = skeleton;
		unequalSkeleton.bones[static_cast<std::size_t>(b)].localTransform =
			glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));
		unequalSkeleton.bones[static_cast<std::size_t>(c)].localTransform =
			glm::translate(glm::mat4(1.0f), glm::vec3(0.25f, 0.0f, 0.0f));
		unequalSkeleton.bones.back().localTransform =
			glm::translate(glm::mat4(1.0f), glm::vec3(0.25f, 0.0f, 0.0f));
		VansCompiledAnimationRig unequalRig;
		if (!VansAnimationRigCompiler::Compile(asset, unequalSkeleton, unequalRig, error))
			return Check(false, error.c_str());
		const std::vector<VansBoneTransform> unequalPose = BuildLocalPose(unequalSkeleton);
		for (const char* chainId : { "ccd", "fabrik" })
		{
			VansPoseWorkspace workspace;
			workspace.Initialize(unequalSkeleton, unequalPose);
			const VansCompiledRigChain& chain = unequalRig.chains[static_cast<std::size_t>(
				unequalRig.FindChain(chainId))];
			VansProceduralGoal innerGoal;
			innerGoal.valid = true;
			innerGoal.positionModel = workspace.GetComponentPosition(chain.boneIndices.front())
				+ glm::vec3(0.10f, 0.0f, 0.0f);
			const VansProceduralSolverResult chainResult = VansChainIKSolver::Solve(
				workspace, unequalRig, chain, innerGoal);
			if (!Check(chainResult.status == VansProceduralSolverStatus::Unreachable
				&& HasLimit(chainResult, VansProceduralLimitReason::Reach)
				&& workspace.IsFinite(),
				"CCD/FABRIK did not identify a target inside minimum reach")) return false;
		}
		for (const char* chainId : { "ccd", "fabrik" })
		{
			VansPoseWorkspace workspace;
			workspace.Initialize(skeleton, pose);
			const VansCompiledRigChain& chain = rig.chains[static_cast<std::size_t>(rig.FindChain(chainId))];
			VansProceduralGoal partialGoal = goal;
			partialGoal.positionWeight = 0.35f;
			const VansProceduralSolverResult chainResult = VansChainIKSolver::Solve(
				workspace, rig, chain, partialGoal);
			if (!Check(chainResult.status == VansProceduralSolverStatus::Solved
				&& chainResult.effectivePositionError > 0.01f,
				"CCD/FABRIK misclassified an authored partial weight as a solver clamp"))
				return false;
		}
		for (const char* chainId : { "ccd", "fabrik" })
		{
			VansPoseWorkspace workspace;
			workspace.Initialize(skeleton, pose);
			VansCompiledRigChain disabledChain = rig.chains[static_cast<std::size_t>(
				rig.FindChain(chainId))];
			std::fill(disabledChain.solveWeights.begin(), disabledChain.solveWeights.end(), 0.0f);
			const glm::vec3 originalTip = workspace.GetComponentPosition(disabledChain.boneIndices.back());
			const VansProceduralSolverResult disabledResult = VansChainIKSolver::Solve(
				workspace, rig, disabledChain, goal);
			if (!Check(disabledResult.status == VansProceduralSolverStatus::Clamped
				&& glm::distance(originalTip,
					workspace.GetComponentPosition(disabledChain.boneIndices.back())) <= 1.0e-6f,
				"CCD/FABRIK solveWeight zero did not preserve the input animation")) return false;
		}

		Skeleton aimSkeleton;
		const int aimRoot = AddBone(aimSkeleton, "root", -1, glm::vec3(0.0f));
		const int spine = AddBone(aimSkeleton, "spine", aimRoot, glm::vec3(0.0f));
		AddBone(aimSkeleton, "head", spine, glm::vec3(0.0f, 0.0f, 1.0f));
		aimSkeleton.BuildTopologicalOrder();
		VansAnimationRigAsset aimAsset;
		aimAsset.name = "Aim Fixture";
		aimAsset.skeletonGuid = "00000000-0000-4000-8000-000000000003";
		aimSkeleton.sourceSkeletonGuid = aimAsset.skeletonGuid;
		aimAsset.modelForward = glm::vec3(0.0f, 0.0f, 1.0f);
		aimAsset.modelUp = glm::vec3(0.0f, 1.0f, 0.0f);
		aimAsset.goals.push_back({ "look", "head" });
		VansRigChainDefinition aimChain;
		aimChain.id = "look";
		aimChain.solver = VansRigSolverKind::Aim;
		aimChain.bones = { "spine", "head" };
		aimChain.goal = "look";
		aimChain.weights = { 0.4f, 0.6f };
		aimChain.forwardAxisLocal = glm::vec3(0.0f, 0.0f, 1.0f);
		aimChain.upAxisLocal = glm::vec3(1.0f, 0.0f, 0.0f);
		aimAsset.chains.push_back(aimChain);
		VansCompiledAnimationRig aimRig;
		if (!VansAnimationRigCompiler::Compile(aimAsset, aimSkeleton, aimRig, error))
			return Check(false, error.c_str());
		VansPoseWorkspace aimWorkspace;
		aimWorkspace.Initialize(aimSkeleton, BuildLocalPose(aimSkeleton));
		VansAimConstraintTarget aimGoal;
		VansAimConstraintState aimState;
		aimGoal.valid = true;
		aimGoal.valueModel = glm::vec3(10.0f, 0.0f, 1.0f);
		VansAimConstraintSettings aimSettings;
		aimSettings.yawLimitDegrees = glm::vec2(-30.0f, 30.0f);
		aimSettings.pitchLimitDegrees = glm::vec2(-20.0f, 20.0f);
		aimSettings.maxAngularSpeedDegrees = 1000.0f;
		const VansProceduralSolverResult aimResult = VansAimConstraintSolver::Solve(
			aimWorkspace, aimRig, aimRig.chains.front(), aimGoal, 1.0f, aimState, aimSettings);
		if (!Check(aimResult.status == VansProceduralSolverStatus::Clamped
			&& HasLimit(aimResult, VansProceduralLimitReason::Joint)
			&& aimWorkspace.IsFinite(), "Aim constraint did not clamp a target outside yaw limits"))
			return false;

		aimWorkspace.Initialize(aimSkeleton, BuildLocalPose(aimSkeleton));
		aimSettings.yawLimitDegrees = glm::vec2(-180.0f, 180.0f);
		aimState = {};
		aimSettings.pitchLimitDegrees = glm::vec2(-89.0f, 89.0f);
		aimSettings.maxAngularSpeedDegrees = 1000.0f;
		const VansProceduralSolverResult frameResult = VansAimConstraintSolver::Solve(
			aimWorkspace, aimRig, aimRig.chains.front(), aimGoal, 1.0f, aimState, aimSettings);
		const glm::quat frameRotation = aimWorkspace.GetComponentRotation(
			aimRig.chains.front().boneIndices.back());
		const glm::vec3 finalAimOrigin = aimWorkspace.GetComponentPosition(
			aimRig.chains.front().boneIndices.back());
		const glm::vec3 finalAimDirection = glm::normalize(aimGoal.valueModel - finalAimOrigin);
		if (!Check(frameResult.status == VansProceduralSolverStatus::Solved
			&& glm::dot(glm::normalize(frameRotation * aimRig.chains.front().forwardAxisLocal),
				finalAimDirection) > 0.999f
			&& glm::dot(glm::normalize(frameRotation * aimRig.chains.front().upAxisLocal),
				glm::vec3(0.0f, 1.0f, 0.0f)) > 0.999f,
			"Aim constraint did not recompute from the moved tip or introduced roll")) return false;

		aimWorkspace.Initialize(aimSkeleton, BuildLocalPose(aimSkeleton));
		aimSettings.maxAngularSpeedDegrees = 30.0f;
		aimState = {};
		const VansProceduralSolverResult speedResult = VansAimConstraintSolver::Solve(
			aimWorkspace, aimRig, aimRig.chains.front(), aimGoal, 1.0f / 60.0f, aimState, aimSettings);
		const float appliedAngle = VansQuaternionAngleDegrees(
			aimWorkspace.GetComponentRotation(aimRig.chains.front().boneIndices.back()));
		if (!Check(speedResult.status == VansProceduralSolverStatus::Clamped
			&& HasLimit(speedResult, VansProceduralLimitReason::AngularSpeed)
			&& appliedAngle <= 0.501f,
			"Aim constraint exceeded its chain-wide angular-speed limit")) return false;
		// 每帧重新输入原动画，连续限速仍须到达完整目标，不能卡在 speed * dt。
		const auto animatedPose = BuildLocalPose(aimSkeleton);
		// 独立几何验收：枪口视差而非骨轴 look-at；旋转完整子树，保留腕部局部姿态。
		VansCompiledRigChain rayChain = aimRig.chains.front();
		rayChain.boneIndices = {spine}; rayChain.weights = {1};
		VansAimConstraintSettings raySettings;
		raySettings.mode = VansAimConstraintMode::WeaponRay;
		raySettings.muzzleLocal = {.25f,.08f,.45f}; raySettings.barrelLocal = {.2f,.1f,1.f};
		const int geometry = aimSkeleton.FindBoneIndex("head");
		for (int yaw : {-70,-35,0,35,70}) for (int pitch : {-45,0,45})
		{
			aimWorkspace.Initialize(aimSkeleton, BuildLocalPose(aimSkeleton));
			const auto originalHand = aimWorkspace.GetLocal(geometry);
			const auto pivot = aimWorkspace.GetComponentPosition(spine);
			const auto offset = aimWorkspace.GetComponentPosition(geometry) + raySettings.muzzleLocal - pivot;
			const auto forward = glm::normalize(raySettings.barrelLocal);
			const auto toTarget = glm::vec3(100*std::sin(glm::radians(float(yaw))), 100*std::sin(glm::radians(float(pitch))), 100);
			const float along = glm::dot(offset,forward);
			const float distance = -along+std::sqrt(along*along+glm::dot(toTarget,toTarget)-glm::dot(offset,offset));
			const auto from = glm::normalize(offset+distance*forward); const auto to = glm::normalize(toTarget);
			const auto cross = glm::cross(from,to);
			const auto expected = glm::normalize(glm::quat(1+glm::dot(from,to),cross.x,cross.y,cross.z));
			VansAimConstraintTarget rayGoal{pivot+toTarget,1,true}; VansAimConstraintState rayState;
			const auto solved = VansAimConstraintSolver::Solve(aimWorkspace,aimRig,rayChain,rayGoal,1.f/120,rayState,raySettings,-1,geometry);
			const auto actual = aimWorkspace.GetComponentRotation(spine);
			const auto muzzle = aimWorkspace.GetComponentPosition(geometry)+aimWorkspace.GetComponentRotation(geometry)*raySettings.muzzleLocal;
			const auto barrel = glm::normalize(aimWorkspace.GetComponentRotation(geometry)*raySettings.barrelLocal);
			if (!Check(solved.status == VansProceduralSolverStatus::Solved && std::abs(glm::dot(actual,expected)) > .999999f
				&& glm::dot(barrel,glm::normalize(rayGoal.valueModel-muzzle)) > .999999f
				&& glm::distance(originalHand.translation,aimWorkspace.GetLocal(geometry).translation) < 1.e-6f
				&& std::abs(glm::dot(originalHand.rotation,aimWorkspace.GetLocal(geometry).rotation)) > .999999f,
				"WeaponRay did not match geometric parallax or changed wrist articulation")) return false;
		}
		for (auto mode : {VansAimConstraintMode::PitchOffset, VansAimConstraintMode::LookAtDirection})
		for (int fps : {30, 60, 120})
		for (float requested : {-80.0f, -30.0f, 0.0f, 30.0f, 80.0f})
		{
			aimState = {};
			aimSettings.mode = mode;
			aimSettings.pitchLimitDegrees = {-45,45};
			aimSettings.maxAngularSpeedDegrees = 180.0f;
			aimGoal.valueModel = {0, std::sin(glm::radians(requested)), std::cos(glm::radians(requested))};
			for (int frame = 0; frame < fps; ++frame)
			{
				aimWorkspace.Initialize(aimSkeleton, animatedPose);
				const auto solved = VansAimConstraintSolver::Solve(aimWorkspace, aimRig, aimRig.chains.front(), aimGoal,
					1.0f/fps, aimState, aimSettings);
				if (!Check(solved.status != VansProceduralSolverStatus::InvalidInput, "Continuous aim solve failed")) return false;
			}
			const auto finalForward = aimWorkspace.GetComponentRotation(aimRig.chains.front().boneIndices.back()) * aimRig.chains.front().forwardAxisLocal;
			const float actualPitch = glm::degrees(std::asin(std::clamp(finalForward.y, -1.0f, 1.0f)));
			if (!Check(std::abs(actualPitch-std::clamp(requested,-45.0f,45.0f)) < 0.06f,
				"Continuous aim did not reach its pitch target independently of frame rate")) return false;
			aimGoal.weight = 0;
			aimWorkspace.Initialize(aimSkeleton, animatedPose);
			const auto disabled = VansAimConstraintSolver::Solve(aimWorkspace, aimRig, aimRig.chains.front(), aimGoal, 1.0f/fps, aimState, aimSettings);
			if (!Check(disabled.status == VansProceduralSolverStatus::NoEffect && !aimState.valid,
				"Disabled aim retained its correction state")) return false;
			aimGoal.weight = 1;
		}
		return true;
	}

	bool TestConfigurableRetargetReferences()
	{
		const auto make = [](const char* prefix, float scale) {
			Skeleton value;
			const std::vector<std::string> labels={"Origin","Center","Crown","TipA","TipB","BaseA","BaseB"};
			const std::vector<int> parents={-1,0,1,1,1,0,0};
			const std::vector<glm::vec3> positions={{0,0,0},{0,1,0},{0,1,0},{-1,.5f,0},{1,.5f,0},{-.2f,1,0},{.2f,1,0}};
			for(std::size_t i=0;i<labels.size();++i)
			{
				const auto name=std::string(prefix)+labels[i];AddBone(value,name.c_str(),parents[i],positions[i]*scale);
				value.bones[i].guid="id-"+name;value.bones[i].canonicalPath="fixture/"+name;
			}
			value.sourceSkeletonGuid=std::string(prefix)=="Input"?"f2eea6ee-9f63-49c1-93d1-5372af9f79f1":"204a8d8b-d589-49c7-aef4-dbf31e30622b";
			value.BuildTopologicalOrder();value.RebuildIdentityMapsAndSignature();return value;
		};
		auto source=make("Input",1),target=make("Output",2);
		VansCompiledAnimationRig rig;rig.skeleton=&target;
		VansRetargetRuntimeDesc desc;desc.targetModelSpaceAlignment=VansRetargetModelSpaceAlignment::TargetBindPose;
		desc.rootAlignment=VansRetargetRootAlignment::FeetToOwner;
		for(std::size_t i=0;i<source.bones.size();++i)desc.boneMappings.push_back({source.bones[i].name,target.bones[i].name,"",1,i<2});
		VansRetargetProcessor processor;
		if(!Check(!processor.Build(source,target,rig,desc),"Custom reference skeleton silently used missing canonical roles"))return false;
		VansAnimationRigAsset rigAsset;rigAsset.name="Reference identity fixture";rigAsset.skeletonGuid=target.sourceSkeletonGuid;
		rigAsset.semanticBones={{"pelvis",target.bones[1].guid},{"head",target.bones[2].canonicalPath},
			{"leftHand",target.bones[3].name},{"rightHand",target.bones[4].name},
			{"leftFoot",target.bones[5].canonicalPath},{"rightFoot",target.bones[6].guid}};
		std::string error;
		if(!VansAnimationRigCompiler::Compile(rigAsset,target,rig,error))return Check(false,error.c_str());
		if(!Check(processor.Build(source,target,rig,desc)&&std::abs(processor.GetStats().translationScale-2)<1.e-5f,
			"Authored reference identities did not drive target/source basis and AutoPelvis scale"))return false;
		std::vector<glm::mat4> local,model,result;
		for(const auto& bone:source.bones)local.push_back(bone.localTransform);
		for(float travel:{0.f,.25f,.5f})
		{
			local[0]=glm::translate(glm::mat4(1),glm::vec3(0,travel,0));
			if(!VansPoseMath::BuildModelTransforms(local,source,model)||!processor.Process(model,source,target,result))return false;
			if(!Check(std::abs(result[5][3].y-travel*2)<1.e-4f&&std::abs(result[6][3].y-travel*2)<1.e-4f,
				"Configured feet calibration followed current pose or lost source root travel"))return false;
		}
		nlohmann::json encoded;VansAnimationRigAsset decoded;
		if(!VansAnimationRigStorage::SerializeToJsonObject(rigAsset,encoded,error)
			||!VansAnimationRigStorage::DeserializeFromJsonObject(encoded,decoded,error))return Check(false,error.c_str());
		nlohmann::json encodedAgain;
		if(!VansAnimationRigStorage::SerializeToJsonObject(decoded,encodedAgain,error)||!Check(encodedAgain==encoded,"Rig semantic identities changed on typed JSON roundtrip"))return false;
		auto invalid=encoded;invalid["semanticBones"]["head"]=42;
		if(!Check(!VansAnimationRigStorage::DeserializeFromJsonObject(invalid,decoded,error),"Rig semantic binding accepted a nonstring identity"))return false;
		auto missing=rigAsset;missing.semanticBones["head"]="missing";VansCompiledAnimationRig invalidRig;
		if(!Check(!VansAnimationRigCompiler::Compile(missing,target,invalidRig,error),"Rig semantic binding accepted a missing bone"))return false;
		missing=rigAsset;missing.semanticBones.erase("head");
		if(!VansAnimationRigCompiler::Compile(missing,target,invalidRig,error)
			||!Check(!processor.Build(source,target,invalidRig,desc),"Retarget invented an undeclared custom head reference"))return false;
		std::cout<<"RETARGET_RIG_SEMANTICS_PASS GUID/path/name, source/target basis, AutoPelvis, bind-only feet, existing Rig roundtrip\n";
		return true;
	}

	bool TestExplicitRetargetMappingAndBake()
	{
		const auto make = [](bool target) {
			Skeleton skeleton;
			const std::vector<std::string> names = target
				? std::vector<std::string>{"root","pelvis","spine_a","spine_b","head","hand_l","hand_r","foot_l","foot_r"}
				: std::vector<std::string>{"S_Root","S_Hip","S_Spine","S_Head","S_LeftHand","S_RightHand","S_LeftFoot","S_RightFoot"};
			const std::vector<int> parents = target ? std::vector<int>{-1,0,1,2,3,0,0,0,0}
				: std::vector<int>{-1,0,1,2,0,0,0,0};
			const float scale = target ? 100.0f : 1.0f;
			for (std::size_t index=0; index<names.size(); ++index)
			{
				BoneInfo bone; bone.id=static_cast<int>(index); bone.name=names[index];
				bone.guid="id-"+bone.name; bone.parentIndex=parents[index];
				bone.canonicalPath=parents[index]<0 ? bone.name : skeleton.bones[parents[index]].canonicalPath+"/"+bone.name;
				glm::vec3 translation(0.0f);
				if(index==1)translation={0,scale,0};
				else if (index>1 && index<(target?5:4)) translation={0,.25f*scale,0};
				else if(index==(target?5:4))translation={-.5f*scale,scale,0};
				else if(index==(target?6:5))translation={.5f*scale,scale,0};
				else if(index==(target?7:6))translation={-.15f*scale,0,0};
				else if(index==(target?8:7))translation={.15f*scale,0,0};
				bone.localTransform=glm::translate(glm::mat4(1),translation);
				if(target && index==0)bone.localTransform*=glm::toMat4(glm::angleAxis(glm::radians(90.0f),glm::vec3(0,1,0)));
				if(target && index==2)bone.localTransform*=glm::toMat4(glm::angleAxis(glm::radians(23.0f),glm::vec3(0,1,0)));
				skeleton.bones.push_back(bone);
				if(parents[index]>=0)skeleton.bones[parents[index]].children.push_back(static_cast<int>(index));
			}
			skeleton.BuildTopologicalOrder(); skeleton.RebuildIdentityMapsAndSignature(); return skeleton;
		};
		auto source=make(false), target=make(true);
		VansCompiledAnimationRig rig; rig.skeleton=&target;
		VansRetargetRuntimeDesc desc; desc.translationScaleMode=VansRetargetTranslationScaleMode::Explicit;
		desc.translationScale=100; desc.targetModelSpaceAlignment=VansRetargetModelSpaceAlignment::TargetBindPose;
		desc.boneMappings={{"S_Root","root","",1,true},{"S_Hip","pelvis","",1,true},
			{"S_Spine","spine_a","S_Hip",.5f,false},{"S_Spine","spine_b","",1,false},
			{"S_Head","head","",1,false},{"S_LeftHand","hand_l","",1,false},
			{"S_RightHand","hand_r","",1,false},{"S_LeftFoot","foot_l","",1,false},{"S_RightFoot","foot_r","",1,false}};
		VansRetargetProcessor processor;
		if(!Check(processor.Build(source,target,rig,desc),"Explicit differing-axis retarget failed to build"))return false;
		std::vector<glm::mat4> sourceLocal,targetLocal,sourceModel,targetBind,result;
		for(const auto& b:source.bones)sourceLocal.push_back(b.localTransform);
		for(const auto& b:target.bones)targetLocal.push_back(b.localTransform);
		VansPoseMath::BuildModelTransforms(sourceLocal,source,sourceModel);
		VansPoseMath::BuildModelTransforms(targetLocal,target,targetBind);
		if(!Check(processor.Process(sourceModel,source,target,result),"Explicit bind retarget failed"))return false;
		for(std::size_t bone=0;bone<targetBind.size();++bone)for(int column=0;column<4;++column)
			if(!Check(glm::length(result[bone][column]-targetBind[bone][column])<.0001f,
				"TargetBindPose did not preserve target binding axes and origin"))return false;
		sourceLocal[2]*=glm::toMat4(glm::angleAxis(glm::radians(90.0f),glm::vec3(0,1,0)));
		sourceLocal[1][3].y+=.1f;
		VansPoseMath::BuildModelTransforms(sourceLocal,source,sourceModel);
		if(!processor.Process(sourceModel,source,target,result))return false;
		glm::vec3 t,s;glm::quat actual,bind;
		VansBoneTransform a,b;
		VansPoseMath::TryDecompose(result[2],a);VansPoseMath::TryDecompose(targetBind[2],b);
		if(!Check(std::abs(VansQuaternionAngleDegrees(a.rotation*glm::inverse(b.rotation))-45)<.002f
			&& std::abs(result[1][3].y-targetBind[1][3].y-10)<.001f,
			"Explicit spine interpolation or metre-to-centimetre translation changed"))return false;
		for(auto invalid : {0,1,2})
		{
			auto bad=desc;
			if(invalid==0)bad.boneMappings[1].sourceBone="missing";
			if(invalid==1)bad.boneMappings[2].rotationReferenceBone="S_LeftHand";
			if(invalid==2)bad.boneMappings.push_back(bad.boneMappings[1]);
			VansRetargetProcessor rejected;
			if(!Check(!rejected.Build(source,target,rig,bad),"Invalid explicit retarget map was accepted"))return false;
		}
		VansAnimationClip clip;clip.clipName="RetargetAudit";clip.duration=1;clip.rootMotion.boneName="S_Root";
		clip.boneKeyframes.resize(source.bones.size());
		for(std::size_t bone=0;bone<source.bones.size();++bone)
		{
			VansBoneTransform first,last;
			VansPoseMath::TryDecompose(source.bones[bone].localTransform,first);
			VansPoseMath::TryDecompose(sourceLocal[bone],last);
			clip.boneKeyframes[bone]={{0,first.translation,first.rotation,first.scale},{1,last.translation,last.rotation,last.scale}};
		}
		clip.events.push_back({42,.75f,"Audit.End",{}});
		VansAnimationClip baked;std::string error;
		if(!Check(VansAnimationRetargetBaker::Bake(clip,source,target,rig,desc,baked,error),error.c_str()))return false;
		if(!Check(baked.rootMotion.boneName=="root" && baked.events.size()==1 && baked.events[0].time==.75f,
			"Retarget bake lost the motion root or event timestamp"))return false;
		VansAnimationSampleRequest request;request.currentTime=1;request.loop=false;request.extractRootMotion=false;
		VansPosePayload sampled;VansAnimationFrameVector<glm::mat4> bakedModel;
		if(!VansAnimationSampler::Sample(baked,target,request,sampled)
			|| !VansPoseMath::BuildModelTransforms(sampled.localPose,target,bakedModel))return false;
		for(std::size_t bone=0;bone<result.size();++bone)for(int column=0;column<4;++column)
			if(!Check(glm::length(bakedModel[bone][column]-result[bone][column])<.0001f,
				"Baked clip differs from the runtime Retarget processor"))return false;
		VansAnimationClip native=clip;
		for(auto& channel:native.boneKeyframes)for(auto& key:channel)
		{ key.position=glm::vec3(0);key.rotation=glm::quat(1,0,0,0);key.scale=glm::vec3(1); }
		for(auto& key:native.boneKeyframes[1])key.position={0,.2f,0};
		for(auto& key:native.boneKeyframes[2])key.rotation=glm::angleAxis(glm::radians(60.0f),glm::vec3(0,1,0));
		VansRetargetBakeSettings settings;settings.referenceClip=&clip;
		settings.nativeDeltaChannels.assign(source.bones.size(),0);
		settings.nativeDeltaWeights.assign(source.bones.size(),1);
		settings.nativeDeltaChannels[1]=1;settings.nativeDeltaChannels[2]=2;
		if(!VansAnimationRetargetBaker::Bake(native,source,target,rig,desc,baked,error,settings)
			|| !VansAnimationSampler::Sample(baked,target,request,sampled)
			|| !VansPoseMath::BuildModelTransforms(sampled.localPose,target,bakedModel))return false;
		VansPoseMath::TryDecompose(bakedModel[2],a);VansPoseMath::TryDecompose(targetBind[2],b);
		if(!Check(!baked.rootMotion.enabled && std::abs(bakedModel[1][3].y-targetBind[1][3].y-20)<.001f
			&& std::abs(VansQuaternionAngleDegrees(a.rotation*glm::inverse(b.rotation))-30)<.002f,
			"Native delta bake interpreted relative Hip/Spine tracks as absolute poses"))return false;
		settings.nativeDeltaWeights[2]=2;
		if(!Check(!VansAnimationRetargetBaker::Bake(native,source,target,rig,desc,baked,error,settings),
			"Native delta bake accepted an invalid channel weight"))return false;
		auto projected=desc;
		projected.outputModelRotation=glm::angleAxis(glm::radians(90.0f),glm::vec3(1,0,0));
		VansRetargetProcessor projection;
		std::vector<glm::mat4> projectedPose;
		if(!projection.Build(source,target,rig,projected)||!projection.Process(sourceModel,source,target,projectedPose))return false;
		const auto expectedProjection=glm::toMat4(projected.outputModelRotation)*result[1];
		for(int column=0;column<4;++column)
			if(!Check(glm::length(projectedPose[1][column]-expectedProjection[column])<.0001f,
				"Output coordinate projection changed the anatomical retarget result"))return false;
		projected.outputModelRotation=glm::quat(2,0,0,0);
		if(!Check(!projection.Build(source,target,rig,projected)&&!projection.IsValid(),
			"Invalid output quaternion left the Retarget processor valid"))return false;
		VansRetargetProfileAsset profile;profile.name="Explicit audit";
		// External clocks must publish only their traversed interval, once.
		clip.boneKeyframes[0].back().position.x=1;
		clip.rootMotion.enabled=true;
		clip.rootMotion.forceLock=true;clip.rootMotion.lockMode=AnimationRootLockMode::FirstFrame;
		VansAnimationClip lockedBake;
		if(!Check(VansAnimationRetargetBaker::Bake(clip,source,target,rig,desc,lockedBake,error)
			&& glm::length(lockedBake.boneKeyframes[0].back().position-lockedBake.boneKeyframes[0].front().position)>99,
			"Offline bake discarded root tracks from a force-locked runtime clip"))return false;
		std::unordered_map<std::string,VansAnimationClip> clips{{"clock",clip}};
		std::unordered_map<std::string,AnimatorParameter> parameters;
		parameters["time"].type=AnimatorParamType::Float;
		VansAnimGraph graph;
		auto player=std::make_unique<AnimGraphClipNode>();
		player->m_ClipName="clock";player->m_Loop=false;player->m_RootMotion=true;
		player->m_SampleTimeParameter="time";player->m_SampleTimeOutputs=true;
		const int playerId=graph.AddNode(std::move(player));
		const int outputId=graph.AddNode(std::make_unique<AnimGraphOutputNode>());
		graph.AddLink(playerId,0,outputId,0);
		VansAnimGraphInstance instance(graph);
		AnimGraphContext context;context.skeleton=&source;context.clips=&clips;context.parameters=&parameters;context.deltaTime=.9f;
		parameters["time"].floatVal=0;instance.EvaluateFrame(context);
		parameters["time"].floatVal=.8f;auto clockPose=instance.EvaluateFrame(context);
		if(!Check(clockPose.valid && clockPose.events.size()==1 && clockPose.rootMotion.valid
			&& glm::length(clockPose.rootMotion.translation)>.7f,"External clock lost traversed root motion or event"))return false;
		auto paused=instance.EvaluateFrame(context);
		if(!Check(paused.events.empty() && glm::length(paused.rootMotion.translation)<.0001f,"External clock advanced while paused"))return false;
		instance.Reset();parameters["time"].floatVal=.8f;
		auto entered=instance.EvaluateFrame(context);
		if(!Check(entered.events.empty() && glm::length(entered.rootMotion.translation)<.0001f,
			"External clock replayed motion/events from zero when entering at a nonzero phase"))return false;
		auto* firstClock=static_cast<AnimGraphClipNode*>(graph.GetNode(playerId));firstClock->m_SampleEpochParameter="epoch";firstClock->m_SampleFromZeroOnEpoch=true;
		parameters["epoch"].type=AnimatorParamType::Int;parameters["epoch"].intVal=1;instance.Reset();
		const auto firstTraversal=instance.EvaluateFrame(context);
		if(!Check(firstTraversal.events.size()==1 && glm::length(firstTraversal.rootMotion.translation)>.7f,
			"First epoch sample skipped a completion event at low frame rate"))return false;
		if(!Check(instance.EvaluateFrame(context).events.empty(),"Paused epoch replayed completion events"))return false;
		VansAnimGraph tailGraph;
		auto tailPlayer=std::make_unique<AnimGraphClipNode>();tailPlayer->m_ClipName="clock";tailPlayer->m_Loop=false;
		tailPlayer->m_SampleTimeParameter="time";tailPlayer->m_SampleTimeOutputs=true;tailPlayer->m_SampleEpochParameter="epoch";
		const int tailClip=tailGraph.AddNode(std::move(tailPlayer));
		auto inertial=std::make_unique<AnimGraphInertializationNode>();inertial->m_TriggerParameter="tail";inertial->m_Duration=.5f;
		const int tail=tailGraph.AddNode(std::move(inertial));const int tailOut=tailGraph.AddNode(std::make_unique<AnimGraphOutputNode>());
		tailGraph.AddLink(tailClip,0,tail,0);tailGraph.AddLink(tail,0,tailOut,0);
		parameters["epoch"].type=AnimatorParamType::Int;parameters["tail"].type=AnimatorParamType::Trigger;
		VansAnimGraphInstance tailInstance(tailGraph);context.deltaTime=1.f/60;
		parameters["time"].floatVal=0;tailInstance.EvaluateFrame(context);tailInstance.EvaluateFrame(context);
		parameters["epoch"].intVal=1;parameters["time"].floatVal=.8f;parameters["tail"].boolVal=true;
		auto joined=tailInstance.EvaluateFrame(context);parameters["tail"].boolVal=false;
		if(!Check(joined.valid && std::abs(joined.localPose[1].translation.y-1)<.02f,
			"Triggered transition tail did not preserve outgoing pose continuity"))return false;
		for(int frame=0;frame<40;++frame){parameters["time"].floatVal=1;joined=tailInstance.EvaluateFrame(context);}
		if(!Check(std::abs(joined.localPose[1].translation.y-1.1f)<.0001f,
			"Transition tail froze the incoming motion instead of decaying over it"))return false;
		// Rotation-only upper masks must preserve local translations and scales.
		VansBoneMaskAsset maskAsset;maskAsset.id="channels";maskAsset.name="channels";maskAsset.defaultWeight=1;
		maskAsset.explicitChannelWeights["S_Hip"]={0,1,0};
		nlohmann::json maskJson;VansBoneMaskAsset maskLoaded;
		if(!VansBoneMaskStorage::SerializeToJsonObject(maskAsset,maskJson,error)
			|| !VansBoneMaskStorage::DeserializeFromJsonObject(maskJson,maskLoaded,error))return false;
		auto mask=VansBoneMaskCompiler::Compile(maskLoaded,source);
		VansAnimationFrameVector<VansBoneTransform> bindPose;
		VansAnimationLayerMixer::BuildBindPose(source,bindPose);
		VansPosePayload basePayload,layerPayload;basePayload.valid=layerPayload.valid=true;
		basePayload.localPose=layerPayload.localPose=bindPose;
		layerPayload.localPose[1].translation+=glm::vec3(4,5,6);layerPayload.localPose[1].scale*=2;
		layerPayload.localPose[1].rotation=glm::angleAxis(glm::radians(45.f),glm::vec3(0,1,0));
		for(auto space:{VansRotationBlendSpace::Local,VansRotationBlendSpace::Mesh})
		{
			VansAnimationLayerDefinition definition;definition.rotationSpace=space;
			auto mixed=VansAnimationLayerMixer::ApplyLayer(basePayload,layerPayload,definition,mask,source,bindPose,1);
			if(!Check(glm::length(mixed.localPose[1].translation-bindPose[1].translation)<.0001f
				&& glm::length(mixed.localPose[1].scale-bindPose[1].scale)<.0001f
				&& VansQuaternionAngleDegrees(mixed.localPose[1].rotation)>40,"Channel mask changed translation/scale or lost rotation"))return false;
		}
		profile.targetModelSpaceAlignment=desc.targetModelSpaceAlignment;profile.boneMappings=desc.boneMappings;
		nlohmann::json json;VansRetargetProfileAsset roundTrip;
		if(!VansRetargetProfileJsonCodec::Encode(profile,json,error)
			|| !VansRetargetProfileJsonCodec::Decode(json,roundTrip,error))return false;
		return Check(roundTrip.boneMappings.size()==9 && roundTrip.boneMappings[2].rotationBlend==.5f,
			"Explicit retarget profile roundtrip lost spine mapping fields");
	}

	bool TestStrictRetargetAndContactConfiguration()
	{
		VansRetargetProfileAsset profile;
		profile.name = "Strict Profile";
		nlohmann::json value;
		std::string error;
		if (!VansRetargetProfileJsonCodec::Encode(profile, value, error))
			return Check(false, error.c_str());
		value["retarget_options"] = nlohmann::json::object();
		VansRetargetProfileAsset parsed;
		if (!Check(!VansRetargetProfileJsonCodec::Decode(value, parsed, error),
			"Retarget Profile accepted a legacy compatibility field")) return false;
		profile.translationScaleMode = static_cast<VansRetargetTranslationScaleMode>(255);
		if (!Check(!VansRetargetProfileJsonCodec::Encode(profile, value, error),
			"Retarget Profile storage silently rewrote an invalid translation scale enum"))
			return false;
		profile.translationScaleMode = VansRetargetTranslationScaleMode::AutoPelvis;
		profile.rootAlignment = static_cast<VansRetargetRootAlignment>(255);
		if (!Check(!VansRetargetProfileJsonCodec::Encode(profile, value, error),
			"Retarget Profile storage silently rewrote an invalid root alignment enum"))
			return false;
		profile.rootAlignment = VansRetargetRootAlignment::None;
		profile.targetModelSpaceAlignment = static_cast<VansRetargetModelSpaceAlignment>(255);
		if (!Check(!VansRetargetProfileJsonCodec::Encode(profile, value, error),
			"Retarget Profile storage silently rewrote an invalid model alignment enum"))
			return false;

		Vans::VansSerializedValue minimalMotionMatching = Vans::VansSerializedValue::Object({});
		MotionMatchingSettings defaultSettings;
		MotionMatchingSettings decodedDefaults;
		if (!Check(Vans::VansMotionMatchingConfigCodec::Decode(
			minimalMotionMatching, decodedDefaults, error), error.c_str()) ||
			!Check(decodedDefaults.sampleRate == defaultSettings.sampleRate
				&& decodedDefaults.desiredSpeedScale == defaultSettings.desiredSpeedScale
				&& decodedDefaults.trajectoryWeight == defaultSettings.trajectoryWeight
				&& decodedDefaults.states.airborneState == defaultSettings.states.airborneState,
				"Motion Matching decoder did not inherit owned defaults")) return false;
		Vans::SetSerializedObjectField(
			minimalMotionMatching, "search_groups", Vans::VansSerializedValue::Array({}));
		if (!Check(!Vans::VansMotionMatchingConfigCodec::Decode(
			minimalMotionMatching, decodedDefaults, error),
			"Motion Matching accepted the removed search_groups compatibility schema")) return false;

		MotionMatchingSettings settings;
		settings.contactProvider = "locomotion";
		settings.contactChannels = {
			{ "leftFoot", MotionMatchingContactSource::LeftFoot },
			{ "rightFoot", MotionMatchingContactSource::LeftFoot }
		};
		VansAnimationController controller;
		return Check(!controller.ConfigureMotionMatching(settings, error),
			"Motion Matching accepted duplicate contact sources");
	}

	bool ValidateProjectScene(
		const std::filesystem::path& scenePath,
		const std::unordered_map<std::string, std::string>& expectedRigs)
	{
		Vans::VansAssetDocument document;
		std::string error;
		if (!document.Load(scenePath, error)) return Check(false, error.c_str());
		const Vans::VansSerializedValue root = document.SerializedRootSnapshot();
		const Vans::VansSerializedValue* entities = Vans::FindObjectField(root, "entities");
		if (!Check(entities && entities->kind == Vans::VansSerializedValue::Kind::Array,
			"Project scene has no entities array")) return false;
		std::unordered_set<std::string> found;
		for (const Vans::VansSerializedValue& entity : entities->arrayItems)
		{
			const std::optional<Vans::VansSceneAnimationComponentConfig> config =
				Vans::VansSceneAnimationComponentReader::ReadFromAuthoringEntity(entity);
			if (!config) continue;
			const auto expected = expectedRigs.find(config->name);
			if (expected == expectedRigs.end()) continue;
			if (!Check(config->valid && config->rigGuid == expected->second,
				"Project scene animation did not resolve its required Animation Rig")) return false;
			if (!Check(found.insert(config->name).second,
				"Project scene contains a duplicate configured character")) return false;
			if (config->rootMotion && !Check(!config->rootBone.empty(),
				"Root-motion Animation component has no configured root bone")) return false;
			if (config->motionMatching)
			{
				VansAnimationController controller;
				if (!Check(controller.ConfigureMotionMatching(*config->motionMatching, error),
					error.c_str())) return false;
				Vans::VansSerializedValue encoded;
				if (!Check(Vans::VansMotionMatchingConfigCodec::Encode(
					*config->motionMatching, encoded, error), error.c_str())) return false;
				MotionMatchingSettings decoded;
				if (!Check(Vans::VansMotionMatchingConfigCodec::Decode(
					encoded, decoded, error), error.c_str())) return false;
				Vans::VansSerializedValue encodedAgain;
				if (!Check(Vans::VansMotionMatchingConfigCodec::Encode(
					decoded, encodedAgain, error), error.c_str()) ||
					!Check(Vans::SerializedValuesEqual(encoded, encodedAgain),
						"Motion Matching current-schema encode/decode is not stable")) return false;
				if (config->motionMatching->enabled && config->name != "Belica")
				{
					if (!Check(config->motionMatching->contactProvider == "locomotion"
						&& config->motionMatching->contactChannels.size() == 2,
						"Motion Matching character does not publish both canonical foot contacts"))
						return false;
					if (!Check(config->motionMatching->states.airborneState == 5,
						"Motion Matching airborne state was not loaded from project configuration"))
						return false;
					if (!Check(config->rootBone == config->motionMatching->rig.root,
						"Motion Matching root bone is not owned by the Animation component configuration"))
						return false;
				}
			}
		}
		return Check(found.size() == expectedRigs.size(),
			"Project scene is missing a character required by the procedural-animation configuration");
	}



	bool TestTransformTargetHierarchy()
	{
		struct Leases
		{
			std::vector<std::uint32_t> ids;
			std::uint32_t Add(){const auto id=Vans::VansTransformStore::Allocate();ids.push_back(id);auto t=Vans::VansTransformStore::Read(id);t.m_Position=glm::vec3(0);t.m_Rotation=glm::vec3(0);t.m_Scale=glm::vec3(1);Vans::VansTransformStore::Write(id,t);return id;}
			~Leases(){for(auto id:ids)Vans::VansTransformStore::Release(id);}
		} leases;
		struct Provider : Vans::IVansTransformAnchorProvider
		{
			bool ResolveModelSpaceTransform(const Vans::VansTransformAnchorHandle&,glm::mat4& m,std::uint64_t& r)const override
			{m=glm::mat4(1);r=1;return true;}
		} provider;
		const auto owner=leases.Add(),gun=leases.Add(),grip=leases.Add();
		Vans::VansTransformGraph graph(&provider);
		Vans::VansLocalTransform rootLocal,gunLocal,gripLocal;
		rootLocal.position={5,4,3};rootLocal.rotation=glm::angleAxis(glm::radians(90.0f),glm::vec3(0,1,0));rootLocal.scale=glm::vec3(0.01f);
		gunLocal.position={0,5,0};gripLocal.position={1,2,3};
		if (!Check(graph.SetWorldTransform(owner,rootLocal.ToMatrix()) &&
			graph.SetParent(gun,owner,Vans::VansTransformReparentMode::Snap) &&
			graph.SetLocalTransform(gun,gunLocal) &&
			graph.SetParent(grip,gun,Vans::VansTransformReparentMode::Snap) &&
			graph.SetLocalTransform(grip,gripLocal), "Target hierarchy fixture setup failed")) return false;
		VansResolvedAnimationTarget target;
		if(!Check(Vans::VansAnimationTargetResolver::Resolve(graph,grip,{},target),target.diagnostic.c_str()))return false;
		// 根 Transform 的实际世界矩阵是权威值，90 度附近的 Euler 往返有浮点量化。
		auto expected=glm::vec3(Vans::VansTransformStore::Read(owner).GetModelMatrix()*
			gunLocal.ToMatrix()*gripLocal.ToMatrix()*glm::vec4(0,0,0,1));
		if(!Check(glm::length(target.positionWorld-expected)<1e-5f,"Target hierarchy world transform or owner scale incorrect"))
		{
			const auto actual = target.positionWorld;
			const auto rotation = Vans::VansTransformStore::Read(owner).m_Rotation;
			std::cerr << "actual=" << actual.x << "," << actual.y << "," << actual.z
				<< " expected=" << expected.x << "," << expected.y << "," << expected.z
				<< " ownerEuler=" << rotation.x << "," << rotation.y << "," << rotation.z << std::endl;
			return false;
		}
		Vans::VansTransformAnchorHandle handle{1,1,Vans::VansTransformAnchorKind::Socket,"socket","beforeIK"};
		if(!Check(graph.SetAnchorWithLocalTransform(gun,owner,handle,gunLocal),"Target fixture anchor setup failed"))return false;
		gripLocal.position={3,2,1};graph.SetLocalTransform(grip,gripLocal);
		const glm::mat4 socket=glm::translate(glm::mat4(1),glm::vec3(2,0,0));
		const auto own=[&](const Vans::VansTransformGraphLink&,Vans::VansAnimationTargetAnchor& anchor)
		{anchor.sameSkeleton=true;anchor.boneIndex=4;anchor.boneToAnchor=socket;return true;};
		if(!Check(Vans::VansAnimationTargetResolver::Resolve(graph,grip,own,target) && target.space==VansAnimationTargetSpace::Pose &&
			target.poseCheckpoint=="beforeIK" && target.sourceBoneIndex==4,"Own-skeleton target did not retain checkpoint recipe"))return false;
		expected=glm::vec3(socket*gunLocal.ToMatrix()*gripLocal.ToMatrix()*glm::vec4(0,0,0,1));
		if(!Check(glm::length(glm::vec3(target.sourceLocal[3])-expected)<1e-5f,"Own target local chain reused stale world pose"))return false;
		if(!Check(!Vans::VansAnimationTargetResolver::Resolve(graph,grip,{},target) && !target.valid,"Missing anchor retained last-frame target"))return false;
		gripLocal.scale=glm::vec3(-1,1,1);graph.SetLocalTransform(grip,gripLocal);
		if(!Check(!Vans::VansAnimationTargetResolver::Resolve(graph,grip,own,target),"Negative target scale accepted"))return false;
		return true;
	}

	bool TestChainTargetOrientation()
	{
		LegFixture fixture;
		if(!BuildLegFixture(fixture,false))return false;
		fixture.asset.contacts.clear();
		for(auto solver:{VansRigSolverKind::CCD,VansRigSolverKind::FABRIK})
		{
			fixture.asset.chains[0].solver=solver;
			fixture.asset.chains[0].solveWeights={1,1};
			std::string error;
			if(!Check(VansAnimationRigCompiler::Compile(fixture.asset,fixture.skeleton,fixture.rig,error),error.c_str()))return false;
			VansPoseWorkspace workspace;
			workspace.Initialize(fixture.skeleton,fixture.localPose);
			const int tip=fixture.rig.chains[0].boneIndices.back();
			const auto position=workspace.GetComponentPosition(tip);
			VansProceduralGoal goal;
			goal.valid=true;goal.positionWeight=0;goal.rotationWeight=1;
			goal.rotationModel=glm::angleAxis(glm::radians(37.0f),glm::vec3(0,1,0));
			const auto result=VansChainIKSolver::Solve(workspace,fixture.rig,fixture.rig.chains[0],goal,{});
			if(!Check(result.status==VansProceduralSolverStatus::Solved &&
				std::abs(glm::dot(workspace.GetComponentRotation(tip),goal.rotationModel))>0.99999f &&
				glm::length(position-workspace.GetComponentPosition(tip))<1e-6f,"Rotation-only chain IK changed position or missed orientation"))return false;
		}
		return true;
	}

	bool TestTransformTargetCheckpoint()
	{
		LegFixture fixture;
		if (!BuildLegFixture(fixture, false)) return false;
		VansAnimGraph graph;
		const int inputId = graph.AddNode(std::make_unique<AnimGraphTargetPoseInputNode>());
		auto goalNode = std::make_unique<AnimGraphGoalNode>();
		goalNode->m_Goal.goalId = "leftFoot";
		goalNode->m_Goal.binding = "grip";
		goalNode->m_Goal.weightParameter = "gripWeight";
		goalNode->m_Goal.fixedRotationWeight = 1;
		goalNode->m_Goal.poleBoneName = "calf_l";
		goalNode->m_Goal.poleOffsetLocal = glm::vec3(-0.3f, 0, 0.2f);
		const int goalId = graph.AddNode(std::move(goalNode));
		auto checkpoint = std::make_unique<AnimGraphPoseCheckpointNode>();
		checkpoint->m_CheckpointId = "beforeIK";
		checkpoint->m_Bones = { "foot_l" };
		const int checkpointId = graph.AddNode(std::move(checkpoint));
		auto limb = std::make_unique<AnimGraphLimbIKNode>();
		limb->m_ChainIds = { "leftLeg" };
		limb->m_Settings.linearPoseBlend = true;
		const int limbId = graph.AddNode(std::move(limb));
		const int outputId = graph.AddNode(std::make_unique<AnimGraphOutputNode>());
		graph.AddLink(inputId,0,goalId,0);
		graph.AddLink(goalId,0,checkpointId,0);
		graph.AddLink(checkpointId,0,limbId,0);
		graph.AddLink(limbId,0,outputId,0);
		nlohmann::json json;
		graph.SerializeToJsonObject(json);
		auto roundtrip = VansAnimGraph::DeserializeFromJsonObject(json);
		if (!Check(roundtrip != nullptr, "Checkpoint graph roundtrip failed")) return false;
		const auto* restoredLimb = dynamic_cast<const AnimGraphLimbIKNode*>(roundtrip->GetNode(limbId));
		if (!Check(restoredLimb && restoredLimb->m_Settings.linearPoseBlend,
			"Limb IK linear pose blend was lost during graph serialization")) return false;
		VansProceduralGraphRuntime runtime;
		std::string error;
		if (!Check(runtime.Configure(*roundtrip,fixture.rig,{},error),error.c_str())) return false;
		float weight = 1;
		VansProceduralParameterAccessor parameters;
		parameters.context = &weight;
		parameters.readFloat=[](const void* data,const std::string& name,float& out)
		{out=*static_cast<const float*>(data);return name=="gripWeight";};
		const int foot = fixture.skeleton.boneNameToIndex.at("foot_l");
		VansAnimationExternalInputSnapshot input;
		VansResolvedAnimationTarget target;
		target.id="grip";target.valid=true;target.space=VansAnimationTargetSpace::Pose;
		target.sourceBoneIndex=foot;target.poseCheckpoint="beforeIK";
		target.sourceLocal=glm::translate(glm::mat4(1),glm::vec3(0.1f,0.12f,0));
		input.targets={target};
		std::vector<VansBoneTransform> output;
		std::vector<VansWorldQueryRequest> requests;
		bool needsResolve=false;
		auto prepare=[&](float deltaTime = 1.0f/60){return runtime.Prepare(deltaTime,fixture.localPose,
			{goalId,checkpointId,limbId},parameters,input,requests,output,needsResolve,error);};
		if (!Check(prepare() && !needsResolve,error.c_str())) return false;
		glm::mat4 checkpointMatrix;
		if (!Check(runtime.TryGetCheckpointTransform("beforeIK",foot,checkpointMatrix),"Checkpoint was not published")) return false;
		VansPoseWorkspace workspace;
		workspace.Initialize(fixture.skeleton,output);
		const auto expected=glm::vec3(checkpointMatrix*target.sourceLocal*glm::vec4(0,0,0,1));
		if (!Check(glm::length(workspace.GetComponentPosition(foot)-expected)<0.003f,"IK did not reach current checkpoint target")) return false;
		const int thigh = fixture.skeleton.FindBoneIndex("thigh_l");
		const int calf = fixture.skeleton.FindBoneIndex("calf_l");
		VansPoseWorkspace inputPose;
		inputPose.Initialize(fixture.skeleton, fixture.localPose);
		const auto rootPosition = inputPose.GetComponentPosition(thigh);
		const auto direction = glm::normalize(expected - rootPosition);
		auto pole = inputPose.GetComponentPosition(calf)
			+ inputPose.GetComponentRotation(calf) * glm::vec3(-0.3f, 0, 0.2f) - rootPosition;
		pole -= direction * glm::dot(pole, direction);
		auto bend = workspace.GetComponentPosition(calf) - rootPosition;
		bend -= direction * glm::dot(bend, direction);
		if (!Check(glm::dot(glm::normalize(pole), glm::normalize(bend)) > 0.999f,
			"Binding goal ignored its independent input-pose pole")) return false;
		// 下一帧从输入姿态重新采样，不能把上一帧 IK 输出积累到目标上。
		if (!Check(prepare(),error.c_str())) return false;
		glm::mat4 next;
		runtime.TryGetCheckpointTransform("beforeIK",foot,next);
		if (!Check(glm::length(glm::vec3(next[3]-checkpointMatrix[3]))<1e-6f,"Checkpoint drifted across frames")) return false;
		input.targets[0].sourceLocal = glm::translate(glm::mat4(1), glm::vec3(-0.12f, 0.16f, 0));
		if (!Check(prepare(0.0f), error.c_str())) return false;
		workspace.Initialize(fixture.skeleton, output);
		const auto pausedTarget = glm::vec3(checkpointMatrix * input.targets[0].sourceLocal * glm::vec4(0,0,0,1));
		if (!Check(glm::length(workspace.GetComponentPosition(foot) - pausedTarget) < 0.003f,
			"Paused procedural evaluation ignored an edited target")) return false;
		weight=0;
		if(!Check(prepare(),error.c_str()))return false;
		for(std::size_t i=0;i<output.size();++i)
			if(!Check(glm::length(output[i].translation-fixture.localPose[i].translation)<1e-6f &&
				std::abs(glm::dot(output[i].rotation,fixture.localPose[i].rotation))>0.999999f,"Zero weight changed input pose"))return false;
		weight=1;input.targets[0].poseCheckpoint.clear();
		if(!Check(prepare(),error.c_str()))return false;
		bool cycleReported=false;
		for(const auto& record:runtime.GetDebugRecords()) if(record.diagnostic.find("Target depends")!=std::string::npos)cycleReported=true;
		if(!Check(cycleReported,"Self-dependent target was not diagnosed"))return false;
		runtime.Reset();
		if(!Check(!runtime.TryGetCheckpointTransform("beforeIK",foot,next),"Reset retained checkpoint pose"))return false;
		Vans::VansSceneParentReference parent;
		const auto parentJson=nlohmann::json{{"kind","socket"},{"entityGuid","00000000-0000-4000-8000-000000000002"},
			{"animationComponentGuid","00000000-0000-4000-8000-000000000003"},{"anchorGuid","00000000-0000-4000-8000-000000000004"},{"poseCheckpoint","beforeIK"}};
		if(!Check(Vans::TryReadSceneParentReference(Vans::DecodeSerializedValueJson(parentJson),parent,error) && parent.poseCheckpoint=="beforeIK","Checkpoint attachment serialization failed"))return false;
		return true;
	}

	bool TestComponentBoneTransformCheckpoint()
	{
		LegFixture fixture;
		if (!BuildLegFixture(fixture, false)) return false;
		const int foot = fixture.skeleton.FindBoneIndex("foot_l");
		VansPoseWorkspace original;
		if (!Check(original.Initialize(fixture.skeleton, fixture.localPose), "Bone transform fixture failed")) return false;
		const glm::vec3 before = original.GetComponentPosition(foot);
		VansAnimGraph graph;
		const int inputId = graph.AddNode(std::make_unique<AnimGraphTargetPoseInputNode>());
		auto checkpoint = std::make_unique<AnimGraphPoseCheckpointNode>();
		checkpoint->m_CheckpointId = "pre";
		checkpoint->m_Bones = {"foot_l"};
		const int checkpointId = graph.AddNode(std::move(checkpoint));
		auto transform = std::make_unique<AnimGraphComponentBoneTransformNode>();
		transform->m_BoneName = "foot_l";
		transform->m_PositionParameter = "position";
		transform->m_RotationParameter = "rotation";
		transform->m_AlphaParameter = "alpha";
		const int transformId = graph.AddNode(std::move(transform));
		const int outputId = graph.AddNode(std::make_unique<AnimGraphOutputNode>());
		graph.AddLink(inputId,0,checkpointId,0);
		graph.AddLink(checkpointId,0,transformId,0);
		graph.AddLink(transformId,0,outputId,0);
		nlohmann::json json;
		graph.SerializeToJsonObject(json);
		auto loaded = VansAnimGraph::DeserializeFromJsonObject(json);
		if (!Check(loaded && loaded->GetNode(transformId)->GetType()==VansAnimGraphNodeType::ComponentBoneTransform,
			"Bone Transform graph serialization lost the node")) return false;
		VansProceduralGraphRuntime runtime;
		std::string error;
		if (!Check(runtime.Configure(*loaded, fixture.rig, {}, error), error.c_str())) return false;
		struct Values {float alpha;glm::vec3 position;glm::quat rotation;} values{
			1.0f, before+glm::vec3(.1f,.2f,0), glm::angleAxis(glm::radians(30.0f),glm::vec3(0,1,0))};
		VansProceduralParameterAccessor parameters;
		parameters.context=&values;
		parameters.readFloat=[](const void* data,const std::string& name,float& value)
		{if(name!="alpha")return false;value=static_cast<const Values*>(data)->alpha;return true;};
		parameters.readVector3=[](const void* data,const std::string& name,glm::vec3& value)
		{if(name!="position")return false;value=static_cast<const Values*>(data)->position;return true;};
		parameters.readQuaternion=[](const void* data,const std::string& name,glm::quat& value)
		{if(name!="rotation")return false;value=static_cast<const Values*>(data)->rotation;return true;};
		VansAnimationExternalInputSnapshot input;
		std::vector<VansBoneTransform> output;
		std::vector<VansWorldQueryRequest> requests;
		bool needsResolve=false;
		auto prepare=[&](){return runtime.Prepare(1.0f/60,fixture.localPose,
			{checkpointId,transformId},parameters,input,requests,output,needsResolve,error);};
		if (!Check(prepare() && !needsResolve,error.c_str())) return false;
		glm::mat4 snapshot;
		if (!Check(runtime.TryGetCheckpointTransform("pre",foot,snapshot)
			&& glm::length(glm::vec3(snapshot[3])-before)<1e-6f,
			"Bone Transform changed an upstream pose checkpoint")) return false;
		VansPoseWorkspace result;
		if (!Check(result.Initialize(fixture.skeleton,output)
			&& glm::length(result.GetComponentPosition(foot)-values.position)<1e-5f
			&& std::abs(glm::dot(result.GetComponentRotation(foot),values.rotation))>.99999f,
			"Bone Transform did not replace component position and rotation")) return false;
		values.alpha=0;
		if (!Check(prepare() && result.Initialize(fixture.skeleton,output)
			&& glm::length(result.GetComponentPosition(foot)-before)<1e-6f,
			"Zero-alpha Bone Transform changed the input pose")) return false;
		auto* additive = static_cast<AnimGraphComponentBoneTransformNode*>(loaded->GetNode(transformId));
		additive->m_PositionAdditive = true;
		additive->m_RotationAdditive = true;
		additive->m_WorldSpace = true;
		if (!Check(runtime.Configure(*loaded,fixture.rig,{},error),error.c_str())) return false;
		values.alpha = 1;
		values.position = glm::vec3(.1f,0,0);
		values.rotation = glm::angleAxis(glm::radians(90.0f),glm::vec3(0,1,0));
		input.ownerWorld = glm::rotate(glm::mat4(1),glm::radians(90.0f),glm::vec3(0,1,0));
		if (!Check(prepare() && result.Initialize(fixture.skeleton,output)
			&& glm::length(result.GetComponentPosition(foot)-(before+glm::vec3(0,0,.1f)))<1e-5f
			&& std::abs(glm::dot(result.GetComponentRotation(foot),
				values.rotation*original.GetComponentRotation(foot)))>.99999f,
			"World-space additive Bone Transform lost owner rotation or composition")) return false;
		return true;
	}

	bool TestGroundingRigEditContinuity()
	{
		LegFixture fixture;
		if (!BuildLegFixture(fixture, false)) return false;
		auto settings = GroundingSettings(false); settings.pelvis.halfLife = 0.12f;
		VansCompiledGroundingSettings compiled;
		std::string error;
		if (!VansCompileGroundingSettings(settings, fixture.rig, compiled, error)) return Check(false, error.c_str());
		VansGroundingRuntime source;
		if (!source.Configure(fixture.rig, compiled, error)) return Check(false, error.c_str());
		VansPoseWorkspace pose;
		VansAnimationExternalInputSnapshot input;
		std::vector<VansProceduralGoal> goals;
		VansProceduralSolverResult result;
		for (int frame = 0; frame < 60; ++frame)
		{
			pose.Initialize(fixture.skeleton, fixture.localPose);
			if (!ResolveGround(source, pose, input, {0,1,0}, {0,-0.15f,0}, goals, result)) return Check(false, "Grounding warmup failed");
		}
		auto changedRig = fixture.rig;
		VansCompiledRigJointLimit limit; limit.boneIndex = fixture.skeleton.boneNameToIndex.at("foot_l");
		changedRig.jointLimits.push_back(limit);
		VansGroundingRuntime replacement;
		if (!replacement.Configure(changedRig, compiled, error)) return Check(false, error.c_str());
		replacement.TransferStateForRigReplacement(source);
		const auto paused = [&](VansGroundingRuntime& runtime)
		{
			pose.Initialize(fixture.skeleton, fixture.localPose);
			std::vector<VansWorldQueryRequest> requests;
			if (!runtime.Prepare(pose, input, requests) || !runtime.Resolve(0, pose, input,
				BuildPlaneResults(requests, {0,1,0}, {0,-0.15f,0}), goals, result)) return false;
			runtime.CommitResolvedState(); return true;
		};
		if (!paused(source)) return Check(false, "Paused source grounding failed");
		const int pelvis = fixture.skeleton.boneNameToIndex.at("pelvis");
		const auto expected = pose.GetComponentPosition(pelvis);
		if (!Check(std::abs(expected.y - 1) > 0.05f, "Grounding continuity fixture did not produce a pelvis offset")) return false;
		if (!paused(replacement)) return Check(false, "Paused replacement grounding failed");
		if (!Check(glm::length(pose.GetComponentPosition(pelvis) - expected) < 1.e-5f,
			"Editing Rig limits reset unrelated grounding state")) return false;
		changedRig.contacts[0].soleSamplesLocal[0].positionLocal.x += 0.01f;
		VansGroundingRuntime incompatible;
		if (!incompatible.Configure(changedRig, compiled, error)) return Check(false, error.c_str());
		incompatible.TransferStateForRigReplacement(source);
		if (!paused(incompatible)) return Check(false, "Changed contact grounding failed");
		return Check(glm::length(pose.GetComponentPosition(pelvis) - expected) > 0.05f,
			"Changed contact geometry incorrectly inherited stale grounding state");
	}

	bool TestRotationDistribution()
	{
		// 非人体骨名、三个模型轴、非单位绑定旋转与父层级，防止把 Survival 的轴写死。
		for (const glm::vec3 axisLocal : {glm::vec3(1,0,0), glm::vec3(0,-1,0), glm::normalize(glm::vec3(1,2,3))})
		{
			Skeleton skeleton;
			const int root = AddBone(skeleton, "mount", -1, {0,0,0});
			const int base = AddBone(skeleton, "segment", root, {0,1,0});
			const int tip = AddBone(skeleton, "tool", base, axisLocal);
			const int helper = AddBone(skeleton, "sleeve", base, axisLocal * 0.4f);
			const int childHelper = AddBone(skeleton, "sleeveEnd", helper, axisLocal * 0.3f);
			AddBone(skeleton, "unrelated", root, {1,0,0});
			skeleton.bones[root].localTransform *= glm::toMat4(glm::angleAxis(0.8f, glm::normalize(glm::vec3(2,1,-1))));
			skeleton.bones[base].localTransform *= glm::toMat4(glm::angleAxis(0.4f, glm::vec3(0,0,1)));
			skeleton.bones[tip].localTransform *= glm::toMat4(glm::angleAxis(0.6f, glm::vec3(0,1,0)));
			skeleton.bones[helper].localTransform *= glm::toMat4(glm::angleAxis(-0.3f, glm::vec3(1,0,0)));
			skeleton.sourceSkeletonGuid = "00000000-0000-4000-8000-000000000071";
			skeleton.BuildTopologicalOrder();
			VansAnimationRigAsset asset;
			asset.name = "Rotation fixture"; asset.skeletonGuid = skeleton.sourceSkeletonGuid;
			asset.goals = {{"toolGoal", "tool"}};
			asset.rotationDistributions = {{"toolRotation", "toolGoal", "segment", 0.6f,
				{{"sleeveEnd", 0.7f}, {"sleeve", 0.4f}}}};
			VansCompiledAnimationRig rig;
			std::string error;
			if (!Check(VansAnimationRigCompiler::Compile(asset, skeleton, rig, error), error.c_str())) return false;
			const auto input = BuildLocalPose(skeleton);
			VansPoseWorkspace pose;
			pose.Initialize(skeleton, input);
			const auto baseRotation = pose.GetComponentRotation(base);
			const auto tipRotation = pose.GetComponentRotation(tip);
			const auto position = pose.GetComponentPosition(tip);
			const auto axis = glm::normalize(position - pose.GetComponentPosition(base));
			const auto helperRotation = pose.GetComponentRotation(helper);
			const auto childRotation = pose.GetComponentRotation(childHelper);
			VansProceduralGoal goal;
			goal.valid = true; goal.rotationWeight = 1;
			goal.rotationModel = glm::angleAxis(glm::radians(100.0f), axis) * tipRotation;
			VansRotationDistributionState rotationState;
			const auto solve = [&]() { return VansRotationDistributionSolver::Solve(pose, rig, rig.rotationDistributions[0], goal, rotationState); };
			const auto closeRotation = [](const glm::quat& a, const glm::quat& b)
			{ return std::abs(glm::dot(glm::normalize(a), glm::normalize(b))) > 0.99999f; };
			if (!Check(solve().status == VansProceduralSolverStatus::Solved, "Rotation profile did not solve")) return false;
			if (!Check(glm::length(pose.GetComponentPosition(tip) - position) < 1.e-5f &&
				closeRotation(pose.GetComponentRotation(tip), goal.rotationModel) &&
				closeRotation(pose.GetComponentRotation(base), glm::angleAxis(glm::radians(60.f), axis) * baseRotation) &&
				closeRotation(pose.GetComponentRotation(helper), glm::angleAxis(glm::radians(40.f), axis) * helperRotation) &&
				closeRotation(pose.GetComponentRotation(childHelper), glm::angleAxis(glm::radians(70.f), axis) * childRotation),
				"Rotation fractions moved the endpoint or double-applied inherited twist")) return false;
			pose.Initialize(skeleton, input); goal.rotationWeight = 0.5f; solve();
			if (!Check(closeRotation(pose.GetComponentRotation(tip), glm::angleAxis(glm::radians(50.f), axis) * tipRotation) &&
				closeRotation(pose.GetComponentRotation(base), glm::angleAxis(glm::radians(30.f), axis) * baseRotation),
				"Rotation activation was applied more than once")) return false;
			pose.Initialize(skeleton, input); goal.rotationWeight = 0;
			if (!Check(solve().status == VansProceduralSolverStatus::NoEffect, "Disabled rotation profile was not a no-op")) return false;
			for (std::size_t i = 0; i < input.size(); ++i)
				if (!Check(pose.GetLocal(static_cast<int>(i)).rotation == input[i].rotation &&
					pose.GetLocal(static_cast<int>(i)).translation == input[i].translation, "Zero activation changed pose")) return false;
			goal.rotationWeight = 1;
			goal.rotationModel = glm::quat(0, axis.x, axis.y, axis.z) * tipRotation;
			pose.Initialize(skeleton, input); solve();
			const auto firstHalfTurn = pose.GetComponentRotation(base);
			goal.rotationModel = -goal.rotationModel;
			pose.Initialize(skeleton, input); solve();
			if (!Check(closeRotation(firstHalfTurn, pose.GetComponentRotation(base)), "q and -q chose different twist directions")) return false;
			rotationState = {};
			goal.rotationModel = glm::angleAxis(glm::radians(179.f), axis) * tipRotation;
			pose.Initialize(skeleton, input); solve();
			const auto beforeWrap = pose.GetComponentRotation(base);
			goal.rotationModel = glm::angleAxis(glm::radians(181.f), axis) * tipRotation;
			pose.Initialize(skeleton, input); solve();
			if (!Check(VansQuaternionAngleDegrees(glm::inverse(beforeWrap) * pose.GetComponentRotation(base)) < 1.3f,
				"Crossing 180 degrees flipped the upstream rotation")) return false;
			goal.rotationWeight = 0; solve();
			if (!Check(!rotationState.valid, "Disabling rotation retained a twist branch")) return false;
			goal.rotationWeight = 1;
			goal.rotationModel = glm::angleAxis(glm::radians(100.f), axis) * tipRotation;
			VansRigJointLimitDefinition baseLimit;
			baseLimit.bone = "segment"; baseLimit.kind = VansJointLimitKind::Hinge;
			baseLimit.axisLocal = axisLocal; baseLimit.minDegrees = -25; baseLimit.maxDegrees = 25;
			VansRigJointLimitDefinition tipLimit;
			tipLimit.bone = "tool"; tipLimit.kind = VansJointLimitKind::SwingTwist;
			tipLimit.axisLocal = glm::inverse(input[tip].rotation) * axisLocal;
			tipLimit.swingReferenceAxisLocal = glm::normalize(glm::cross(tipLimit.axisLocal,
				std::abs(tipLimit.axisLocal.z) < 0.9f ? glm::vec3(0,0,1) : glm::vec3(0,1,0)));
			tipLimit.minDegrees = -20; tipLimit.maxDegrees = 20; tipLimit.swingLimitDegrees = {35,45};
			asset.jointLimits = {baseLimit, tipLimit};
			if (!Check(VansAnimationRigCompiler::Compile(asset, skeleton, rig, error), error.c_str())) return false;
			pose.Initialize(skeleton, input);
			const auto limited = solve();
			if (!Check(limited.status == VansProceduralSolverStatus::Clamped && limited.rotationErrorDegrees > 54 &&
				glm::length(pose.GetComponentPosition(tip) - position) < 1.e-5f &&
				closeRotation(pose.GetComponentRotation(base), glm::angleAxis(glm::radians(25.f), axis) * baseRotation),
				"Base/tip joint limits failed or displaced the solved endpoint")) return false;
			for (int bone : {base, tip})
				if (!Check(!VansApplyJointLimit(pose.GetLocal(bone).rotation, rig.FindJointLimit(bone)).limited,
					"Rotation output violated a configured joint limit")) return false;
			pose.Initialize(skeleton, input);
			pose.SetComponentRotation(tip, goal.rotationModel);
			const auto beforeFade = pose.GetComponentRotation(tip);
			goal.rotationWeight = 0.001f;
			solve();
			if (!Check(VansQuaternionAngleDegrees(glm::inverse(beforeFade) * pose.GetComponentRotation(tip)) < 0.2f,
				"Activating a limit on an out-of-range input pose caused a discontinuity")) return false;
			goal.rotationWeight = 1;
			for (float degrees : {160.f, 260.f, 360.f})
			{
				goal.rotationModel = glm::angleAxis(glm::radians(degrees), axis) * tipRotation;
				pose.Initialize(skeleton, input); solve();
				if (!Check(closeRotation(pose.GetComponentRotation(base), glm::angleAxis(glm::radians(25.f), axis) * baseRotation),
					"A full turn wrapped through the base joint limit")) return false;
			}
			nlohmann::json json;
			if (!Check(VansAnimationRigStorage::SerializeToJsonObject(asset, json, error), error.c_str())) return false;
			auto dto = Vans::EditorAPI::AnimationAuthoringBridge::DecodeAnimationRig(json.dump());
			if (!Check(dto.success && dto.document.rotationDistributions.size() == 1 && dto.document.jointLimits.size() == 2,
				"Rig public DTO lost rotation profiles or limits")) return false;
			auto encoded = Vans::EditorAPI::AnimationAuthoringBridge::EncodeAnimationRig(dto.document);
			if (!Check(encoded.success && nlohmann::json::parse(encoded.canonicalJson) == json, "Rig editor roundtrip changed configuration")) return false;
			auto invalid = asset;
			invalid.rotationDistributions[0].recipients.push_back({"tool", 0.5f});
			if (!Check(!VansAnimationRigCompiler::Compile(invalid, skeleton, rig, error), "Effector subtree accepted as an auxiliary recipient")) return false;
			invalid = asset; invalid.rotationDistributions[0].baseBone = "mount";
			if (!Check(!VansAnimationRigCompiler::Compile(invalid, skeleton, rig, error), "Non-segment hierarchy accepted for axial distribution")) return false;
		}
		return true;
	}

	bool TestRotationDistributionGraph()
	{
		LegFixture fixture;
		if (!BuildLegFixture(fixture, true)) return false;
		fixture.asset.rotationDistributions = {{"toolRotation", "leftFoot", "calf_l", 0.6f, {}}};
		std::string error;
		if (!Check(VansAnimationRigCompiler::Compile(fixture.asset, fixture.skeleton, fixture.rig, error), error.c_str())) return false;
		VansAnimGraph graph;
		const int inputId = graph.AddNode(std::make_unique<AnimGraphTargetPoseInputNode>());
		auto goal = std::make_unique<AnimGraphGoalNode>();
		goal->m_Goal.goalId = "leftFoot"; goal->m_Goal.binding = "toolAnchor"; goal->m_Goal.fixedRotationWeight = 1;
		const int goalId = graph.AddNode(std::move(goal));
		auto rotation = std::make_unique<AnimGraphRotationDistributionNode>(); rotation->m_RotationProfileId = "toolRotation";
		const int rotationId = graph.AddNode(std::move(rotation));
		const int outputId = graph.AddNode(std::make_unique<AnimGraphOutputNode>());
		graph.AddLink(inputId, 0, goalId, 0); graph.AddLink(goalId, 0, rotationId, 0); graph.AddLink(rotationId, 0, outputId, 0);
		nlohmann::json json;
		graph.SerializeToJsonObject(json);
		auto loaded = VansAnimGraph::DeserializeFromJsonObject(json);
		if (!Check(loaded && static_cast<AnimGraphRotationDistributionNode*>(loaded->GetNode(rotationId))->m_RotationProfileId == "toolRotation",
			"Rotation graph properties did not roundtrip")) return false;
		VansProceduralGraphRuntime runtime;
		if (!Check(runtime.Configure(*loaded, fixture.rig, {}, error), error.c_str())) return false;
		VansAnimationExternalInputSnapshot external;
		VansResolvedAnimationTarget target;
		target.id = "toolAnchor"; target.valid = true; target.rotationWorld = glm::angleAxis(glm::radians(90.f), glm::vec3(0,-1,0));
		external.targets = {target};
		std::vector<VansBoneTransform> output;
		std::vector<VansWorldQueryRequest> requests;
		bool needsResolve = false;
		const auto prepare = [&]() { return runtime.Prepare(0, fixture.localPose, {goalId, rotationId}, {}, external, requests, output, needsResolve, error); };
		if (!Check(prepare() && !needsResolve, error.c_str())) return false;
		VansPoseWorkspace pose; pose.Initialize(fixture.skeleton, output);
		const int base = fixture.skeleton.boneNameToIndex.at("calf_l");
		const auto first = pose.GetComponentRotation(base);
		if (!Check(std::abs(glm::dot(first, glm::angleAxis(glm::radians(54.f), glm::vec3(0,-1,0)))) > 0.99999f,
			"Paused graph did not execute rotation distribution")) return false;
		if (!Check(prepare(), error.c_str())) return false;
		pose.Initialize(fixture.skeleton, output);
		if (!Check(std::abs(glm::dot(first, pose.GetComponentRotation(base))) > 0.999999f, "Rotation graph accumulated pose between evaluations")) return false;
		external.targets[0].space = VansAnimationTargetSpace::Pose;
		external.targets[0].sourceBoneIndex = base;
		if (!Check(prepare(), error.c_str())) return false;
		if (!Check(runtime.GetDebugRecords().back().diagnostic.find("Target depends") != std::string::npos,
			"Rotation write set was missing from target feedback detection")) return false;
		auto duplicate = std::make_unique<AnimGraphRotationDistributionNode>(); duplicate->m_RotationProfileId = "toolRotation";
		const int duplicateId = loaded->AddNode(std::move(duplicate));
		for (const auto& link : loaded->GetLinks()) if (link.toNodeId == outputId) { loaded->RemoveLink(link.linkId); break; }
		loaded->AddLink(rotationId, 0, duplicateId, 0); loaded->AddLink(duplicateId, 0, outputId, 0);
		return Check(!runtime.Configure(*loaded, fixture.rig, {}, error), "Overlapping rotation profiles were accepted");
	}

	bool TestBlendSpace2DGraph()
	{
		VansAnimGraph graph;
		const int entryId = graph.AddNode(std::make_unique<AnimGraphEntryNode>());
		auto blend = std::make_unique<AnimGraphBlendSpace2DNode>();
		blend->m_XParamName = "AimYaw";
		blend->m_YParamName = "AimPitch";
		blend->m_Samples = { { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } };
		const int blendId = graph.AddNode(std::move(blend));
		const int outputId = graph.AddNode(std::make_unique<AnimGraphOutputNode>());
		graph.AddLink(entryId, 0, blendId, 0);
		graph.AddLink(blendId, 0, outputId, 0);
		nlohmann::json json;
		graph.SerializeToJsonObject(json);
		auto loaded = VansAnimGraph::DeserializeFromJsonObject(json);
		if (!Check(loaded != nullptr, "BlendSpace2D graph did not deserialize"))
			return false;
		const auto* loadedNode = loaded->GetNode(blendId);
		if (!Check(loadedNode && loadedNode->GetType() == VansAnimGraphNodeType::BlendSpace2D,
			"BlendSpace2D node type did not roundtrip"))
			return false;
		const auto* loadedBlend = static_cast<const AnimGraphBlendSpace2DNode*>(loadedNode);
		return Check(loadedBlend->m_XParamName == "AimYaw"
			&& loadedBlend->m_YParamName == "AimPitch"
			&& loadedBlend->m_Samples.size() == 3
			&& loadedBlend->m_Samples[1].x == 1.0f,
			"BlendSpace2D parameters or samples did not roundtrip");
	}

	bool TestProjectSceneProceduralConfiguration()
	{
		std::filesystem::path workspace = std::filesystem::current_path();
		for (int depth = 0; depth < 5
			&& !std::filesystem::exists(workspace / "AnimationV2Project"); ++depth)
		{
			if (!workspace.has_parent_path() || workspace.parent_path() == workspace) break;
			workspace = workspace.parent_path();
		}
		if (!Check(std::filesystem::exists(workspace / "AnimationV2Project")
			&& std::filesystem::exists(workspace / "DemoHallProject"),
			"Animation project fixtures could not be located")) return false;
		const std::unordered_map<std::string, std::string> animationV2Rigs = {
			{ "TwinBlast", "4dbe6fc1-88c9-4b06-82b5-e4c6c9f37002" },
			{ "SWAT", "4dbe6fc1-88c9-4b06-82b5-e4c6c9f37004" },
			{ "Survival", "4dbe6fc1-88c9-4b06-82b5-e4c6c9f37005" },
			{ "Belica", "4dbe6fc1-88c9-4b06-82b5-e4c6c9f37003" }
		};
		const std::unordered_map<std::string, std::string> demoHallRigs = {
			{ "UEFN_Mannequin", "65af9371-2a97-43f1-93a8-04dc2e4f1001" },
			{ "Survival", "65af9371-2a97-43f1-93a8-04dc2e4f1002" }
		};
		return ValidateProjectScene(
			workspace / "AnimationV2Project" / "Scenes" / "MainScene.json", animationV2Rigs)
			&& ValidateProjectScene(
				workspace / "DemoHallProject" / "Scenes" / "DemoHall.json", demoHallRigs);
	}
}

bool RunProceduralAnimationContractTests()
{
	return TestTrsPoseMathContract()
		&& TestSkeletonIdentityAndHierarchyContract()
		&& TestGroundingRigEditContinuity()
		&& TestBlendSpace2DGraph()
		&& TestRotationDistribution()
		&& TestRotationDistributionGraph()
		&& TestTransformTargetHierarchy()
		&& TestChainTargetOrientation()
		&& TestTransformTargetCheckpoint()
		&& TestComponentBoneTransformCheckpoint()
		&& TestGroundingPlanesAndAirborne()
		&& TestGroundingContactWeightingAndStaticSeams()
		&& TestGroundingMovingSupport()
		&& TestJointConstraintMath()
		&& TestRigValidationAndLimbSolver()
		&& TestChainAndAimSolvers()
		&& TestStrictRetargetAndContactConfiguration()
		&& TestExplicitRetargetMappingAndBake()
		&& TestProjectSceneProceduralConfiguration();
}

bool RunComponentBoneTransformContractTest()
{
	return TestComponentBoneTransformCheckpoint();
}

bool RunExplicitRetargetMappingAndBakeContract()
{
	return TestStrictRetargetAndContactConfiguration() && TestExplicitRetargetMappingAndBake();
}

bool RunWeaponRayAlignmentContract()
{
	return TestChainAndAimSolvers();
}

bool RunLeonSourceGraphComparison()
{
	using namespace VansGraphics;
	auto workspace=std::filesystem::current_path();for(int i=0;i<6 && !std::filesystem::is_directory(workspace/"DemoHallProject");++i)workspace=workspace.parent_path();
	const auto project=workspace/"DemoHallProject",task=project/"Tools/LeonSurvival";std::string error;
	nlohmann::json oracle,stages,input;std::ifstream(task/"runtime_input.json")>>input;
	std::ifstream(task/"Audit/ue_source_stage_oracle.json")>>oracle;std::ifstream(task/"Audit/source_graph_stages.json")>>stages;
	nlohmann::json temporal;std::ifstream(task/"Audit/ue_source_transition_oracle.json")>>temporal;for(const auto& row:temporal["cases"])oracle["cases"].push_back(row);
	nlohmann::json finalTemporal;std::ifstream(task/"Audit/ue_final_temporal_oracle.json")>>finalTemporal;for(const auto& row:finalTemporal["cases"])oracle["cases"].push_back(row);
	AnimatorAssetData asset;if(!VansAnimatorIO::Load((project/"Assets/AnimationSources/Leon/Leon_Source.vanimator").string(),asset))return false;
	nlohmann::json canonical;if(!VansAnimatorIO::SerializeToJsonObject(asset,canonical,error))return Check(false,error.c_str());
	const auto dto=Vans::EditorAPI::AnimationAuthoringBridge::DecodeAnimator(canonical.dump());if(!dto.success)return Check(false,"Source Animator DTO decode failed");
	const auto encoded=Vans::EditorAPI::AnimationAuthoringBridge::EncodeAnimator(*dto.document);
	if(!Check(encoded.success && nlohmann::json::parse(encoded.canonicalJson)==canonical,"Source Animator changed through native/DTO/native"))return false;
	std::unordered_map<std::string,std::shared_ptr<const VansAnimationClipAsset>> clips;
	for(const auto& ref:asset.clipRefs)if(!clips.count(ref.assetGuid)){auto clip=std::make_shared<VansAnimationClipAsset>();if(!VansAnimationClipIO::Load((project/ref.pathHint).string(),clip->clip,clip->skeleton))return false;clips.emplace(ref.assetGuid,std::move(clip));}
	const auto skeleton=clips.at(asset.clipRefs[0].assetGuid)->skeleton;auto rig=std::make_shared<VansAnimationRigAsset>();
	if(!Check(skeleton.bones.size()==415,"Source graph lost source skeleton") || !VansAnimationRigStorage::Load(project/"Assets/AnimationSources/Leon/Leon_Source.vanimrig",*rig,error))return Check(false,error.c_str());
	VansAnimatorRuntimeCompileOptions options;options.normalizeRootPose=false;options.enableTargetPostProcess=false;options.enableFinalComposition=false;options.enableRootMotion=false;
	options.rigResolver=[rig](const auto&,auto&){return rig;};
	auto resolveClip=[&](const auto& ref,auto& out,auto&){out=clips.at(ref.assetGuid);return true;};auto resolveMask=[](const auto&,auto& out,auto&){out.reset();return true;};
	auto fullOptions=options;fullOptions.mode=VansAnimatorRuntimeCompileMode::RetargetSource;fullOptions.enableTargetPostProcess=true;fullOptions.enableFinalComposition=true;
	auto eventController=VansAnimatorRuntimeCompiler::Compile(asset,skeleton,resolveClip,resolveMask,fullOptions,error);if(!eventController)return Check(false,error.c_str());
	{
		auto base=*eventController->GetClip("Base_Idle");AnimationClipEvent early;early.id=4231;early.time=.1f;early.name="BeforeProcedural";
		base.events.push_back(early);eventController->AddClip("Base_Idle",std::move(base));
	}
	eventController->Play();eventController->Update(0,skeleton);
	eventController->SetBool("LeonBaseLoop",false);eventController->SetFloat("LeonBaseTimeA",.4f);eventController->SetFloat("LeonBaseTimeB",.4f);
	eventController->SetInt("LeonShotEpoch",1);eventController->SetFloat("LeonNativeShotEventTime",.4f);
	eventController->SetFloat("LeonNativeShotTime",.4f);eventController->SetBool("LeonShotActive",true);eventController->SetFloat("LeonNativeShotWeight",1);
	eventController->PrepareFrame(.4f,skeleton);
	const auto earlyRange=eventController->ClaimSampledEventPublicationRange();
	if(!Check(earlyRange.begin==0 && earlyRange.end>0,"Pre-CCT fixture did not publish its base event prefix"))return false;
	eventController->GatherPreparedWorldQueries(skeleton);
	if(eventController->HasPreparedWorldQueries())eventController->ResolvePreparedWorldQueries({},skeleton);
	int nativeShotEvents=0;
	for(const auto& event:eventController->GetSampledEvents())
	{std::cout<<"NATIVE_SOURCE_EVENT name="<<event.name<<" clip="<<event.clipId<<" weight="<<event.weight<<'\n';if(event.name=="Pistol_Shot.Finished" && event.clipId==VansAnimationStableId("Pistol_Shot") && event.weight>=.5f)++nativeShotEvents;}
	if(!Check(nativeShotEvents==1,"Full CCT-prepared source pipeline lost native shot completion"))return false;
	const auto lateRange=eventController->ClaimSampledEventPublicationRange();
	if(!Check(lateRange.begin==earlyRange.end && lateRange.end==lateRange.begin+1
		&& eventController->GetSampledEvents()[lateRange.begin].name=="Pistol_Shot.Finished",
		"Final event publication repeated the base prefix or omitted the late shot"))return false;
	const auto repeated=eventController->ClaimSampledEventPublicationRange();
	if(!Check(repeated.begin==repeated.end,"An event suffix was published twice"))return false;
	asset.composeBeforeRetarget=false;
	auto graphOnly=VansAnimatorRuntimeCompiler::Compile(asset,skeleton,resolveClip,resolveMask,fullOptions,error);if(!graphOnly)return Check(false,error.c_str());
	graphOnly->Play();graphOnly->SetInt("LeonShotEpoch",1);graphOnly->SetFloat("LeonNativeShotEventTime",.4f);graphOnly->Update(.4f,skeleton);
	if(!Check(graphOnly->GetSampledEvents().empty(),"Source composition opt-in changed graph-only retarget behavior"))return false;
	asset.composeBeforeRetarget=true;
	{
		nlohmann::json motionOracle;std::ifstream(task/"Audit/native_motion_oracle.json")>>motionOracle;
		auto motionOptions=options;motionOptions.enableRootMotion=true;
		auto motion=VansAnimatorRuntimeCompiler::Compile(asset,skeleton,resolveClip,resolveMask,motionOptions,error);if(!motion)return Check(false,error.c_str());
		float maximum=0;std::size_t intervals=0;
		for(const auto& group:motionOracle["groups"])
		{
			motion->Play();motion->SetBool("LeonBaseLoop",false);
			motion->SetInt("LeonBaseA",input["baseIndex"][group["first"].get<std::string>()].get<int>());
			motion->SetInt("LeonBaseB",input["baseIndex"][group["second"].get<std::string>()].get<int>());
			for(const auto& frame:group["frames"])
			{
				motion->SetFloat("LeonBaseTimeA",frame["timeA"].get<float>());motion->SetFloat("LeonBaseTimeB",frame["timeB"].get<float>());
				motion->SetFloat("LeonDirectionBlend",frame["blend"].get<float>());
				if(!motion->PrepareIndependentCharacterMotion(1.f/60,skeleton))return Check(false,"Native CCT motion graph did not own root preparation");
				const auto& expected=frame["expectedTranslation"];
				maximum=std::max(maximum,glm::distance(motion->GetRootMotionDelta(),glm::vec3(expected[0].get<float>(),expected[1].get<float>(),expected[2].get<float>())));++intervals;
			}
		}
		nlohmann::json report={{"producer",motionOracle["producer"]},{"intervals",intervals},{"maxPositionMeters",maximum},{"passed",maximum<.0001f}};
		std::ofstream(task/"Audit/native_motion_comparison.json")<<report.dump(2);
		std::cout<<"NATIVE_PAIR_ROOT_COMPARISON intervals="<<intervals<<" errorMeters="<<maximum<<'\n';
		if(!Check(maximum<.0001f,"Incoming PairRoot diverged from UE root-position intervals"))return false;
	}
	// 正式编译入口必须在外部姿势模式下保留显式启用的目标动作图。
	AnimatorAssetData targetAsset;if(!VansAnimatorIO::Load((project/"Assets/AnimationSources/Leon/Survival_Leon_Target.vanimator").string(),targetAsset))return false;
	std::unordered_map<std::string,std::shared_ptr<const VansAnimationClipAsset>> targetClips;
	for(const auto& ref:targetAsset.clipRefs)if(!targetClips.count(ref.assetGuid)){auto clip=std::make_shared<VansAnimationClipAsset>();if(!VansAnimationClipIO::Load((project/ref.pathHint).string(),clip->clip,clip->skeleton))return false;targetClips.emplace(ref.assetGuid,std::move(clip));}
	auto targetRig=std::make_shared<VansAnimationRigAsset>();if(!VansAnimationRigStorage::Load(project/"Assets/AnimationSources/Leon/Survival_Leon_Target.vanimrig",*targetRig,error))return false;
	VansAnimatorRuntimeCompileOptions targetOptions;targetOptions.mode=VansAnimatorRuntimeCompileMode::ExternalPoseTarget;
	targetOptions.rigResolver=[targetRig](const auto&,auto&){return targetRig;};targetOptions.queryProfileResolver=[](const auto&,std::uint32_t& mask,auto&){mask=1;return true;};
	auto resolveTargetClip=[&](const auto& ref,auto& out,auto&){out=targetClips.at(ref.assetGuid);return true;};
	nlohmann::json targetCanonical;if(!VansAnimatorIO::SerializeToJsonObject(targetAsset,targetCanonical,error))return false;
	const auto targetDTO=Vans::EditorAPI::AnimationAuthoringBridge::DecodeAnimator(targetCanonical.dump());
	if(!targetDTO.success)return Check(false,"Target overlay DTO decode failed");
	const auto targetEncoded=Vans::EditorAPI::AnimationAuthoringBridge::EncodeAnimator(*targetDTO.document);
	if(!Check(targetEncoded.success&&nlohmann::json::parse(targetEncoded.canonicalJson)==targetCanonical,"External pose layer Graph Set changed through DTO"))return false;
	auto resolveTargetMask=[&](const auto& layer,auto& out,auto& message){if(layer.maskGuid.empty()){out.reset();return true;}auto mask=std::make_shared<VansBoneMaskAsset>();if(!VansBoneMaskStorage::Load(project/layer.maskPathHint,*mask,message))return false;out=std::move(mask);return true;};
	auto targetController=VansAnimatorRuntimeCompiler::Compile(targetAsset,targetClips.at(targetAsset.clipRefs[0].assetGuid)->skeleton,resolveTargetClip,resolveTargetMask,targetOptions,error);
	if(!Check(targetController && targetController->HasGraphSet("graph-set-preserved-air") && targetController->HasGraphSet("graph-set-preserved-window") && targetController->HasGraphSet("graph-set-attack"),error.c_str()))return false;
	{
		const Skeleton targetSkeleton=*targetController->GetRuntimeSkeleton();
		VansRetargetProfileAsset profile;if(!VansRetargetProfileStorage::Load(project/"Assets/Retarget/Leon_To_Survival.vretarget",profile,error))return Check(false,error.c_str());
		VansRetargetRuntimeDesc desc;desc.translationScaleMode=profile.translationScaleMode;desc.translationScale=profile.explicitTranslationScale;
		desc.rootAlignment=profile.rootAlignment;desc.targetModelSpaceAlignment=profile.targetModelSpaceAlignment;
		desc.limbChains=profile.limbChains;desc.controlBones=profile.controlBones;desc.boneMappings=profile.boneMappings;desc.outputModelRotation=profile.outputModelRotation;
		VansAnimationNode node("Native Source Events");node.SetSkeleton(targetSkeleton);
		if(!node.SetController(std::move(targetController))||!node.ConfigureRetargetSource(skeleton,std::move(eventController),desc,error))return Check(false,error.c_str());
		node.Play(VansAnimationEvaluationPurpose::EditorPreview);node.Update({VansAnimationEvaluationPurpose::EditorPreview,0});
		auto* parameters=node.GetController();parameters->SetInt("LeonShotEpoch",1);parameters->SetFloat("LeonNativeShotEventTime",.4f);
		parameters->SetBool("LeonShotActive",true);parameters->SetFloat("LeonNativeShotWeight",1);parameters->SetFloat("LeonNativeShotTime",.4f);
		parameters->SetBool("LeonBaseLoop",false);parameters->SetFloat("LeonBaseTimeA",.4f);parameters->SetFloat("LeonBaseTimeB",.4f);
		Vans::VansCharacterTrajectory trajectory;node.PrepareCharacterMotionFrame(.4f,trajectory);
		const auto rootRange=node.ClaimSampledEventPublicationRange();if(!Check(rootRange.begin==rootRange.end,"Independent root preparation emitted pose events"))return false;
		node.Update({VansAnimationEvaluationPurpose::EditorPreview,.4f});
		const auto posed=node.ClaimSampledEventPublicationRange();int shots=0;
		for(auto i=posed.begin;i<posed.end;++i)if(node.GetSampledEvents()[i].name=="Pistol_Shot.Finished")++shots;
		if(!Check(shots==1,"Actual independent-root/post-CCT/retarget node lost the late shot event"))return false;
		const auto again=node.ClaimSampledEventPublicationRange();if(!Check(again.begin==again.end,"Native source node republished its events"))return false;
		if(!Check(node.RestartLayer("layer-survival-attack-upper")&&node.UsesRetargetSource()&&!node.RestartLayer("missing-layer"),
			"Authored target overlay restart did not preserve source locomotion owner"))return false;
		auto pulse=*parameters->GetClip("Attack_Crowbar");AnimationClipEvent targetEvent;targetEvent.id=4532;targetEvent.time=.03f;targetEvent.name="TargetOverlayPulse";
		pulse.events.push_back(targetEvent);parameters->AddClip("Attack_Crowbar",std::move(pulse));parameters->SetFloat("SurvivalAttackWeight",1);
		node.RestartLayer("layer-survival-attack-upper");node.PrepareCharacterMotionFrame(.1f,trajectory);
		node.ClaimSampledEventPublicationRange();node.Update({VansAnimationEvaluationPurpose::EditorPreview,.1f});
		const auto overlayRange=node.ClaimSampledEventPublicationRange();int targetPulses=0;
		for(auto i=overlayRange.begin;i<overlayRange.end;++i)if(node.GetSampledEvents()[i].name=="TargetOverlayPulse")++targetPulses;
		const auto overlayAgain=node.ClaimSampledEventPublicationRange();
		if(!Check(targetPulses==1&&overlayAgain.begin==overlayAgain.end&&node.UsesRetargetSource(),"Target overlay events did not reach the source-owned event stream exactly once"))return false;
		std::cout<<"TARGET_OVERLAY_EVENTS_PASS source owner, target restart, suffix once\n";
		std::cout<<"NATIVE_LATE_EVENTS_PASS prefix, suffix, noDuplicates, independentRoot, postCCT, retarget\n";
	}
	nlohmann::json report={{"producer","Source CPU Animator versus independent UE compressed pose stage equations"},{"cases",nlohmann::json::array()}};
	float maxPosition=0,maxAngle=0,maxScale=0;std::size_t samples=0;
	const auto originalGraph=asset.FindGraph(stages["graph"].get<std::string>())->Clone();
	for(const auto& row:oracle["cases"])
	{
		for(auto& definition:asset.graphs)if(definition.id==stages["graph"].get<std::string>())definition.graph=originalGraph->Clone();
		const bool finalStage=row["stage"]=="final";
		if(!finalStage)
		{
		auto* graph=asset.FindGraph(stages["graph"].get<std::string>());const int output=graph->GetOutputNodeId();const auto links=graph->GetLinks();
		for(const auto& link:links)if(link.toNodeId==output)graph->RemoveLink(link.linkId);
		graph->AddLink(stages["stages"][row["stage"].get<std::string>()].get<int>(),0,output,0);
		std::unordered_set<int> reachable;std::vector<int> pending{output};while(!pending.empty()){const int id=pending.back();pending.pop_back();if(!reachable.insert(id).second)continue;for(const auto& link:graph->GetLinks())if(link.toNodeId==id)pending.push_back(link.fromNodeId);}
		std::vector<int> remove;for(const auto& [id,node]:graph->GetNodes())if(!reachable.count(id))remove.push_back(id);for(const int id:remove)graph->RemoveNode(id);
		}
		auto controller=VansAnimatorRuntimeCompiler::Compile(asset,skeleton,resolveClip,resolveMask,finalStage?fullOptions:options,error);if(!controller)return Check(false,error.c_str());
		controller->Play();const auto frames=row.contains("frames")?row["frames"]:nlohmann::json::array({row});int frameIndex=0;
		for(const auto& frame:frames)
		{
		controller->SavePoseSnapshot("LeonPreviousDisplay",skeleton);
		for(const auto& parameter:frame["parameters"].items())
		{const auto& value=parameter.value();if(value.is_array())controller->SetVector3(parameter.key(),glm::vec3(value[0].get<float>(),value[1].get<float>(),value[2].get<float>()));
		else if(value.is_boolean())controller->SetBool(parameter.key(),value.get<bool>());else if(value.is_number_integer())controller->SetInt(parameter.key(),value.get<int>());else controller->SetFloat(parameter.key(),value.get<float>());}
		// JSON 中数值的 int/float 表示不决定 Animator 的参数类型。
		for(const auto& parameter:asset.parameters)if(frame["parameters"].contains(parameter.name) && parameter.type==AnimatorParamType::Float)controller->SetFloat(parameter.name,frame["parameters"][parameter.name].get<float>());
		controller->Update(frame.value("dt",1.f/60),skeleton);const auto view=controller->GetFinalPoseView(skeleton);
		if(!Check(view.IsValid(),"Source graph did not produce a full final pose"))return false;
		std::vector<glm::mat4> local;if(!VansPoseMath::BuildLocalTransforms(controller->GetCachedGlobalTransforms(),skeleton,local))return false;
		float position=0,angle=0,scale=0;std::string worst;
		for(const auto& entry:frame["local"].items())
		{
			const auto& v=entry.value();VansBoneTransform actual;if(!VansPoseMath::TryDecompose(local[skeleton.FindBoneIndex(entry.key())],actual))return false;
			const auto expected=glm::normalize(glm::quat(v[6].get<float>(),v[3].get<float>(),v[4].get<float>(),v[5].get<float>()));
			position=std::max(position,glm::distance(actual.translation,glm::vec3(v[0].get<float>(),v[1].get<float>(),v[2].get<float>())));
			scale=std::max(scale,glm::distance(actual.scale,glm::vec3(v[7].get<float>(),v[8].get<float>(),v[9].get<float>())));
			const float rotation=glm::degrees(2*std::acos(std::clamp(std::abs(glm::dot(actual.rotation,expected)),0.f,1.f)));if(rotation>angle){angle=rotation;worst=entry.key();}++samples;
		}
		report["cases"].push_back({{"name",row["name"]},{"frame",frameIndex++},{"temporal",row.contains("frames")},{"stage",row["stage"]},{"positionMeters",position},{"rotationDegrees",angle},{"scaleError",scale},{"worstBone",worst}});
		maxPosition=std::max(maxPosition,position);maxAngle=std::max(maxAngle,angle);maxScale=std::max(maxScale,scale);
		}
	}
	report["samples"]=samples;report["maxPositionMeters"]=maxPosition;report["maxRotationDegrees"]=maxAngle;report["maxScaleError"]=maxScale;report["passed"]=maxPosition<.0002f && maxAngle<.25f && maxScale<.0001f;
	std::ofstream(task/"Audit/source_stage_comparison.json")<<report.dump(2);
	std::cout<<"LEON_SOURCE_STAGE_COMPARISON cases="<<oracle["cases"].size()<<" samples="<<samples<<" position="<<maxPosition<<" angle="<<maxAngle<<" scale="<<maxScale<<'\n';return report["passed"].get<bool>();
}

bool RunPoseHistoryBlendContract()
{
	using namespace VansGraphics;
	std::vector<std::vector<VertexBoneData>> skin(1,std::vector<VertexBoneData>(1));std::string paletteError;
	skin[0][0].boneIDs[0]=static_cast<int>(MAX_BONES)-1;skin[0][0].weights[0]=1;
	if(!VansAnimationGpuBinding::ValidateSkinWeights(skin,paletteError))return false;
	skin[0][0].boneIDs[0]=MAX_BONES;
	if(!Check(!VansAnimationGpuBinding::ValidateSkinWeights(skin,paletteError),"CPU skeleton expansion allowed an out-of-range rendered bone"))return false;
	skin[0][0].weights[0]=0;if(!VansAnimationGpuBinding::ValidateSkinWeights(skin,paletteError))return false;
	// 深层共享子图共有 2^48 条路径，校验只需遍历节点与不同的速率上下文。
	VansAnimGraph diamond;auto leaf=std::make_unique<AnimGraphClipNode>();leaf->m_ClipName="pose";int current=diamond.AddNode(std::move(leaf));
	for(int i=0;i<48;++i){const int blend=diamond.AddNode(std::make_unique<AnimGraphBlendNode>());diamond.AddLink(current,0,blend,0);diamond.AddLink(current,0,blend,1);current=blend;}
	const int diamondOutput=diamond.AddNode(std::make_unique<AnimGraphOutputNode>());diamond.AddLink(current,0,diamondOutput,0);
	std::vector<int> plan;std::string planError;if(!Check(diamond.BuildExecutionPlan(plan,planError) && plan.size()==50,"Shared DAG speed validation did not finish with one entry per node"))return false;
	VansAnimGraph conflict;auto clipSource=std::make_unique<AnimGraphClipNode>();clipSource->m_ClipName="pose";const int source=conflict.AddNode(std::move(clipSource));
	const int speedA=conflict.AddNode(std::make_unique<AnimGraphSpeedScaleNode>()),speedB=conflict.AddNode(std::make_unique<AnimGraphSpeedScaleNode>()),combined=conflict.AddNode(std::make_unique<AnimGraphBlendNode>()),conflictOutput=conflict.AddNode(std::make_unique<AnimGraphOutputNode>());
	conflict.AddLink(source,0,speedA,0);conflict.AddLink(source,0,speedB,0);conflict.AddLink(speedA,0,combined,0);conflict.AddLink(speedB,0,combined,1);conflict.AddLink(combined,0,conflictOutput,0);
	if(!Check(!conflict.BuildExecutionPlan(plan,planError) && planError.find("conflicting SpeedScale paths")!=std::string::npos,"DAG memoization hid a shared clock conflict"))return false;
	Skeleton skeleton;AddBone(skeleton,"root",-1,glm::vec3(0));AddBone(skeleton,"hand",0,glm::vec3(0));skeleton.BuildTopologicalOrder();
	VansAnimationClip clip;clip.duration=1;clip.boneKeyframes.resize(2);
	for(int i=0;i<2;++i)clip.boneKeyframes[i]={{0,glm::vec3(0),glm::quat(1,0,0,0),glm::vec3(1)},{1,glm::vec3((i+1)*10,0,0),glm::quat(1,0,0,0),glm::vec3(1)}};
	std::unordered_map<std::string,VansAnimationClip> clips{{"pose",clip}};
	std::unordered_map<std::string,AnimatorParameter> parameters;
	for(const auto name:{"revision","mode","mask"})parameters[name].type=AnimatorParamType::Int;
	for(const auto name:{"time","alpha"})parameters[name].type=AnimatorParamType::Float;
	parameters["active"].type=AnimatorParamType::Bool;parameters["active"].boolVal=true;
	VansAnimGraph graph;auto player=std::make_unique<AnimGraphClipNode>();player->m_ClipName="pose";player->m_SampleTimeParameter="time";player->m_Loop=false;
	const int input=graph.AddNode(std::move(player));auto definition=std::make_unique<AnimGraphPoseHistoryBlendNode>();auto* history=definition.get();
	history->m_RevisionParameter="revision";history->m_AlphaParameter="alpha";history->m_ModeParameter="mode";history->m_MaskIndexParameter="mask";history->m_ActiveParameter="active";history->m_UnionMaskCoverage=true;
	for(const auto bone:{"hand","root"}){VansBoneMaskAsset mask;mask.id=bone;mask.name=bone;mask.defaultWeight=0;mask.explicitWeights[bone]=1;history->m_Masks.push_back(mask);}
	const int historyId=graph.AddNode(std::move(definition)),output=graph.AddNode(std::make_unique<AnimGraphOutputNode>());graph.AddLink(input,0,historyId,0);graph.AddLink(historyId,0,output,0);
	AnimGraphContext context;context.skeleton=&skeleton;context.clips=&clips;context.parameters=&parameters;context.deltaTime=.02f;
	VansAnimGraphInstance instance(graph);auto sample=[&](float time,int revision,float alpha,int mask,int mode=0){parameters["time"].floatVal=time;parameters["revision"].intVal=revision;parameters["alpha"].floatVal=alpha;parameters["mask"].intVal=mask;parameters["mode"].intVal=mode;return instance.EvaluateFrame(context);};
	sample(.1f,1,1,0);const auto saved=instance.CaptureRuntimeState();auto result=sample(.5f,2,0,1);
	if(!Check(result.valid && std::abs(result.localPose[0].translation.x-1)<1.e-5f && std::abs(result.localPose[1].translation.x-2)<1.e-5f,"History coverage union did not preserve the displayed prior pose"))return false;
	result=sample(.6f,2,.5f,1);
	if(!Check(std::abs(result.localPose[0].translation.x-3.5f)<1.e-5f && std::abs(result.localPose[1].translation.x-7)<1.e-5f,"History blended from a recursive current value instead of captured pose"))return false;
	if(!instance.RestoreRuntimeState(saved))return false;sample(.5f,2,0,1);const auto replay=sample(.6f,2,.5f,1);
	if(!Check(glm::distance(replay.localPose[1].translation,result.localPose[1].translation)<1.e-6f,"History snapshot lost interrupted blend state"))return false;
	result=sample(.9f,3,0,0);if(!Check(std::abs(result.localPose[1].translation.x-7)<1.e-5f,"Revision interruption did not capture displayed result"))return false;
	result=sample(.3f,4,0,0,1);result=sample(.4f,4,.5f,0,1);
	if(!Check(std::abs(result.localPose[0].translation.x-4.25f)<1.e-5f && std::abs(result.localPose[1].translation.x-8.5f)<1.e-5f,"Offset tail stopped the incoming clip or used velocity inertia"))return false;
	// Actor 转向时，根子骨须保持捕获时的世界位置和旋转。
	history->m_AlignRootChildren=true;history->m_UnionMaskCoverage=false;
	VansAnimGraphInstance worldInstance(graph);parameters["time"].floatVal=.1f;parameters["alpha"].floatVal=1;parameters["mask"].intVal=0;parameters["mode"].intVal=0;parameters["revision"].intVal=1;
	const auto first=worldInstance.EvaluateFrame(context);const auto oldWorld=VansPoseMath::Compose(first.localPose[0])*VansPoseMath::Compose(first.localPose[1]);
	context.ownerWorldTransform=glm::rotate(glm::mat4(1),glm::half_pi<float>(),glm::vec3(0,0,1));parameters["revision"].intVal=2;parameters["alpha"].floatVal=0;
	const auto aligned=worldInstance.EvaluateFrame(context);const auto actualWorld=context.ownerWorldTransform*VansPoseMath::Compose(aligned.localPose[0])*VansPoseMath::Compose(aligned.localPose[1]);
	for(int c=0;c<4;++c)if(!Check(glm::length(actualWorld[c]-oldWorld[c])<1.e-4f,"History root-child alignment changed planted world pose"))return false;
	history->m_AlignRootChildren=false;history->m_DampingSeconds=.04f;history->m_DampingMask=history->m_Masks[0];
	VansAnimGraphInstance damped(graph);context.ownerWorldTransform=glm::mat4(1);parameters["active"].boolVal=false;damped.EvaluateFrame(context);parameters["active"].boolVal=true;parameters["mode"].intVal=2;
	const auto filtered=damped.EvaluateFrame(context);
	if(!Check(std::abs(filtered.localPose[1].translation.x-2*(1-std::exp(-.5f)))<1.e-5f
		&& std::abs(filtered.localPose[0].translation.x-1)<1.e-5f,"Exponential damping touched unfiltered bones or did not reset to identity"))return false;
	// 当前标量遮罩与旧通道并集分开；内层显示缓存不能被外层权重污染。
	VansAnimationClip liveClip=clip;for(auto& keys:liveClip.boneKeyframes)for(auto& key:keys)key.position=glm::vec3(0);clips["live"]=liveClip;
	parameters["outerWeight"].type=AnimatorParamType::Float;parameters["outerWeight"].floatVal=.5f;
	VansAnimGraph weightedGraph;auto sourcePlayer=std::make_unique<AnimGraphClipNode>();sourcePlayer->m_ClipName="pose";sourcePlayer->m_SampleTimeParameter="time";sourcePlayer->m_Loop=false;
	const int weightedSource=weightedGraph.AddNode(std::move(sourcePlayer));auto livePlayer=std::make_unique<AnimGraphClipNode>();livePlayer->m_ClipName="live";livePlayer->m_SampleTime=0;
	const int liveInput=weightedGraph.AddNode(std::move(livePlayer));auto weightedDefinition=std::make_unique<AnimGraphPoseHistoryBlendNode>(*history);auto* weighted=weightedDefinition.get();
	weighted->m_DampingSeconds=0;weighted->m_RetainCurrentMaskWeights=true;weighted->m_UnionMaskCoverage=true;weighted->m_OutputWeightParameter="outerWeight";
	weighted->m_Masks[1]=weighted->m_Masks[0];weighted->m_Masks[1].explicitWeights["hand"]=.25f;weighted->m_Masks[1].explicitChannelWeights["hand"]=glm::vec3(0,1,0);
	const int weightedHistory=weightedGraph.AddNode(std::move(weightedDefinition)),weightedOutput=weightedGraph.AddNode(std::make_unique<AnimGraphOutputNode>());
	weightedGraph.AddLink(weightedSource,0,weightedHistory,0);weightedGraph.AddLink(liveInput,0,weightedHistory,1);weightedGraph.AddLink(weightedHistory,0,weightedOutput,0);
	VansAnimGraphInstance weightedInstance(weightedGraph);auto weightedSample=[&](float time,int revision,float alpha,int mask){parameters["time"].floatVal=time;parameters["revision"].intVal=revision;parameters["alpha"].floatVal=alpha;parameters["mask"].intVal=mask;parameters["mode"].intVal=0;return weightedInstance.EvaluateFrame(context);};
	const auto initialWeighted=weightedSample(.1f,1,1,0);const auto changedWeighted=weightedSample(.5f,2,0,1);
	const auto advancedWeighted=weightedSample(.6f,2,.5f,1);
	if(!Check(std::abs(initialWeighted.localPose[1].translation.x-1)<1.e-5f && std::abs(changedWeighted.localPose[1].translation.x-1)<1.e-5f
		&& std::abs(advancedWeighted.localPose[1].translation.x-1.34375f)<1.e-5f,"History did not retain current scalar, previous channel coverage or unweighted displayed cache"))return false;
	parameters["active"].boolVal=false;if(!Check(glm::length(weightedInstance.EvaluateFrame(context).localPose[1].translation)<1.e-6f,"Inactive weighted history did not return live base"))return false;
	// The external display includes later procedural/additive stages, not this node's last input.
	weighted->m_HistorySnapshotName="display";weighted->m_HistorySnapshotParameter="captureDisplay";
	parameters["captureDisplay"].type=AnimatorParamType::Bool;parameters["captureDisplay"].boolVal=true;parameters["active"].boolVal=true;
	std::vector<VansBoneTransform> display(2);display[1].translation=glm::vec3(30,0,0);
	weightedInstance.SetPoseSnapshot("display",display);const auto fromDisplay=weightedSample(.2f,9,0,0);
	if(!Check(std::abs(fromDisplay.localPose[1].translation.x-15)<1.e-5f,"History captured pre-solver node pose instead of final displayed snapshot"))return false;
	parameters["captureDisplay"].boolVal=false;const auto localHistory=weightedSample(.2f,10,0,0);
	if(!Check(std::abs(localHistory.localPose[1].translation.x-15)<1.e-5f,"Disabled external capture lost ordinary node history"))return false;
	auto workspace=std::filesystem::current_path();for(int i=0;i<6 && !std::filesystem::is_directory(workspace/"DemoHallProject");++i)workspace=workspace.parent_path();
	AnimatorAssetData asset;std::string error;if(!VansAnimatorIO::Load((workspace/"DemoHallProject/Assets/Animations/LeonSurvival/Survival_Leon.vanimator").string(),asset))return false;
	auto* final=asset.FindFinalCompositionGraph();const int finalOutput=final->GetOutputNodeId();const auto links=final->GetLinks();
	int targetInput=-1;for(const auto& [id,node]:final->GetNodes())if(node->GetType()==VansAnimGraphNodeType::TargetPoseInput)targetInput=id;
	for(const auto& link:links)if(link.toNodeId==finalOutput){final->RemoveLink(link.linkId);auto copy=std::make_unique<AnimGraphPoseHistoryBlendNode>(*weighted);const int added=final->AddNode(std::move(copy));final->AddLink(link.fromNodeId,link.fromPinIndex,added,0);final->AddLink(targetInput,0,added,1);final->AddLink(added,0,finalOutput,0);}
	for(const auto& [name,value]:parameters)if(name!="time"){auto added=value;added.name=name;asset.parameters.push_back(added);}
	nlohmann::json canonical;if(!VansAnimatorIO::SerializeToJsonObject(asset,canonical,error))return Check(false,error.c_str());
	const auto dto=Vans::EditorAPI::AnimationAuthoringBridge::DecodeAnimator(canonical.dump());if(!dto.success)return Check(false,"Pose history DTO decode failed");
	const auto encoded=Vans::EditorAPI::AnimationAuthoringBridge::EncodeAnimator(*dto.document);
	if(!Check(encoded.success && nlohmann::json::parse(encoded.canonicalJson)==canonical,"Pose history fields changed through DTO"))return false;
	std::cout<<"POSE_HISTORY_BLEND_PASS coverage union, interruption, offset tail, world alignment, damping, snapshot, DTO\n";return true;
}

bool RunMaskedLocalDeltaContract()
{
	using namespace VansGraphics;
	Skeleton skeleton;AddBone(skeleton,"root",-1,glm::vec3(0));AddBone(skeleton,"hand",0,glm::vec3(0,1,0));skeleton.BuildTopologicalOrder();
	VansBoneMaskAsset mask;mask.id="native-delta";mask.name="Native Delta";mask.defaultWeight=0;
	mask.explicitWeights["hand"]=.8f;mask.explicitChannelWeights["hand"]=glm::vec3(.25f,1.f,0.f);
	VansAnimationFrameVector<VansBoneTransform> base(2),delta(2),actual;
	base[1].translation=glm::vec3(2,3,4);base[1].rotation=glm::angleAxis(.7f,glm::vec3(0,1,0));base[1].scale=glm::vec3(2,3,4);
	delta[0].translation=glm::vec3(100);delta[1].translation=glm::vec3(4,8,12);delta[1].rotation=glm::angleAxis(1.f,glm::vec3(1,0,0));delta[1].scale=glm::vec3(2);
	const auto compiled=VansBoneMaskCompiler::Compile(mask,skeleton);
	if(!Check(compiled.valid && VansPoseMath::ApplyMaskedLocalDeltaPose(base,delta,compiled,.5f,actual),"Masked native delta failed"))return false;
	const auto expected=glm::angleAxis(.4f,glm::vec3(1,0,0))*base[1].rotation;
	if(!Check(glm::length(actual[0].translation)<1.e-6f && glm::distance(actual[1].translation,glm::vec3(2.4f,3.8f,5.2f))<1.e-5f
		&& std::abs(glm::dot(expected,actual[1].rotation))>1-1.e-6f && actual[1].scale==base[1].scale,
		"Masked delta changed excluded TRS or used right-multiplied rotation"))return false;
	// 图运行时独立检查，防止仅数学接口正确而节点丢掉通道遮罩。
	std::unordered_map<std::string,VansAnimationClip> clips;
	for(const auto& [name,pose]:std::vector<std::pair<std::string,VansAnimationFrameVector<VansBoneTransform>>>{{"base",base},{"delta",delta}})
	{auto& clip=clips[name];clip.duration=1;clip.boneKeyframes.resize(2);for(int i=0;i<2;++i)clip.boneKeyframes[i]={{0,pose[i].translation,pose[i].rotation,pose[i].scale}};}
	VansAnimGraph graph;auto player=[&](const char* alias){auto node=std::make_unique<AnimGraphClipNode>();node->m_ClipName=alias;node->m_SampleTime=0;return graph.AddNode(std::move(node));};
	const int first=player("base"),second=player("delta");auto blend=std::make_unique<AnimGraphAdditiveBlendNode>();blend->m_FixedWeight=.5f;blend->m_AdditiveMode=VansAdditivePoseMode::LocalSphericalLeft;blend->m_UseBoneMask=true;blend->m_BoneMask=mask;
	const int mix=graph.AddNode(std::move(blend)),output=graph.AddNode(std::make_unique<AnimGraphOutputNode>());graph.AddLink(first,0,mix,0);graph.AddLink(second,0,mix,1);graph.AddLink(mix,0,output,0);
	VansAnimGraphInstance instance(graph);AnimGraphContext context;context.skeleton=&skeleton;context.clips=&clips;
	const auto result=instance.EvaluateFrame(context);if(!Check(result.valid && glm::distance(result.localPose[1].translation,actual[1].translation)<1.e-6f
		&& std::abs(glm::dot(result.localPose[1].rotation,expected))>1-1.e-6f,"Additive graph ignored compiled mask"))return false;
	auto workspace=std::filesystem::current_path();for(int i=0;i<6 && !std::filesystem::is_directory(workspace/"DemoHallProject");++i)workspace=workspace.parent_path();
	AnimatorAssetData asset;std::string error;if(!VansAnimatorIO::Load((workspace/"DemoHallProject/Assets/Animations/LeonSurvival/Survival_Leon.vanimator").string(),asset))return false;
	AnimGraphAdditiveBlendNode* masked=nullptr;for(auto& [id,node]:asset.FindFinalCompositionGraph()->GetNodes())if(node->GetType()==VansAnimGraphNodeType::AdditiveBlend)masked=static_cast<AnimGraphAdditiveBlendNode*>(node.get());
	if(!masked)return false;masked->m_UseBoneMask=true;masked->m_BoneMask=mask;
	nlohmann::json canonical;if(!VansAnimatorIO::SerializeToJsonObject(asset,canonical,error))return Check(false,error.c_str());
	const auto dto=Vans::EditorAPI::AnimationAuthoringBridge::DecodeAnimator(canonical.dump());if(!dto.success)return Check(false,"Masked delta DTO decode failed");
	const auto encoded=Vans::EditorAPI::AnimationAuthoringBridge::EncodeAnimator(*dto.document);
	if(!Check(encoded.success && nlohmann::json::parse(encoded.canonicalJson)==canonical,"Masked native delta fields changed through DTO"))return false;
	masked->m_BoneMask.explicitChannelWeights["hand"].x=std::numeric_limits<float>::quiet_NaN();
	if(!Check(!VansAnimatorValidator::Validate(asset,error),"Typed validator accepted a nonfinite inline channel mask"))return false;
	std::cout<<"MASKED_LOCAL_DELTA_PASS independent TRS, graph, exact DTO, invalid definition\n";return true;
}

bool RunIndependentCharacterMotionContract()
{
	using namespace VansGraphics;
	Skeleton skeleton;AddBone(skeleton,"root",-1,glm::vec3(0));AddBone(skeleton,"child",0,glm::vec3(0,1,0));
	skeleton.sourceSkeletonGuid="f2eea6ee-9f63-49c1-93d1-5372af9f79f1";skeleton.BuildTopologicalOrder();
	VansAnimationRigAsset rigAsset;rigAsset.name="Independent Motion";rigAsset.skeletonGuid=skeleton.sourceSkeletonGuid;
	VansCompiledAnimationRig rig;std::string error;if(!VansAnimationRigCompiler::Compile(rigAsset,skeleton,rig,error))return Check(false,error.c_str());
	VansAnimationClip clip;clip.clipName="motion";clip.duration=1;clip.boneKeyframes.resize(2);
	clip.boneKeyframes[0]={{0,glm::vec3(0),glm::quat(1,0,0,0),glm::vec3(1)},
		{1,glm::vec3(1,0,0),glm::angleAxis(glm::radians(90.f),glm::vec3(0,0,1)),glm::vec3(1)}};
	clip.boneKeyframes[1]={{0,glm::vec3(0,1,0),glm::quat(1,0,0,0),glm::vec3(1)}};
	clip.rootMotion.boneName="root";clip.rootMotion.forceLock=true;clip.rootMotion.lockMode=AnimationRootLockMode::ReferencePose;
	VansAnimationSampleRequest request;request.loop=false;request.previousTime=.25f;request.currentTime=.5f;
	const auto interval=VansAnimationSampler::ExtractRootMotion(clip,skeleton,request);
	request.rootMotionReferenceSpace=true;
	const auto reference=VansAnimationSampler::ExtractRootMotion(clip,skeleton,request);
	if(!Check(glm::distance(reference.translation,glm::vec3(.25f,0,0))<1.e-5f && glm::distance(reference.translation,interval.translation)>.05f,
		"Reference root translation was rotated by the interval's prior heading"))return false;
	request.loop=true;request.previousTime=.9f;request.currentTime=1.1f;
	if(!Check(glm::distance(VansAnimationSampler::ExtractRootMotion(clip,skeleton,request).translation,glm::vec3(.2f,0,0))<1.e-5f,
		"Reference root loop did not preserve authored translation across the endpoint"))return false;
	Vans::VansCharacterLocomotionResolver resolver;resolver.Clear(glm::vec3(0),90);
	Vans::VansCharacterMotionIntent intent;intent.valid=true;intent.gravity=0;intent.rootMotionReferenceYaw=0;resolver.SetIntent(intent);
	Vans::VansCharacterMotionSettings settings;settings.rootRotationWeight=0;
	resolver.Prepare(.25f,settings,glm::vec3(0),90,true,false);
	Vans::VansLocomotionAuthority authority;authority.mode=Vans::VansLocomotionAuthorityMode::RootMotion;
	const auto travel=resolver.Resolve(glm::vec3(1,0,0),glm::quat(1,0,0,0),true,authority,settings,glm::vec3(1),90);
	if(!Check(glm::distance(travel.displacementWorld,glm::vec3(1,0,0))<1.e-5f,"CCT ignored the authored translation heading"))return false;
	auto makeGraph=[&](bool history){auto graph=std::make_unique<VansAnimGraph>();
		auto sample=std::make_unique<AnimGraphClipNode>();sample->m_ClipName="motion";sample->m_Loop=false;sample->m_RootMotion=true;
		sample->m_SampleTimeParameter="Time";sample->m_SampleTimeOutputs=true;sample->m_SampleEpochParameter="Revision";
		sample->m_SampleFromZeroOnEpoch=true;sample->m_RootMotionReferenceSpace=true;
		int input=graph->AddNode(std::move(sample));
		if(history){auto blend=std::make_unique<AnimGraphPoseHistoryBlendNode>();blend->m_RevisionParameter="Revision";
			blend->m_AlphaParameter="Alpha";blend->m_AlignRootChildren=true;const int next=graph->AddNode(std::move(blend));graph->AddLink(input,0,next,0);input=next;}
		const int output=graph->AddNode(std::make_unique<AnimGraphOutputNode>());graph->AddLink(input,0,output,0);return graph;};
	if(!Check(!makeGraph(true)->ValidateCharacterMotionGraph(error),"Motion graph accepted pose history side effects"))return false;
	auto makeController=[&](bool independent){auto controller=std::make_unique<VansAnimationController>();controller->AddClip("motion",clip);
		controller->AddParameter("Time",AnimatorParamType::Float);controller->AddParameter("Alpha",AnimatorParamType::Float);controller->AddParameter("Revision",AnimatorParamType::Int);
		controller->SetFloat("Alpha",1);controller->SetInt("Revision",1);
		std::vector<VansAnimationLayerSetup> layers(1);layers[0].definition.id="base";layers[0].definition.name="Base";layers[0].definition.kind=VansAnimationLayerKind::Base;
		std::vector<VansAnimationGraphSetSetup> sets(1);auto& set=sets[0];set.definition.id="default";set.definition.name="Default";
		set.definition.bindings.push_back({"base","pose",true});set.bindings.push_back({{"base","pose",true},makeGraph(true)});
		if(independent){set.definition.characterMotionGraphId="root";set.characterMotionGraph=makeGraph(false);}
		if(!controller->SetAnimationGraphSets(std::move(layers),std::move(sets),"default",{},{},error)||!controller->SetAnimationRig(rig,{},error))return std::unique_ptr<VansAnimationController>{};
		controller->SetNormalizeRootPose(false);controller->EnableRootMotion(true);controller->Play();controller->Update(0,skeleton);return controller;};
	auto source=makeController(true),target=makeController(false);if(!Check(source && target,error.c_str()))return false;
	if(!Check(!target->PrepareIndependentCharacterMotion(.25f,skeleton),"Unconfigured controller stopped using its existing prepare path"))return false;
	source->SetFloat("Time",.25f);
	if(!source->PrepareIndependentCharacterMotion(.25f,skeleton)||!Check(glm::distance(source->GetRootMotionDelta(),glm::vec3(.25f,0,0))<1.e-5f,
		"Motion entry discarded the first interval from zero"))return false;
	std::string transfer;auto resumed=makeController(true);
	if(!resumed->TransferRuntimeStateFrom(*source,skeleton,transfer))return Check(false,transfer.c_str());
	resumed->SetFloat("Time",.5f);resumed->PrepareIndependentCharacterMotion(.25f,skeleton);
	if(!Check(glm::distance(resumed->GetRootMotionDelta(),glm::vec3(.25f,0,0))<1.e-5f,"Motion hot reload restarted its clip interval"))return false;
	source->SetInt("Revision",2);source->SetFloat("Time",.1f);source->PrepareIndependentCharacterMotion(.1f,skeleton);
	if(!Check(glm::distance(source->GetRootMotionDelta(),glm::vec3(.1f,0,0))<1.e-5f,"Same clip reentry reused the preceding epoch"))return false;
	const auto id=Vans::VansTransformStore::Allocate();struct Release{std::uint32_t id;~Release(){Vans::VansTransformStore::Release(id);}} release{id};
	auto transform=Vans::VansTransformStore::Read(id);transform.m_Position=glm::vec3(0);transform.m_Rotation=glm::vec3(0);transform.m_Scale=glm::vec3(1);Vans::VansTransformStore::Write(id,transform);
	VansAnimationNode node("Independent Motion");node.SetSkeleton(skeleton);node.SetTransformID(id);
	VansRetargetRuntimeDesc desc;desc.translationScaleMode=VansRetargetTranslationScaleMode::Explicit;desc.translationScale=1;
	if(!node.SetController(std::move(target))||!node.ConfigureRetargetSource(skeleton,std::move(source),desc,error))return Check(false,error.c_str());
	node.Play(VansAnimationEvaluationPurpose::EditorPreview);auto* active=node.GetCharacterMotionController();auto* parameters=node.GetController();
	parameters->SetInt("Revision",1);parameters->SetFloat("Time",0);parameters->SetFloat("Alpha",1);node.Update({VansAnimationEvaluationPurpose::EditorPreview,0});
	const auto revision=active->GetFinalPoseView(skeleton).revision;
	parameters->SetInt("Revision",2);parameters->SetFloat("Time",.25f);parameters->SetFloat("Alpha",.5f);
	Vans::VansCharacterTrajectory trajectory;node.PrepareCharacterMotionFrame(.25f,trajectory);
	if(!Check(active->GetFinalPoseView(skeleton).revision==revision,"Preparing CCT root advanced displayed pose history"))return false;
	transform.m_Position.x=.25f;Vans::VansTransformStore::Write(id,transform);
	node.Update({VansAnimationEvaluationPurpose::EditorPreview,.25f});
	const float local=active->GetCachedGlobalTransforms()[1][3].x;
	if(!Check(std::abs(local+.125f)<1.e-4f,"Source base history did not evaluate in the post-CCT owner frame"))return false;
	std::cout<<"INDEPENDENT_CHARACTER_MOTION_PASS firstInterval, epoch, referenceFrame, hotReload, postCCT, legacyFallback\n";
	return true;
}

bool RunRetargetControllerRoutingContract()
{
	using namespace VansGraphics;
	if(!TestConfigurableRetargetReferences())return false;
	Skeleton skeleton;AddBone(skeleton,"root",-1,glm::vec3(0));skeleton.sourceSkeletonGuid="f2eea6ee-9f63-49c1-93d1-5372af9f79f1";skeleton.BuildTopologicalOrder();
	VansAnimationRigAsset asset;asset.name="Routing Fixture";asset.skeletonGuid=skeleton.sourceSkeletonGuid;
	VansCompiledAnimationRig rig;std::string error;if(!VansAnimationRigCompiler::Compile(asset,skeleton,rig,error))return Check(false,error.c_str());
	auto makeController=[&](bool target,float duration=0.f){
		auto controller=std::make_unique<VansAnimationController>();
		VansAnimationClip clip;clip.clipName="pose";clip.duration=1;clip.boneKeyframes.resize(1);clip.boneKeyframes[0]={{0,glm::vec3(0),glm::quat(1,0,0,0),glm::vec3(1)},{1,glm::vec3(1,0,0),glm::quat(1,0,0,0),glm::vec3(1)}};clip.rootMotion.boneName="root";controller->AddClip("pose",clip);
		auto graph=[](){auto value=std::make_unique<VansAnimGraph>();auto clip=std::make_unique<AnimGraphClipNode>();clip->m_ClipName="pose";clip->m_Loop=false;clip->m_RootMotion=true;const int input=value->AddNode(std::move(clip));const int output=value->AddNode(std::make_unique<AnimGraphOutputNode>());value->AddLink(input,0,output,0);return value;};
		std::vector<VansAnimationLayerSetup> layers(1);layers[0].definition.id="base";layers[0].definition.name="Base";layers[0].definition.kind=VansAnimationLayerKind::Base;
		std::vector<VansAnimationGraphSetSetup> sets;
		for(const std::string id:target?std::vector<std::string>{"default","preserved"}:std::vector<std::string>{"default"})
		{VansAnimationGraphSetSetup set;set.definition.id=id;set.definition.name=id;set.definition.bindings.push_back({"base","pose",true});set.bindings.push_back({{"base","pose",true},graph()});sets.push_back(std::move(set));}
		VansGraphSetTransitionPolicy policy;policy.duration=duration;policy.requireStateMatch=false;
		if(!controller->SetAnimationGraphSets(std::move(layers),std::move(sets),"default",policy,{},error)||!controller->SetAnimationRig(rig,{},error))return std::unique_ptr<VansAnimationController>{};
		controller->SetNormalizeRootPose(false);controller->EnableRootMotion(true);return controller;};
	auto target=makeController(true),source=makeController(false);if(!Check(target && source,error.c_str()))return false;
	target->AddClip("preservedOnly",*target->GetClip("pose"));source->AddClip("sourceOnly",*source->GetClip("pose"));
	const auto* targetView=target.get();const auto* sourceView=source.get();
	VansAnimationNode node("Routing Fixture");node.SetSkeleton(skeleton);
	VansRetargetRuntimeDesc desc;desc.translationScaleMode=VansRetargetTranslationScaleMode::Explicit;desc.translationScale=2;desc.outputModelRotation=glm::angleAxis(glm::radians(90.f),glm::vec3(1,0,0));
	if(!node.SetController(std::move(target))||!node.ConfigureRetargetSource(skeleton,std::move(source),desc,error))return Check(false,error.c_str());
	if(!Check(node.FindClipByAlias("preservedOnly")&&node.FindClipByAlias("sourceOnly")&&!node.FindClipByAlias("missing")&&node.UsesRetargetSource(),
		"Cross-controller Clip lookup changed motion owner or hid a pending authored Clip"))return false;
	node.Play(VansAnimationEvaluationPurpose::EditorPreview);node.Update({VansAnimationEvaluationPurpose::EditorPreview,.25f});
	if(!Check(node.UsesRetargetSource() && node.GetCharacterMotionController()==sourceView,"Initial retarget graph owner is not source"))return false;
	if(!Check(glm::distance(node.GetRootMotionDelta(),glm::vec3(.5f,0,0))<1.e-4f,"Source root delta did not use compiled target units"))return false;
	Vans::VansCharacterTrajectory trajectory;node.PrepareCharacterMotionFrame(.25f,trajectory);node.Update({VansAnimationEvaluationPurpose::EditorPreview,.25f});
	if(!Check(std::abs(targetView->GetCachedGlobalTransforms()[0][3].x-1.f)<1.e-4f,
		"CCT-prepared source pose was stale or its clock advanced twice before retarget"))return false;
	if(!Check(node.SwitchGraphSet("preserved")==VansGraphSetSwitchResult::Completed && !node.UsesRetargetSource()
		&& node.GetCharacterMotionController()==targetView && node.GetActiveGraphSetId()=="preserved","Target-only preserved graph was not routed to target owner"))return false;
	node.Update({VansAnimationEvaluationPurpose::EditorPreview,.25f});
	if(!Check(node.SwitchGraphSet("unknown")==VansGraphSetSwitchResult::UnknownGraphSet && !node.UsesRetargetSource(),"Unknown graph changed controller owner"))return false;
	if(!Check(node.SwitchGraphSet("default")==VansGraphSetSwitchResult::AlreadyActive && node.UsesRetargetSource()
		&& node.GetCharacterMotionController()==sourceView,"Returning to source graph lost source owner"))return false;
	auto handoff=makeController(true,.2f);handoff->Play();handoff->Update(.4f,skeleton);
	const float beforeHandoff=handoff->GetCachedGlobalTransforms()[0][3].x;
	handoff->BeginDisplayedPoseHandoff(skeleton,"default","preserved");
	std::vector<glm::mat4> incoming{glm::translate(glm::mat4(1),glm::vec3(4,0,0))};
	if(!handoff->SubmitExternalModelPose(incoming,skeleton,.1f,VansExternalPoseEvaluationMode::TargetPostProcess,false))return false;
	const float displayed=handoff->GetCachedGlobalTransforms()[0][3].x;
	std::cout<<"DISPLAY_HANDOFF prior="<<beforeHandoff<<" output="<<displayed<<" expected="<<(beforeHandoff+4)*.5f<<'\n';
	if(!Check(std::abs(displayed-(beforeHandoff+4)*.5f)<1.e-4f,"Cross-owner handoff did not blend from target displayed pose"))return false;
	handoff->BeginDisplayedPoseHandoff(skeleton,"preserved","default");
	if(!handoff->SubmitExternalModelPose(incoming,skeleton,0,VansExternalPoseEvaluationMode::TargetPostProcess,false)
		|| !Check(std::abs(handoff->GetCachedGlobalTransforms()[0][3].x-displayed)<1.e-4f,"Interrupted owner handoff snapped to stale input"))return false;
	// 非角色骨名验证外部基础姿态与普通 Layer Stack 的 Mesh Space、动态加性、惯性和事件等价。
	Skeleton overlaySkeleton;AddBone(overlaySkeleton,"anchor",-1,{0,0,0});AddBone(overlaySkeleton,"segment",0,{0,1,0});overlaySkeleton.sourceSkeletonGuid=skeleton.sourceSkeletonGuid;overlaySkeleton.BuildTopologicalOrder();
	VansAnimationRigAsset overlayRigAsset;overlayRigAsset.name="Overlay fixture";overlayRigAsset.skeletonGuid=overlaySkeleton.sourceSkeletonGuid;
	VansCompiledAnimationRig overlayRig;
	if(!VansAnimationRigCompiler::Compile(overlayRigAsset,overlaySkeleton,overlayRig,error))return Check(false,error.c_str());
	for(bool dynamic:{false,true})
	{
		auto createOverlay=[&](bool external){
			auto c=std::make_unique<VansAnimationController>();c->AddParameter("time",AnimatorParamType::Float);c->AddParameter("weight",AnimatorParamType::Float);
			for(const std::string alias:{"seed","overlay"})
			{
				VansAnimationClip clip;clip.duration=2;clip.boneKeyframes.resize(2);
				clip.boneKeyframes[0]={{0,{0,0,0},glm::quat(1,0,0,0),{1,1,1}},{2,{2,0,0},glm::angleAxis(.8f,glm::vec3(0,1,0)),{1,1,1}}};
				clip.boneKeyframes[1]={{0,{0,1,0},glm::quat(1,0,0,0),{1,1,1}},{2,{.2f,1,.1f},glm::angleAxis(alias=="overlay"?1.2f:.3f,glm::vec3(1,0,0)),{1,1,1}}};
				if(alias=="overlay"){AnimationClipEvent e;e.id=16;e.time=.15f;e.name="SegmentPulse";clip.events.push_back(e);}c->AddClip(alias,clip);
			}
			auto graph=[&](bool overlay){auto g=std::make_unique<VansAnimGraph>();auto p=std::make_unique<AnimGraphClipNode>();p->m_ClipName=overlay?"overlay":"seed";p->m_Loop=false;p->m_RootMotion=false;if(!overlay)p->m_SampleTimeParameter="time";const int player=g->AddNode(std::move(p)),out=g->AddNode(std::make_unique<AnimGraphOutputNode>());g->AddLink(player,0,out,0);return g;};
			std::vector<VansAnimationLayerSetup> layers(2);auto& base=layers[0].definition;base.id="base";base.name="Base";base.kind=VansAnimationLayerKind::Base;base.updateWhenWeightIsZero=true;
			auto& layer=layers[1].definition;layer.id="overlay";layer.name="Overlay";layer.kind=VansAnimationLayerKind::Overlay;layer.useWeightParameter=true;layer.weightParameter="weight";
			layer.rotationSpace=VansRotationBlendSpace::Mesh;layer.dynamicAdditive=dynamic;layer.dynamicAdditiveWeight=.65f;
			layer.activationBlendInSeconds=.12f;layer.activationBlendOutSeconds=.16f;layer.activationCurve=VansLayerActivationCurve::SmoothStep;layer.restartOnActivation=true;
			layer.inertializationHalfLife=.1f;layer.inertializationMaxDuration=.28f;layer.updateWhenWeightIsZero=true;layer.rootMotion=VansLayerRootMotionMode::Ignore;
			auto mask=std::make_shared<VansBoneMaskAsset>();mask->id="segment-mask";mask->name="Segment";mask->defaultWeight=0;mask->explicitWeights["segment"]=1;layers[1].mask=*mask;
			std::vector<VansAnimationGraphSetSetup> sets;
			for(const std::string id:{"ordinary","external"}){VansAnimationGraphSetSetup set;set.definition.id=id;set.definition.name=id;
				for(int i=0;i<2;++i){VansAnimationGraphBindingDefinition binding{i?"overlay":"base",i?"overlay":"seed",true};set.definition.bindings.push_back(binding);set.bindings.push_back({binding,graph(i==1)});}sets.push_back(std::move(set));}
			VansGraphSetTransitionPolicy policy;policy.duration=0;
			if(!c->SetAnimationGraphSets(std::move(layers),std::move(sets),"ordinary",policy,{},error)||!c->SetAnimationRig(overlayRig,{},error))return std::unique_ptr<VansAnimationController>{};
			if(external&&!c->SetExternalPoseLayerGraphSet("external",error))return std::unique_ptr<VansAnimationController>{};
			c->SetNormalizeRootPose(false);c->EnableRootMotion(false);c->Play();return c;
		};
		auto expected=createOverlay(false),external=createOverlay(true);if(!expected||!external)return Check(false,error.c_str());
		int fullEvents=0,externalEvents=0;float maximum=0;
		for(int frame=0;frame<85;++frame)
		{
			const float time=frame/60.f,weight=frame>=5&&frame<47?1.f:0.f;
			expected->SetFloat("time",time);expected->SetFloat("weight",weight);external->SetFloat("weight",weight);
			if(frame==26){expected->RestartLayer("overlay");external->RestartLayer("overlay");}
			expected->Update(1.f/60,overlaySkeleton);
			VansAnimationSampleRequest request;request.loop=false;request.extractRootMotion=false;request.previousTime=request.currentTime=time;
			VansPosePayload input;VansAnimationFrameVector<glm::mat4> matrices;
			if(!VansAnimationSampler::Sample(*expected->GetClip("seed"),overlaySkeleton,request,input)||!VansPoseMath::BuildModelTransforms(input.localPose,overlaySkeleton,matrices))return false;
			const std::vector<glm::mat4> model(matrices.begin(),matrices.end());
			if(!external->SubmitExternalModelPose(model,overlaySkeleton,1.f/60,VansExternalPoseEvaluationMode::TargetPostProcess,false))return false;
			const auto& a=expected->GetCachedGlobalTransforms();const auto& b=external->GetCachedGlobalTransforms();
			for(std::size_t bone=0;bone<a.size();++bone)for(int col=0;col<4;++col)for(int row=0;row<4;++row)maximum=std::max(maximum,std::abs(a[bone][col][row]-b[bone][col][row]));
			fullEvents+=static_cast<int>(expected->GetSampledEvents().size());externalEvents+=static_cast<int>(external->GetSampledEvents().size());
		}
		if(!Check(maximum<1.e-5f && fullEvents==externalEvents && fullEvents>0 && external->GetActiveGraphSetId()=="ordinary",
			"External base layers changed existing mixer/activation/inertia/events or switched the active target graph"))return false;
	}
	std::cout<<"EXTERNAL_POSE_LAYERS_PASS mesh, dynamicAdditive, activation, inertia, restart, events, namedSet\n";
	std::cout<<"RETARGET_CONTROLLER_ROUTING_PASS source/target/source and root units\n";return true;
}

bool RunLeonUEPoseComparison()
{
	using namespace VansGraphics;
	auto workspace=std::filesystem::current_path();
	for(int depth=0;depth<6 && !std::filesystem::is_directory(workspace/"DemoHallProject");++depth)workspace=workspace.parent_path();
	const auto project=workspace/"DemoHallProject",task=project/"Tools/LeonSurvival";
	nlohmann::json oracle,input,sources,bakeManifest;
	std::ifstream(task/"Audit/ue_compressed_pose_oracle.json")>>oracle;
	std::ifstream(task/"runtime_input.json")>>input;
	std::ifstream(task/"Audit/selected_sources.json")>>sources;
	std::ifstream(task/"leon_bake.json")>>bakeManifest;
	Skeleton sourceSkeleton;std::string error;
	const auto sourcePath=task/"Inputs/Assets/Leon_Verified.fbx";Vans::VansAssetMeta sourceMeta;
	if (!Vans::VansAssetMetaStorage::Load(Vans::VansAssetMeta::MetaPathFor(sourcePath),sourceMeta,error)
		|| !VansSkinnedMeshLoader::LoadSkeletonFromModelAsset(sourcePath.string(),Vans::ReadSkeletalMeshImportSettings(sourceMeta),sourceSkeleton,error)) return Check(false,error.c_str());
	VansAnimationClip targetClip;Skeleton targetSkeleton;
	if (!VansAnimationClipIO::Load((project/input["aliases"]["Base_Idle"].get<std::string>()).string(),targetClip,targetSkeleton)) return false;
	VansAnimationRigAsset rigAsset;VansCompiledAnimationRig rig;
	if (!VansAnimationRigStorage::Load(project/"Assets/AnimationRigs/Survival_Leon.vanimrig",rigAsset,error)
		|| !VansAnimationRigCompiler::Compile(rigAsset,targetSkeleton,rig,error)) return Check(false,error.c_str());
	VansRetargetProfileAsset profile;
	if (!VansRetargetProfileStorage::Load(project/"Assets/Retarget/Leon_To_Survival.vretarget",profile,error)) return Check(false,error.c_str());
	VansRetargetRuntimeDesc desc;desc.translationScaleMode=profile.translationScaleMode;desc.translationScale=profile.explicitTranslationScale;
	desc.rootAlignment=profile.rootAlignment;desc.targetModelSpaceAlignment=profile.targetModelSpaceAlignment;
	desc.limbChains=profile.limbChains;desc.controlBones=profile.controlBones;desc.boneMappings=profile.boneMappings;desc.outputModelRotation=profile.outputModelRotation;
	VansRetargetProcessor processor;
	if (!processor.Build(sourceSkeleton,targetSkeleton,rig,desc)) return Check(false,"UE pose comparison could not bind retarget profile");
	std::unordered_map<std::string,std::size_t> ueIndices;
	for (std::size_t index=0;index<oracle["bones"].size();++index) ueIndices.emplace(oracle["bones"][index].get<std::string>(),index);
	// UE 左手坐标到原始 FBX 右手坐标：位置 [-Y,Z,X]，旋转伪向量 [Y,-Z,-X]。
	auto convert=[](const nlohmann::json& values){
		VansBoneTransform transform;transform.translation={-values[1].get<float>(),values[2].get<float>(),values[0].get<float>()};
		transform.rotation=glm::normalize(glm::quat(values[6].get<float>(),values[4].get<float>(),-values[5].get<float>(),-values[3].get<float>()));
		transform.scale={values[7].get<float>(),values[8].get<float>(),values[9].get<float>()};return transform;};
	nlohmann::json result={{"producer","Independent UE compressed pose -> one engine retarget versus current standalone baked VCLIP"},{"clips",nlohmann::json::array()}};
	float maximumPosition=0,maximumAngle=0;int samples=0;
	std::unordered_map<std::string,std::vector<VansAnimationClip>> rawClips;
	for (const auto& entry:oracle["clips"])
	{
		const auto alias=entry["alias"].get<std::string>();
		const auto selected=std::find_if(sources.begin(),sources.end(),[&](const auto& row){return row["alias"]==alias;});
		if (selected==sources.end() || !(*selected)["deltaReference"].is_null()) continue;
		VansAnimationClip baked;Skeleton bakedSkeleton;
		if (!VansAnimationClipIO::Load((project/input["aliases"][alias].get<std::string>()).string(),baked,bakedSkeleton)) return false;
		const auto job=std::find_if(bakeManifest["clips"].begin(),bakeManifest["clips"].end(),[&](const auto& row){return row["output"]==input["aliases"][alias];});
		const auto rawPath=(*job)["source"].get<std::string>();
		if (!rawClips.count(rawPath) && !VansSkinnedMeshLoader::ExtractExternAnimationClips((project/rawPath).string(),sourceSkeleton,rawClips[rawPath])) return false;
		const auto rawClip=std::find_if(rawClips[rawPath].begin(),rawClips[rawPath].end(),[&](const auto& clip){return clip.clipName==(*job)["take"].get<std::string>();});
		if (rawClip==rawClips[rawPath].end()) return false;
		float clipPosition=0,clipAngle=0,rawPosition=0,rawAngle=0;std::string worstBone,rawWorstBone;
		nlohmann::json details=nlohmann::json::array();
		for (const auto& frame:entry["samples"])
		{
			std::vector<VansBoneTransform> sourcePose(sourceSkeleton.bones.size());
			for (std::size_t bone=0;bone<sourcePose.size();++bone)
			{
				const auto& name=sourceSkeleton.bones[bone].name;sourcePose[bone]=convert(frame["local"][ueIndices.at(name)]);
				if (name=="root" || name=="Null_Offset")
				{
					VansBoneTransform rest;if (!VansPoseMath::TryDecompose(sourceSkeleton.bones[bone].localTransform,rest)) return false;
					sourcePose[bone].translation=rest.translation;sourcePose[bone].rotation=rest.rotation;
					if (name=="root") sourcePose[bone].scale=rest.scale;
				}
			}
			std::vector<glm::mat4> sourceModel,expectedModel;
			VansAnimationSampleRequest rawRequest;rawRequest.loop=false;rawRequest.extractRootMotion=false;rawRequest.previousTime=rawRequest.currentTime=frame["time"].get<float>();
			VansPosePayload rawPose;if (!VansAnimationSampler::Sample(*rawClip,sourceSkeleton,rawRequest,rawPose)) return false;
			for (const auto& mapping:desc.boneMappings)
			{
				const int bone=sourceSkeleton.FindBoneIndex(mapping.sourceBone);if(mapping.sourceBone=="root" || mapping.sourceBone=="Null_Offset")continue;
				const float position=glm::distance(rawPose.localPose[bone].translation,sourcePose[bone].translation);
				const float angle=glm::degrees(2*std::acos(std::clamp(std::abs(glm::dot(rawPose.localPose[bone].rotation,sourcePose[bone].rotation)),0.f,1.f)));
				if(angle>rawAngle){rawAngle=angle;rawWorstBone=mapping.sourceBone;}rawPosition=std::max(rawPosition,position);
			}
			if (alias=="WP_AimWalkBR")
			{
				nlohmann::json diagnostic={{"time",frame["time"]},{"bones",nlohmann::json::object()}};
				for (const std::string name:{"root","Null_Offset","Spine_2","R_UpperArm","R_Forearm","R_Hand"})
				{
					const int bone=sourceSkeleton.FindBoneIndex(name);const auto& raw=rawPose.localPose[bone];const auto& expected=sourcePose[bone];
					diagnostic["bones"][name]={{"rawPosition",{raw.translation.x,raw.translation.y,raw.translation.z}},{"uePosition",{expected.translation.x,expected.translation.y,expected.translation.z}},
						{"rawQuaternion",{raw.rotation.x,raw.rotation.y,raw.rotation.z,raw.rotation.w}},{"ueQuaternion",{expected.rotation.x,expected.rotation.y,expected.rotation.z,expected.rotation.w}}};
				}
				details.push_back(std::move(diagnostic));
			}
			if (!VansPoseMath::BuildModelTransforms(sourcePose,sourceSkeleton,sourceModel)
				|| !processor.Process(sourceModel,sourceSkeleton,targetSkeleton,expectedModel)) return false;
			VansAnimationSampleRequest request;request.loop=false;request.extractRootMotion=false;request.previousTime=request.currentTime=frame["time"].get<float>();
			VansPosePayload sampled;VansAnimationFrameVector<glm::mat4> actualModel;
			if (!VansAnimationSampler::Sample(baked,targetSkeleton,request,sampled)
				|| !VansPoseMath::BuildModelTransforms(sampled.localPose,targetSkeleton,actualModel)) return false;
			for (const auto& mapping:desc.boneMappings)
			{
				const int bone=targetSkeleton.FindBoneIndex(mapping.targetBone);
				VansBoneTransform actual,expected;
				if (!VansPoseMath::TryDecompose(actualModel[bone],actual) || !VansPoseMath::TryDecompose(expectedModel[bone],expected)) return false;
				const float position=glm::distance(actual.translation,expected.translation);
				const float angle=glm::degrees(2*std::acos(std::clamp(std::abs(glm::dot(actual.rotation,expected.rotation)),0.f,1.f)));
				if (angle>clipAngle) {clipAngle=angle;worstBone=mapping.targetBone;}
				clipPosition=std::max(clipPosition,position);
			}
			++samples;
		}
		result["clips"].push_back({{"alias",alias},{"maxPositionCm",clipPosition},{"maxRotationDegrees",clipAngle},{"worstRotationBone",worstBone},
			{"sourcePositionMeters",rawPosition},{"sourceRotationDegrees",rawAngle},{"sourceWorstRotationBone",rawWorstBone},{"details",details}});
		maximumPosition=std::max(maximumPosition,clipPosition);maximumAngle=std::max(maximumAngle,clipAngle);
	}
	result["samples"]=samples;result["maxPositionCm"]=maximumPosition;result["maxRotationDegrees"]=maximumAngle;
	result["passed"]=maximumPosition<.05f && maximumAngle<.2f;
	std::ofstream(task/"Audit/ue_retarget_pose_comparison.json")<<result.dump(2);
	std::cout<<"LEON_UE_RETARGET_COMPARISON samples="<<samples<<" positionCm="<<maximumPosition<<" angle="<<maximumAngle<<"\n";
	return result["passed"].get<bool>();
}

bool RunLeonSurvivalProjectContract()
{
	using namespace VansGraphics;
	auto workspace=std::filesystem::current_path();
	for(int depth=0;depth<6 && !std::filesystem::is_directory(workspace/"DemoHallProject");++depth)workspace=workspace.parent_path();
	const auto project=workspace/"DemoHallProject";
	AnimatorAssetData asset;std::string error;
	if(!Check(VansAnimatorIO::Load((project/"Assets/Animations/LeonSurvival/Survival_Leon.vanimator").string(),asset),"Leon Animator failed to load"))return false;
	nlohmann::json authored,restored;
	if (!VansAnimatorIO::SerializeToJsonObject(asset,authored,error)) return Check(false,error.c_str());
	const auto dto=Vans::EditorAPI::AnimationAuthoringBridge::DecodeAnimator(authored.dump());
	if (!Check(dto.success,"Leon Animator native-to-DTO failed")) return false;
	const auto encoded=Vans::EditorAPI::AnimationAuthoringBridge::EncodeAnimator(*dto.document);
	if (!Check(encoded.success,"Leon Animator DTO-to-native failed")) return false;
	restored=nlohmann::json::parse(encoded.canonicalJson);
	if (!Check(restored==authored,"Leon Animator geometry or composition policy was lost in DTO roundtrip")) return false;
	std::unordered_map<std::string,std::shared_ptr<const VansAnimationClipAsset>> clips;
	for(const auto& ref:asset.clipRefs)
	{
		if(clips.count(ref.assetGuid))continue;
		auto clip=std::make_shared<VansAnimationClipAsset>();
		if(!Check(VansAnimationClipIO::Load((project/ref.pathHint).string(),clip->clip,clip->skeleton),ref.pathHint.c_str()))return false;
		clips.emplace(ref.assetGuid,std::move(clip));
	}
	const auto skeleton=clips.at(asset.clipRefs[0].assetGuid)->skeleton;
	if(!Check(skeleton.bones.size()==161,"Leon output is not the Survival skeleton"))return false;
	int clipSamples=0;
	for(const auto& entry:clips)
	{
		const auto& source=*entry.second;
		if(!Check(skeleton.MatchesAnimationLayout(source.skeleton,&error),error.c_str()))return false;
		for(int frame=0;frame<=20;++frame)
		{
			VansAnimationSampleRequest request;request.loop=false;request.extractRootMotion=false;
			request.previousTime=request.currentTime=source.clip.duration*frame/20;
			VansPosePayload pose;VansAnimationFrameVector<glm::mat4> model;
			if(!Check(VansAnimationSampler::Sample(source.clip,skeleton,request,pose)
				&& pose.valid && pose.localPose.size()==skeleton.bones.size()
				&& VansPoseMath::BuildModelTransforms(pose.localPose,skeleton,model),
				"Leon clip failed full target skeleton sampling"))return false;
			for(const auto& matrix:model)for(int column=0;column<4;++column)for(int row=0;row<4;++row)
				if(!Check(std::isfinite(matrix[column][row]),"Leon clip contains a non-finite target bone"))return false;
			++clipSamples;
		}
	}
	auto rig=std::make_shared<VansAnimationRigAsset>();
	if(!VansAnimationRigStorage::Load(project/"Assets/AnimationRigs/Survival_Leon.vanimrig",*rig,error))return Check(false,error.c_str());
	VansAnimatorRuntimeCompileOptions options;options.normalizeRootPose=false;options.enableRootMotion=true;
	options.rigResolver=[rig](const auto&,auto&){return rig;};
	options.queryProfileResolver=[](const auto& profile,std::uint32_t& mask,auto&){mask=1;return profile=="characterGround";};
	auto resolveClip=[&](const auto& ref,auto& out,auto&){out=clips.at(ref.assetGuid);return true;};
	auto resolveMask=[&](const auto& layer,auto& out,auto& message){
		if(layer.maskGuid.empty()){out.reset();return true;}
		auto mask=std::make_shared<VansBoneMaskAsset>();
		if(!VansBoneMaskStorage::Load(project/layer.maskPathHint,*mask,message))return false;
		out=std::move(mask);return true;};
	auto controller=VansAnimatorRuntimeCompiler::Compile(asset,skeleton,resolveClip,resolveMask,options,error);
	if(!Check(controller!=nullptr,error.c_str()))return false;
	controller->Play();controller->SetFloat("LeonBaseFade",1);
	// 保留的目标烘焙试作按自身图中的选择器验证，不能混入后来源图新增的动作索引。
	nlohmann::json data={{"baseIndex",nlohmann::json::object()}};
	for(const auto& definition:asset.graphs)if(definition.graph)
		for(const auto& [id,node]:definition.graph->GetNodes())
			if(node->GetType()==VansAnimGraphNodeType::Switch && static_cast<const AnimGraphSwitchNode*>(node.get())->m_ParamName=="LeonBaseA")
				for(const auto& link:definition.graph->GetLinks())if(link.toNodeId==id)
				{
					const auto* clip=definition.graph->GetNode(link.fromNodeId);
					if(clip && clip->GetType()==VansAnimGraphNodeType::Clip)
						data["baseIndex"][static_cast<const AnimGraphClipNode*>(clip)->m_ClipName]=link.toPinIndex;
				}
	if(!Check(!data["baseIndex"].empty(),"Preserved prototype lost its authored base selectors"))return false;
	// 瞄准后的后坐必须保留：用独立采样与矩阵组合验证实际 Controller 的阶段顺序。
	controller->SetInt("LeonBaseA",data["baseIndex"]["Base_Idle"].get<int>());
	controller->SetInt("LeonBaseB",data["baseIndex"]["Base_Idle"].get<int>());
	controller->SetFloat("PistolAimWeight",1);controller->SetVector3("LeonAimTarget",glm::vec3(2200,-8000,3200));
	controller->SetFloat("LeonShotWeight",0);controller->Update(1.f/60,skeleton);
	const auto aimModel = controller->GetCachedGlobalTransforms();
	std::vector<glm::mat4> aimLocal;
	VansPosePayload aimPose,shotPose,referencePose;
	if (!VansPoseMath::BuildLocalTransforms(aimModel,skeleton,aimLocal)
		|| !VansPoseMath::FromMatrices(aimLocal,aimPose.localPose)) return false;
	auto clipByAlias=[&](const std::string& alias)->const VansAnimationClip&{
		const auto ref=std::find_if(asset.clipRefs.begin(),asset.clipRefs.end(),[&](const auto& value){return value.name==alias;});
		return clips.at(ref->assetGuid)->clip;};
	VansAnimationSampleRequest aimRequest;aimRequest.loop=false;aimRequest.extractRootMotion=false;
	VansAnimationSampler::Sample(clipByAlias("WP_AimLoop"),skeleton,aimRequest,referencePose);
	aimRequest.previousTime=aimRequest.currentTime=.08f;
	VansAnimationSampler::Sample(clipByAlias("Pistol_Shot"),skeleton,aimRequest,shotPose);
	if (!VansPoseMath::MakeAdditiveDeltaPose(shotPose.localPose,referencePose.localPose,skeleton,VansAdditivePoseMode::LocalSphericalLeft)) return false;
	VansAnimationFrameVector<VansBoneTransform> expectedPose;
	if (!VansPoseMath::ApplyAdditiveDeltaPose(aimPose.localPose,shotPose.localPose,1,skeleton,expectedPose,VansAdditivePoseMode::LocalSphericalLeft)) return false;
	VansAnimationFrameVector<glm::mat4> expectedModel;
	if (!VansPoseMath::BuildModelTransforms(expectedPose,skeleton,expectedModel)) return false;
	controller->SetFloat("LeonShotWeight",1);controller->SetFloat("LeonShotTime",.08f);controller->SetInt("LeonShotEpoch",1);
	controller->Update(1.f/60,skeleton);
	const auto& shotModel=controller->GetCachedGlobalTransforms();
	float orderError=0;
	for (std::size_t bone=0;bone<shotModel.size();++bone) for(int column=0;column<4;++column)for(int row=0;row<4;++row)
		orderError=std::max(orderError,std::abs(shotModel[bone][column][row]-expectedModel[bone][column][row]));
	if (!Check(orderError<.001f,"Recoil was applied before geometric aim or used the wrong additive product")) return false;
	controller->SetFloat("PistolAimWeight",0);controller->SetFloat("LeonShotWeight",0);controller->SetFloat("LeonShotTime",0);controller->SetInt("LeonShotEpoch",2);
	const auto ownerWorld=glm::translate(glm::mat4(1),glm::vec3(7,2,-4))*glm::toMat4(glm::angleAxis(glm::radians(67.f),glm::vec3(0,1,0)))*glm::scale(glm::mat4(1),glm::vec3(.01f));
	controller->SetOwnerWorldTransform(ownerWorld);controller->SetFloat("PistolAimWeight",1);const glm::vec3 worldTarget(8,4,-15);controller->SetVector3("LeonAimTarget",worldTarget);controller->Update(1.f/60,skeleton);
	nlohmann::json geometry;std::ifstream(project/"Tools/LeonSurvival/Audit/pistol_aim_geometry.json")>>geometry;
	const auto& weaponModel=controller->GetCachedGlobalTransforms()[skeleton.FindBoneIndex("weapon_r")];
	const glm::vec3 muzzleLocal(geometry["muzzleLocal"][0].get<float>(),geometry["muzzleLocal"][1].get<float>(),geometry["muzzleLocal"][2].get<float>());
	const glm::vec3 barrelLocal(geometry["barrelLocal"][0].get<float>(),geometry["barrelLocal"][1].get<float>(),geometry["barrelLocal"][2].get<float>());
	const auto muzzleWorld=glm::vec3(ownerWorld*weaponModel*glm::vec4(muzzleLocal,1));
	const auto barrelWorld=glm::normalize(glm::vec3(ownerWorld*weaponModel*glm::vec4(barrelLocal,0)));
	const float worldAimDot=glm::dot(barrelWorld,glm::normalize(worldTarget-muzzleWorld));
	std::cout<<"LEON_WORLD_AIM dot="<<worldAimDot<<" muzzle="<<muzzleWorld.x<<","<<muzzleWorld.y<<","<<muzzleWorld.z<<" barrel="<<barrelWorld.x<<","<<barrelWorld.y<<","<<barrelWorld.z<<"\n";
	if (!Check(worldAimDot>.999999f,"World aim target ignored current actor translation, rotation or scale")) return false;
	controller->SetOwnerWorldTransform(glm::mat4(1));controller->SetFloat("PistolAimWeight",0);
	// A sparse CarryJog track must leave the source's untracked left arm on the live gait.
	controller->SetInt("LeonBaseA",data["baseIndex"]["Base_Jog"].get<int>());
	controller->SetInt("LeonBaseB",data["baseIndex"]["Base_Jog"].get<int>());
	for(float time:{.37f,.74f,1.11f,1.48f})
	{
		controller->SetFloat("LeonCarryJogWeight",0);controller->SetFloat("LeonCarryTime",time);
		controller->SetFloat("LeonBaseTimeA",time);controller->SetFloat("LeonBaseTimeB",time);
		for(int frame=0;frame<24;++frame)controller->Update(1.f/60,skeleton);
		const auto before=controller->GetCachedGlobalTransforms();
		controller->SetFloat("LeonCarryJogWeight",1);
		for(int frame=0;frame<24;++frame)controller->Update(1.f/60,skeleton);
		const auto& after=controller->GetCachedGlobalTransforms();
		for(const char* name:{"clavicle_l","upperarm_l","lowerarm_l","hand_l"})
		{
			const auto index=skeleton.FindBoneIndex(name);float difference=0;
			for(int column=0;column<4;++column)for(int row=0;row<4;++row)
				difference=std::max(difference,std::abs(before[index][column][row]-after[index][column][row]));
			if(!Check(difference<.0001f,"CarryJog overwrote the untracked live left arm"))return false;
		}
	}
	controller->SetFloat("LeonCarryJogWeight",0);
	int samples=0;
	for(const auto& item:data["baseIndex"].items())
	{
		const auto name=item.key();const int selector=item.value().get<int>();
		controller->RestartLayer("layer-base");controller->SetInt("LeonBaseA",selector);controller->SetInt("LeonBaseB",selector);
		controller->SetBool("LeonBaseLoop",false);
		const auto* clip=controller->GetClip(name);if(!clip)return Check(false,name.c_str());
		for(int frame=0;frame<=20;++frame)
		{
			const float time=clip->duration*frame/20;controller->SetFloat("LeonBaseTimeA",time);controller->SetFloat("LeonBaseTimeB",time);
			controller->Update(1.f/60,skeleton);
			const auto& model=controller->GetCachedGlobalTransforms();
			if(!Check(model.size()==skeleton.bones.size(),"Leon graph did not output the full target pose"))return false;
			for(const auto& m:model)for(int c=0;c<4;++c)for(int r=0;r<4;++r)
				if(!Check(std::isfinite(m[c][r]),"Leon graph produced a non-finite bone matrix"))return false;
			const int pelvis=skeleton.FindBoneIndex("pelvis"),head=skeleton.FindBoneIndex("head");
			if(!Check(model[head][3].z>model[pelvis][3].z,"Leon graph lost Z-up root coordinates"))return false;
			++samples;
		}
	}
	controller->SetInt("LeonBaseA",data["baseIndex"]["WP_AimLoop"].get<int>());controller->SetInt("LeonBaseB",data["baseIndex"]["WP_AimLoop"].get<int>());
	controller->SetFloat("LeonBaseTimeA",0);controller->SetFloat("LeonBaseTimeB",0);
	controller->SetFloat("LeonShotWeight",1);controller->RestartLayer("layer-leon-shot");
	int events=0;
	for(int frame=0;frame<=80;++frame)
	{
		controller->SetFloat("LeonShotTime",std::min(frame/60.f,.8333333f));controller->Update(1.f/60,skeleton);
		for(const auto& event:controller->GetSampledEvents())if(event.name=="Pistol_Shot.Finished")
		{
			if(!Check(event.clipId==controller->GetClip("Pistol_Shot")->stableId && event.weight>=.5f,
				"Leon Shot signal does not match the existing GAF subscription identity/weight"))return false;
			++events;
		}
	}
	if(!Check(events==1,"Leon Shot did not publish exactly one accepted completion signal"))return false;
	controller->SetFloat("LeonShotWeight",0);
	controller->RestartLayer("layer-leon-upper-Equip-stand");controller->RestartLayer("layer-leon-upper-Equip-moving");
	const float equipDuration=controller->GetClip("WP_Equip")->duration;int equipEvents=0;
	for(int frame=0;frame<54;++frame)
	{
		const float time=std::min(frame/60.f,equipDuration);const bool final=time>=equipDuration;
		controller->SetFloat("LeonWeaponTime",time);
		controller->SetFloat("LeonUpper_Equip_stand",final?0:1);controller->SetFloat("LeonUpper_Equip_moving",final?1:0);
		controller->Update(1.f/60,skeleton);
		for(const auto& event:controller->GetSampledEvents())if(event.name=="Leon.Draw.Finished")++equipEvents;
	}
	if(!Check(equipEvents==1,"Changing movement at the last weapon frame lost/duplicated the completion signal"))return false;
	controller->SetFloat("LeonUpper_Equip_moving",0);
	for(const auto& set:asset.graphSets)
	{
		const auto result=controller->SwitchGraphSet(set.id);
		if(result==VansGraphSetSwitchResult::UnknownGraphSet || result==VansGraphSetSwitchResult::Rejected
			|| result==VansGraphSetSwitchResult::StateHandoffFailed)return Check(false,set.id.c_str());
		for(int frame=0;frame<30;++frame)controller->Update(1.f/60,skeleton);
	}
	std::cout<<"LEON_SURVIVAL_GRAPH_PASS samples="<<samples<<" clipSamples="<<clipSamples<<" clips="<<clips.size()<<" shotSignals="<<events<<" graphSets="<<asset.graphSets.size()<<'\n';
	return true;
}

bool RunLeonUESourcePoseComparison()
{
	using namespace VansGraphics;
	auto workspace=std::filesystem::current_path();
	for(int depth=0;depth<6 && !std::filesystem::is_directory(workspace/"DemoHallProject");++depth)workspace=workspace.parent_path();
	const auto project=workspace/"DemoHallProject",task=project/"Tools/LeonSurvival";
	nlohmann::json oracle;
	std::ifstream(task/"Audit/ue_canonical_clip_oracle.json")>>oracle;
	nlohmann::json output={{"clips",nlohmann::json::array()},{"producer","Independent UE compressed samples versus formally imported source VCLIP"}};
	float maxPosition=0,maxAngle=0,maxScale=0;std::size_t samples=0,deltaSamples=0;
	for(const auto& row:oracle["clips"])
	{
		const auto alias=row["alias"].get<std::string>();
		VansAnimationClip clip;Skeleton skeleton;
		if(!VansAnimationClipIO::Load((project/"Assets/AnimationSources/Leon/Clips"/(alias+".vclip")).string(),clip,skeleton))return false;
		if(!Check(skeleton.bones.size()==415,"Source pose clip did not retain canonical source skeleton"))return false;
		float clipPosition=0,clipAngle=0,clipScale=0;std::string worst;
		for(const auto& sample:row["samples"])
		{
			VansAnimationSampleRequest request;request.loop=false;request.extractRootMotion=false;request.previousTime=request.currentTime=sample["time"].get<float>();
			VansPosePayload pose;if(!VansAnimationSampler::Sample(clip,skeleton,request,pose))return false;
			for(const auto& entry:sample["local"].items())
			{
				const auto name=entry.key();const auto& values=entry.value();
				const glm::vec3 position(values[0].get<float>(),values[1].get<float>(),values[2].get<float>());
				const auto rotation=glm::normalize(glm::quat(values[6].get<float>(),values[3].get<float>(),values[4].get<float>(),values[5].get<float>()));
				const auto& actual=pose.localPose[skeleton.FindBoneIndex(name)];
				clipPosition=std::max(clipPosition,glm::distance(actual.translation,position));
				clipScale=std::max(clipScale,glm::distance(actual.scale,glm::vec3(values[7].get<float>(),values[8].get<float>(),values[9].get<float>())));
				const float angle=glm::degrees(2*std::acos(std::clamp(std::abs(glm::dot(actual.rotation,rotation)),0.f,1.f)));
				if(angle>clipAngle){clipAngle=angle;worst=name;}++samples;if(row["delta"].get<bool>())++deltaSamples;
			}
		}
		output["clips"].push_back({{"alias",alias},{"delta",row["delta"]},{"maxPositionMeters",clipPosition},{"maxRotationDegrees",clipAngle},{"maxScaleError",clipScale},{"worstBone",worst}});
		maxPosition=std::max(maxPosition,clipPosition);maxAngle=std::max(maxAngle,clipAngle);maxScale=std::max(maxScale,clipScale);
	}
	output["samples"]=samples;output["deltaSamples"]=deltaSamples;output["maxPositionMeters"]=maxPosition;output["maxRotationDegrees"]=maxAngle;output["maxScaleError"]=maxScale;
	output["passed"]=maxPosition<.0001f && maxAngle<.15f && maxScale<.0001f;
	std::ofstream(task/"Audit/ue_source_pose_comparison.json")<<output.dump(2);
	std::cout<<"LEON_UE_SOURCE_COMPARISON samples="<<samples<<" positionMeters="<<maxPosition<<" angle="<<maxAngle<<'\n';
	return output["passed"].get<bool>();
}

bool RunProceduralLimbIKContractTests()
{
	return TestRigValidationAndLimbSolver() && TestTransformTargetCheckpoint();
}
