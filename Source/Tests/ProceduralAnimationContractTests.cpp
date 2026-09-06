#include "ProceduralAnimationContractTests.h"

#include "../EngineCore/AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../EngineCore/AnimationCore/Procedural/Grounding/VansGroundingRuntime.h"
#include "../EngineCore/AnimationCore/Procedural/Solvers/VansAimConstraintSolver.h"
#include "../EngineCore/AnimationCore/Procedural/Solvers/VansChainIKSolver.h"
#include "../EngineCore/AnimationCore/Procedural/Solvers/VansLimbIKSolver.h"
#include "../EngineCore/AnimationCore/Storage/VansAnimationRigStorage.h"
#include "../EngineCore/AnimationCore/Serialization/VansRetargetProfileJsonCodec.h"
#include "../EngineCore/AnimationCore/VansAnimationController.h"
#include "../EngineCore/AnimationCore/VansPoseMath.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EngineCore/AssetCore/VansAssetDocument.h"
#include "../EngineCore/SceneCore/VansSceneAnimationComponentReader.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	using namespace VansGraphics;

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

		workspace.Initialize(fixture.skeleton, fixture.localPose);
		VansProceduralGoal partialGoal = reachable;
		partialGoal.positionWeight = 0.35f;
		const VansProceduralSolverResult partialResult = VansLimbIKSolver::Solve(
			workspace, fixture.rig, chain, partialGoal);
		if (!Check(partialResult.status == VansProceduralSolverStatus::Solved
			&& partialResult.effectivePositionError > 1.0e-3f,
			"Limb IK misclassified an authored partial weight as a solver clamp")) return false;

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
		VansProceduralGoal aimGoal;
		aimGoal.valid = true;
		aimGoal.positionModel = glm::vec3(10.0f, 0.0f, 1.0f);
		VansAimConstraintSettings aimSettings;
		aimSettings.yawLimitDegrees = glm::vec2(-30.0f, 30.0f);
		aimSettings.pitchLimitDegrees = glm::vec2(-20.0f, 20.0f);
		aimSettings.maxAngularSpeedDegrees = 1000.0f;
		const VansProceduralSolverResult aimResult = VansAimConstraintSolver::Solve(
			aimWorkspace, aimRig, aimRig.chains.front(), aimGoal, 1.0f, aimSettings);
		if (!Check(aimResult.status == VansProceduralSolverStatus::Clamped
			&& HasLimit(aimResult, VansProceduralLimitReason::Joint)
			&& aimWorkspace.IsFinite(), "Aim constraint did not clamp a target outside yaw limits"))
			return false;

		aimWorkspace.Initialize(aimSkeleton, BuildLocalPose(aimSkeleton));
		aimSettings.yawLimitDegrees = glm::vec2(-180.0f, 180.0f);
		aimSettings.pitchLimitDegrees = glm::vec2(-89.0f, 89.0f);
		aimSettings.maxAngularSpeedDegrees = 1000.0f;
		const VansProceduralSolverResult frameResult = VansAimConstraintSolver::Solve(
			aimWorkspace, aimRig, aimRig.chains.front(), aimGoal, 1.0f, aimSettings);
		const glm::quat frameRotation = aimWorkspace.GetComponentRotation(
			aimRig.chains.front().boneIndices.back());
		const glm::vec3 finalAimOrigin = aimWorkspace.GetComponentPosition(
			aimRig.chains.front().boneIndices.back());
		const glm::vec3 finalAimDirection = glm::normalize(aimGoal.positionModel - finalAimOrigin);
		if (!Check(frameResult.status == VansProceduralSolverStatus::Solved
			&& glm::dot(glm::normalize(frameRotation * aimRig.chains.front().forwardAxisLocal),
				finalAimDirection) > 0.999f
			&& glm::dot(glm::normalize(frameRotation * aimRig.chains.front().upAxisLocal),
				glm::vec3(0.0f, 1.0f, 0.0f)) > 0.999f,
			"Aim constraint did not recompute from the moved tip or introduced roll")) return false;

		aimWorkspace.Initialize(aimSkeleton, BuildLocalPose(aimSkeleton));
		aimSettings.maxAngularSpeedDegrees = 30.0f;
		const VansProceduralSolverResult speedResult = VansAimConstraintSolver::Solve(
			aimWorkspace, aimRig, aimRig.chains.front(), aimGoal, 1.0f / 60.0f, aimSettings);
		const float appliedAngle = VansQuaternionAngleDegrees(
			aimWorkspace.GetComponentRotation(aimRig.chains.front().boneIndices.back()));
		return Check(speedResult.status == VansProceduralSolverStatus::Clamped
			&& HasLimit(speedResult, VansProceduralLimitReason::AngularSpeed)
			&& appliedAngle <= 0.501f,
			"Aim constraint exceeded its chain-wide angular-speed limit");
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
			if (config->motionMatching)
			{
				VansAnimationController controller;
				if (!Check(controller.ConfigureMotionMatching(*config->motionMatching, error),
					error.c_str())) return false;
				if (config->motionMatching->enabled && config->name != "Belica")
				{
					if (!Check(config->motionMatching->contactProvider == "locomotion"
						&& config->motionMatching->contactChannels.size() == 2,
						"Motion Matching character does not publish both canonical foot contacts"))
						return false;
				}
			}
		}
		return Check(found.size() == expectedRigs.size(),
			"Project scene is missing a character required by the procedural-animation configuration");
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
	return TestGroundingPlanesAndAirborne()
		&& TestGroundingContactWeightingAndStaticSeams()
		&& TestGroundingMovingSupport()
		&& TestJointConstraintMath()
		&& TestRigValidationAndLimbSolver()
		&& TestChainAndAimSolvers()
		&& TestStrictRetargetAndContactConfiguration()
		&& TestProjectSceneProceduralConfiguration();
}
