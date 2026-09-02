#include "NavigationAIContractTests.h"

#include "../EngineCore/AICore/VansAIBlackboard.h"
#include "../EngineCore/AICore/VansAIPerception.h"
#include "../EngineCore/AICore/VansAIRuntimeComponents.h"
#include "../EngineCore/AssetCore/VansAssetDatabase.h"
#include "../EngineCore/NavigationCore/VansNavigationMesh.h"
#include "../EngineCore/NavigationCore/VansSceneNavigationGeometry.h"
#include "../EngineCore/RuntimeCore/VansCharacterTrajectoryGenerator.h"
#include "../EngineCore/RuntimeCore/VansCharacterMotion.h"
#include "../EngineCore/SceneCore/VansSceneObjectBuildPlan.h"
#include "../EngineCore/SceneCore/VansSceneRuntimeProjection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace
{
void AppendEnvironmentBox(Vans::VansSceneObjectBuildPlan& plan,
	const char* name,
	const std::array<float, 3>& position,
	const std::array<float, 3>& extents)
{
	Vans::VansSceneObjectBuildConfig object;
	object.name = name;
	object.transform = Vans::VansSceneTransformConfig{};
	object.transform->position = position;
	Vans::VansScenePhysicsNodeConfig physics;
	physics.enabled = true;
	physics.bodyType = "static";
	physics.colliderType = "box";
	physics.boxExtents = extents;
	physics.layer = "Environment";
	physics.isTrigger = false;
	object.physicsComponents.physics = physics;
	plan.objects.push_back(std::move(object));
}

bool Expect(bool condition, const char* message)
{
	if (condition) return true;
	std::cerr << "[NavigationAIContractTests] " << message << '\n';
	return false;
}
}

bool RunNavigationAIContractTests()
{
	using namespace Vans;
	if (!Expect(VansAssetDatabase::Classify("test.vnavmesh") ==
		VansAssetType::NavigationMesh, "Navigation asset classification failed") ||
		!Expect(VansAssetDatabase::Classify("test.vaibehavior") ==
			VansAssetType::AIBehavior, "AI Behavior asset classification failed") ||
		!Expect(VansAssetDatabase::ParseSerializedType("navigationMesh") ==
			VansAssetType::NavigationMesh, "Navigation serialized type failed") ||
		!Expect(VansAssetDatabase::ParseSerializedType("aiBehavior") ==
			VansAssetType::AIBehavior, "AI Behavior serialized type failed"))
	{
		return false;
	}
	const float correctedNonstandardYaw = ResolveModelOwnerFacingYaw(
		35.0f, glm::vec3(-1.0f, 0.0f, 0.0f));
	const glm::vec3 correctedNonstandardForward = glm::angleAxis(
		glm::radians(correctedNonstandardYaw), glm::vec3(0.0f, 1.0f, 0.0f)) *
		glm::vec3(-1.0f, 0.0f, 0.0f);
	const glm::vec3 desiredForward = glm::angleAxis(
		glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
		glm::vec3(0.0f, 0.0f, 1.0f);
	const std::array<float, 3> projectedWhisperRotation =
		ProjectSceneQuaternionToEulerDegrees({
			5.1898859965e-7f, 0.9322234988f, 5.1898859965e-7f, 0.3618831038f }, true);
	const std::array<float, 3> defaultWhisperRotation =
		ProjectSceneQuaternionToEulerDegrees({
			5.1898859965e-7f, 0.9322234988f, 5.1898859965e-7f, 0.3618831038f }, false);
	const std::array<float, 3> projectedUEFNRotation =
		ProjectSceneQuaternionToEulerDegrees({
			-0.7071067812f, 0.0f, 0.0f, 0.7071067812f }, false);
	const float whisperForwardOwnerYaw = ResolveModelOwnerFacingYaw(
		0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
	const float whisperRightTargetVisualYaw = ResolveAIVisualFacingYawDegrees(
		glm::vec3(1.0f, 0.0f, 0.0f));
	const float whisperLeftTargetVisualYaw = ResolveAIVisualFacingYawDegrees(
		glm::vec3(-1.0f, 0.0f, 0.0f));
	const float whisperRightTargetOwnerYaw = ResolveModelOwnerFacingYaw(
		whisperRightTargetVisualYaw, glm::vec3(0.0f, 0.0f, 1.0f));
	const float whisperRightTurnDelta = std::remainder(
		whisperRightTargetOwnerYaw - whisperForwardOwnerYaw, 360.0f);
	VansCharacterTrajectoryGenerator whisperRightTurn;
	VansCharacterMotionIntent whisperRightTurnIntent;
	whisperRightTurnIntent.movementReferenceYaw = 0.0f;
	whisperRightTurnIntent.desiredFacingYaw = whisperRightTargetOwnerYaw;
	whisperRightTurnIntent.hasFacing = true;
	whisperRightTurnIntent.valid = true;
	VansCharacterMotionSettings whisperTurnSettings;
	whisperTurnSettings.facingHalfLife = 0.1f;
	whisperTurnSettings.maxFacingYawRate = 720.0f;
	whisperRightTurn.Reset(glm::vec3(0.0f), whisperForwardOwnerYaw);
	whisperRightTurn.Update(0.1f, whisperRightTurnIntent, whisperTurnSettings,
		glm::vec3(0.0f), whisperForwardOwnerYaw);
	const float plannedWhisperRightYaw = whisperRightTurn.GetPlannedFacingYaw();
	const glm::vec3 plannedWhisperForward = glm::angleAxis(
		glm::radians(plannedWhisperRightYaw), glm::vec3(0.0f, 1.0f, 0.0f)) *
		glm::vec3(0.0f, 0.0f, 1.0f);
	VansRuntimeAIAgentComponent walkOnlyAgent;
	walkOnlyAgent.maxMovementState = 1;
	const glm::vec3 lateralFacing = ResolveAIChaseFacingDirection(
		glm::vec3(0.0f),
		glm::vec3(10.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f));
	if (!Expect(glm::dot(correctedNonstandardForward, desiredForward) > 0.9999f,
		"Nonstandard model forward was not corrected to the desired world facing") ||
		!Expect(std::abs(projectedWhisperRotation[0]) < 0.001f &&
			projectedWhisperRotation[1] > 137.5f && projectedWhisperRotation[1] < 137.7f &&
			std::abs(projectedWhisperRotation[2]) < 0.001f,
			"Configured Whisper scene rotation did not retain a stable Yaw branch") ||
		!Expect(std::abs(defaultWhisperRotation[0]) > 179.0f &&
			defaultWhisperRotation[1] > 42.3f && defaultWhisperRotation[1] < 42.6f &&
			std::abs(defaultWhisperRotation[2]) > 179.0f,
			"Unconfigured scene rotation unexpectedly entered the Whisper-only Yaw path") ||
		!Expect(projectedUEFNRotation[0] < -89.9f && projectedUEFNRotation[0] > -90.1f &&
			std::abs(projectedUEFNRotation[1]) < 0.001f &&
			std::abs(projectedUEFNRotation[2]) < 0.001f,
			"UEFN model-axis correction was incorrectly flattened to upright Yaw") ||
		!Expect(whisperRightTurnDelta > 89.9f && whisperRightTurnDelta < 90.1f &&
			whisperLeftTargetVisualYaw < -89.9f && whisperLeftTargetVisualYaw > -90.1f,
			"AI visual yaw did not preserve symmetric right/left turn signs") ||
		!Expect(plannedWhisperRightYaw > 0.1f && plannedWhisperForward.x > 0.1f,
			"Whisper facing interpolation did not approach a +X target") ||
		!Expect(std::abs(ResolveModelOwnerFacingYaw(
			35.0f, glm::vec3(0.0f, 0.0f, 1.0f)) - 35.0f) < 0.001f,
			"Engine +Z model forward should not receive a facing correction") ||
		!Expect(ResolveAIMovementState(4.0f, walkOnlyAgent) == 1,
			"Walk-only AI selected the Run movement state") ||
		!Expect(glm::dot(lateralFacing, glm::vec3(1.0f, 0.0f, 0.0f)) > 0.9999f,
			"Chase facing followed the route instead of a 90-degree target offset") ||
		!Expect(IsTargetInsideAIVisionCone(
			glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(10.0f, 0.0f, 0.0f), 14.0f, 240.0f),
			"Wide Whisper sight rejected a 90-degree lateral target") ||
		!Expect(!IsTargetInsideAIVisionCone(
			glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(0.0f, 0.0f, -10.0f), 14.0f, 240.0f),
			"Wide Whisper sight accepted a target directly behind it") ||
		!Expect(!IsTargetInsideAIVisionCone(
			glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(0.0f, 0.0f, 15.0f), 14.0f, 240.0f),
			"Whisper sight accepted a target outside its configured range"))
	{
		return false;
	}
	VansAIBlackboard blackboard;
	std::string error;
	const std::vector<VansAIBlackboardEntryDefinition> blackboardSchema = {
		{ "ActivationRequested", VansAIValueType::Bool, false },
		{ "Target", VansAIValueType::Entity, VansEntityHandle{} }
	};
	if (!Expect(blackboard.Configure(blackboardSchema, error), error.c_str()) ||
		!Expect(blackboard.SetBool("ActivationRequested", true, &error), error.c_str()) ||
		!Expect(blackboard.GetBool("ActivationRequested"),
			"Typed Blackboard bool update failed") ||
		!Expect(!blackboard.Set("ActivationRequested", VansAIValue(std::int64_t{ 1 }), &error),
			"Typed Blackboard accepted an invalid value type"))
	{
		return false;
	}

	VansSceneObjectBuildPlan plan;
	AppendEnvironmentBox(plan, "Floor", { 0.0f, -0.1f, 0.0f },
		{ 5.0f, 0.1f, 5.0f });
	AppendEnvironmentBox(plan, "Wall", { 0.0f, 1.5f, 0.0f },
		{ 0.5f, 1.5f, 2.0f });
	const VansNavigationGeometry geometry =
		VansSceneNavigationGeometry::BuildEnvironmentGeometry(plan);
	if (!Expect(geometry.VertexCount() == 16u && geometry.TriangleCount() == 24u,
		"Scene collider geometry projection failed"))
	{
		return false;
	}

	VansNavigationBuildSettings settings;
	settings.regionMinSize = 1.0f;
	settings.regionMergeSize = 2.0f;
	VansNavigationMesh mesh;
	if (!Expect(mesh.Build(geometry, settings, error), error.c_str()))
		return false;
	const VansNavigationPath path = mesh.FindPath(
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	const bool routedAroundWall = std::any_of(path.points.begin(), path.points.end(),
		[](const glm::vec3& point) { return std::abs(point.z) > 2.0f; });
	if (!Expect(path.status == VansNavigationPathStatus::Complete,
		"Detour did not produce a complete path") ||
		!Expect(path.points.size() >= 3u && routedAroundWall,
			"Path did not route around the Environment collider"))
	{
		return false;
	}

	const std::filesystem::path assetPath =
		std::filesystem::temp_directory_path() / "ForestNavigationContract.vnavmesh";
	if (!Expect(mesh.Save(assetPath, error), error.c_str()))
		return false;
	VansNavigationMesh loaded;
	const bool loadedOk = loaded.Load(assetPath, error);
	std::error_code removeError;
	std::filesystem::remove(assetPath, removeError);
	if (!Expect(loadedOk, error.c_str()))
		return false;
	const VansNavigationPath loadedPath = loaded.FindPath(
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	return Expect(loadedPath.status == VansNavigationPathStatus::Complete &&
		loadedPath.points.size() >= 3u,
		"Saved navigation asset did not preserve query behavior");
}
