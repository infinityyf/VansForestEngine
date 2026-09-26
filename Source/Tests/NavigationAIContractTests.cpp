#include "NavigationAIContractTests.h"

#include "../EngineCore/AICore/VansAIBlackboard.h"
#include "../EngineCore/AICore/VansAIEvents.h"
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
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace
{
class ScopedFileRemoval
{
public:
	explicit ScopedFileRemoval(std::filesystem::path path)
		: m_Path(std::move(path))
	{
	}

	~ScopedFileRemoval()
	{
		std::error_code error;
		std::filesystem::remove(m_Path, error);
	}

private:
	std::filesystem::path m_Path;
};

void AppendEnvironmentBox(Vans::VansSceneObjectBuildPlan& plan,
	const char* name,
	const std::array<float, 3>& position,
	const std::array<float, 3>& extents,
	const char* navigationArea = nullptr)
{
	Vans::VansSceneObjectBuildConfig object;
	object.entityGuid = Vans::VansAssetGuid::FromStableName(
		"NavigationContract", name).ToString();
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
	if (navigationArea) physics.navigationArea = navigationArea;
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
	VansRuntimeAIAgentComponent scheduledAgent;
	VansRuntimeNavigationAgentComponent scheduledNavigation;
	if (!Expect(ValidateAIRuntimeTiming(
			scheduledAgent, scheduledNavigation, error), error.c_str()))
	{
		return false;
	}
	VansRuntimeAIAgentComponent invalidTiming = scheduledAgent;
	invalidTiming.timing.perceptionInterval = 0.0f;
	if (!Expect(!ValidateAIRuntimeTiming(
			invalidTiming, scheduledNavigation, error),
		"AI timing accepted a zero perception interval"))
	{
		return false;
	}
	VansRuntimeNavigationAgentComponent invalidRepath = scheduledNavigation;
	invalidRepath.forceRepathDistance = invalidRepath.targetMoveThreshold;
	if (!Expect(!ValidateAIRuntimeTiming(
			scheduledAgent, invalidRepath, error),
		"AI repath policy accepted a non-distinct force threshold"))
	{
		return false;
	}
	const std::vector<VansAIBlackboardEntryDefinition> blackboardSchema = {
		{ "ActivationRequested", VansAIValueType::Bool, false },
		{ "Target", VansAIValueType::Entity, VansEntityHandle{} }
	};
	if (!Expect(blackboard.Configure(blackboardSchema, error), error.c_str()) ||
		!Expect(blackboard.SetBool("ActivationRequested", true,
			"script-guid", &error), error.c_str()) ||
		!Expect(blackboard.GetBool("ActivationRequested"),
			"Typed Blackboard bool update failed") ||
		!Expect(!blackboard.Set("ActivationRequested", VansAIValue(std::int64_t{ 1 }),
			"invalid-test", &error),
			"Typed Blackboard accepted an invalid value type"))
	{
		return false;
	}
	const VansAIBlackboardDebugSnapshot blackboardDebug =
		blackboard.CaptureDebugSnapshot(1u);
	const VansAIActivationRequested activationEvent{
		VansEntityHandle{ 3u, 2u }, "script-guid" };
	if (!Expect(blackboardDebug.totalEntries == 2u && blackboardDebug.truncated &&
			blackboardDebug.entries.size() == 1u &&
			blackboardDebug.entries.front().name == "ActivationRequested" &&
			blackboardDebug.entries.front().lastWriter == "script-guid",
			"Blackboard diagnostics did not preserve stable order, bound, and writer") ||
		!Expect(activationEvent.target.IsValid() &&
			activationEvent.sourceGuid == "script-guid",
			"AI event did not preserve its source component identity"))
	{
		return false;
	}

	VansNavigationSettings settings;
	settings.bake.regionMinSize = 1.0f;
	settings.bake.regionMergeSize = 2.0f;
	const auto testSource = [](const VansNavigationSettings& sourceSettings,
		const char* scene)
	{
		VansNavigationSource source;
		source.scene = scene;
		source.sceneHash = 1u;
		source.colliderHash = HashNavigationColliders({});
		source.settingsHash = HashNavigationSettings(sourceSettings);
		return source;
	};
	VansSceneObjectBuildPlan plan;
	AppendEnvironmentBox(plan, "Floor", { 0.0f, -0.1f, 0.0f },
		{ 5.0f, 0.1f, 5.0f });
	AppendEnvironmentBox(plan, "Wall", { 0.0f, 1.5f, 0.0f },
		{ 0.5f, 1.5f, 2.0f });
	VansNavigationGeometry geometry;
	if (!Expect(VansSceneNavigationGeometry::BuildEnvironmentGeometry(
		plan, settings.areas, {}, geometry, error), error.c_str()))
	{
		return false;
	}
	if (!Expect(geometry.VertexCount() == 16u && geometry.TriangleCount() == 24u &&
		geometry.areas.size() == geometry.TriangleCount() &&
		std::all_of(geometry.areas.begin(), geometry.areas.end(),
			[](std::uint8_t area) { return area == 0u; }),
		"Scene collider geometry projection failed"))
	{
		return false;
	}

	VansSceneObjectBuildPlan hierarchyPlan;
	VansSceneObjectBuildConfig parent;
	const VansEntityGuid parentGuid = VansAssetGuid::FromStableName(
		"NavigationContract", "HierarchyParent");
	parent.entityGuid = parentGuid.ToString();
	parent.name = "HierarchyParent";
	parent.transform = VansSceneTransformConfig{};
	parent.transform->position = { 10.0f, 0.0f, 0.0f };
	parent.transform->rotation = { 0.0f, 90.0f, 0.0f };
	parent.transform->scale = { 2.0f, 1.0f, 3.0f };
	hierarchyPlan.objects.push_back(parent);
	AppendEnvironmentBox(hierarchyPlan, "HierarchyChild", { 1.0f, 0.0f, 0.0f },
		{ 1.0f, 0.5f, 0.25f });
	VansSceneObjectBuildConfig& child = hierarchyPlan.objects.back();
	child.transform->scale = { 1.0f, 2.0f, 1.0f };
	child.physicsComponents.physics->shapeOffset = { 0.5f, 0.0f, 0.0f };
	child.parent = VansSceneParentReference{};
	child.parent->entityGuid = parentGuid;
	AppendEnvironmentBox(hierarchyPlan, "DynamicEnvironment",
		{ 100.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
	hierarchyPlan.objects.back().physicsComponents.physics->bodyType = "dynamic";
	VansNavigationGeometry hierarchyGeometry;
	if (!Expect(VansSceneNavigationGeometry::BuildEnvironmentGeometry(
		hierarchyPlan, settings.areas, {}, hierarchyGeometry, error), error.c_str()) ||
		!Expect(hierarchyGeometry.VertexCount() == 8u &&
			hierarchyGeometry.TriangleCount() == 12u,
			"Navigation geometry included a non-static collider"))
	{
		return false;
	}
	glm::vec3 minimum(std::numeric_limits<float>::max());
	glm::vec3 maximum(std::numeric_limits<float>::lowest());
	for (std::size_t vertex = 0; vertex < hierarchyGeometry.VertexCount(); ++vertex)
	{
		const glm::vec3 point(
			hierarchyGeometry.vertices[vertex * 3u],
			hierarchyGeometry.vertices[vertex * 3u + 1u],
			hierarchyGeometry.vertices[vertex * 3u + 2u]);
		minimum = glm::min(minimum, point);
		maximum = glm::max(maximum, point);
	}
	if (!Expect(glm::all(glm::lessThan(glm::abs(
		minimum - glm::vec3(9.25f, -1.0f, -5.0f)), glm::vec3(0.001f))) &&
		glm::all(glm::lessThan(glm::abs(
			maximum - glm::vec3(10.75f, 1.0f, -1.0f)), glm::vec3(0.001f))),
		"Navigation geometry did not apply parent rotation, parent scale, child scale, and offset"))
	{
		return false;
	}

	VansSceneObjectBuildPlan conflictingOffsetPlan;
	AppendEnvironmentBox(conflictingOffsetPlan, "ConflictingOffset",
		{ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
	conflictingOffsetPlan.objects.back().physicsComponents.physics->shapeOffset =
		{ 0.0f, 0.0f, 0.0f };
	conflictingOffsetPlan.objects.back().physicsComponents.physics->colliderOffset =
		{ 1.0f, 0.0f, 0.0f };
	VansNavigationGeometry preservedGeometry;
	preservedGeometry.vertices = { 42.0f, 43.0f, 44.0f };
	if (!Expect(!VansSceneNavigationGeometry::BuildEnvironmentGeometry(
		conflictingOffsetPlan, settings.areas, {}, preservedGeometry, error) &&
		error.find("both shapeOffset and colliderOffset") != std::string::npos &&
		preservedGeometry.vertices == std::vector<float>{ 42.0f, 43.0f, 44.0f },
		"Navigation geometry accepted conflicting offsets or published partial output"))
	{
		return false;
	}
	VansSceneObjectBuildPlan unknownAreaPlan;
	AppendEnvironmentBox(unknownAreaPlan, "UnknownArea",
		{ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, "Missing");
	VansNavigationGeometry preservedAreaGeometry;
	preservedAreaGeometry.vertices = { 7.0f, 8.0f, 9.0f };
	preservedAreaGeometry.areas = { 4u };
	if (!Expect(!VansSceneNavigationGeometry::BuildEnvironmentGeometry(
		unknownAreaPlan, settings.areas, {}, preservedAreaGeometry, error) &&
		error.find("unknown area 'Missing'") != std::string::npos &&
		preservedAreaGeometry.vertices == std::vector<float>{ 7.0f, 8.0f, 9.0f } &&
		preservedAreaGeometry.areas == std::vector<std::uint8_t>{ 4u },
		"Navigation geometry accepted an unknown area or published partial output"))
	{
		return false;
	}

	VansSceneObjectBuildPlan meshPlan;
	VansSceneObjectBuildConfig meshObject;
	meshObject.entityGuid = VansAssetGuid::FromStableName(
		"NavigationContract", "MeshCollider").ToString();
	meshObject.name = "MeshCollider";
	meshObject.transform = VansSceneTransformConfig{};
	meshObject.transform->position = { 2.0f, 0.0f, 3.0f };
	meshObject.transform->scale = { 2.0f, 1.0f, 3.0f };
	VansScenePhysicsNodeConfig meshPhysics;
	meshPhysics.enabled = true;
	meshPhysics.bodyType = "static";
	meshPhysics.colliderType = "mesh";
	meshPhysics.useMeshCollider = true;
	meshPhysics.layer = "Environment";
	meshPhysics.isTrigger = false;
	meshPhysics.navigationArea = "Walkable";
	meshPhysics.mesh = "mesh-collider-guid";
	meshObject.physicsComponents.physics = meshPhysics;
	meshPlan.objects.push_back(std::move(meshObject));
	VansTriangleMeshData triangleMesh;
	triangleMesh.positions = {
		0.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f
	};
	triangleMesh.indices = { 0u, 1u, 2u };
	std::size_t meshResolveCount = 0;
	const VansNavigationMeshResolver meshResolver = [&triangleMesh, &meshResolveCount](
		const std::string& guid, VansTriangleMeshData& output, std::string& resolveError)
	{
		if (guid != "mesh-collider-guid")
		{
			resolveError = "Unexpected mesh collider GUID";
			return false;
		}
		++meshResolveCount;
		output = triangleMesh;
		return true;
	};
	VansNavigationGeometry meshGeometry;
	std::vector<std::string> meshAssets;
	if (!Expect(VansSceneNavigationGeometry::BuildEnvironmentGeometry(
		meshPlan, settings.areas, meshResolver, meshGeometry, error), error.c_str()) ||
		!Expect(meshResolveCount == 1u && meshGeometry.VertexCount() == 3u &&
			meshGeometry.TriangleCount() == 1u &&
			meshGeometry.vertices == std::vector<float>{
				2.0f, 0.0f, 3.0f, 4.0f, 0.0f, 3.0f, 2.0f, 0.0f, 6.0f },
			"Navigation mesh collider geometry did not preserve source topology and transform") ||
		!Expect(VansSceneNavigationGeometry::CollectEnvironmentMeshAssets(
			meshPlan, meshAssets, error), error.c_str()) ||
		!Expect(meshAssets == std::vector<std::string>{ "mesh-collider-guid" },
			"Navigation mesh collider source collection failed"))
	{
		return false;
	}
	const std::vector<VansNavigationColliderSource> colliderSources = {
		{ "b", 2u, 3u }, { "a", 4u, 5u }
	};
	const std::vector<VansNavigationColliderSource> reorderedColliderSources = {
		{ "a", 4u, 5u }, { "b", 2u, 3u }
	};
	const std::vector<VansNavigationColliderSource> changedColliderSources = {
		{ "a", 4u, 6u }, { "b", 2u, 3u }
	};
	if (!Expect(HashNavigationColliders(colliderSources) ==
		HashNavigationColliders(reorderedColliderSources) &&
		HashNavigationColliders(colliderSources) !=
		HashNavigationColliders(changedColliderSources),
		"Navigation collider closure hash is order-dependent or ignores source changes"))
	{
		return false;
	}

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

	VansNavigationSettings highCostSettings = settings;
	highCostSettings.areas.definitions.push_back({ "Slow", 1u, 50.0f, true });
	VansSceneObjectBuildPlan areaPlan;
	AppendEnvironmentBox(areaPlan, "AreaStart", { -4.0f, -0.1f, 0.0f },
		{ 1.2f, 0.1f, 3.1f });
	AppendEnvironmentBox(areaPlan, "AreaEnd", { 4.0f, -0.1f, 0.0f },
		{ 1.2f, 0.1f, 3.1f });
	AppendEnvironmentBox(areaPlan, "AreaDirect", { 0.0f, -0.1f, 0.0f },
		{ 3.0f, 0.1f, 1.0f }, "Slow");
	AppendEnvironmentBox(areaPlan, "AreaDetour", { 0.0f, -0.1f, 2.0f },
		{ 3.0f, 0.1f, 1.0f });
	VansNavigationGeometry areaGeometry;
	if (!Expect(VansSceneNavigationGeometry::BuildEnvironmentGeometry(
		areaPlan, highCostSettings.areas, {}, areaGeometry, error), error.c_str()))
	{
		return false;
	}
	VansNavigationMesh highCostMesh;
	if (!Expect(highCostMesh.Build(areaGeometry, highCostSettings, error), error.c_str()))
		return false;
	const auto usesAreaDetour = [](const VansNavigationPath& candidate)
	{
		return std::any_of(candidate.points.begin(), candidate.points.end(),
			[](const glm::vec3& point) { return point.z > 0.9f; });
	};
	const VansNavigationPath highCostPath = highCostMesh.FindPath(
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(highCostPath.status == VansNavigationPathStatus::Complete &&
		usesAreaDetour(highCostPath),
		"Navigation area traversal cost did not select the lower-cost route"))
	{
		return false;
	}
	const std::filesystem::path areaAssetPath =
		std::filesystem::temp_directory_path() / "ForestNavigationAreaContract.vnavmesh";
	ScopedFileRemoval areaAssetRemoval(areaAssetPath);
	if (!Expect(highCostMesh.Save(areaAssetPath,
		testSource(highCostSettings, "Scenes/NavigationAreaContract.json"), error),
		error.c_str())) return false;
	VansNavigationSettings staleBakeSettings = highCostSettings;
	staleBakeSettings.bake.agentRadius += 0.1f;
	VansNavigationMesh staleBakeMesh;
	if (!Expect(!staleBakeMesh.Load(areaAssetPath, staleBakeSettings, error) &&
		error.find("stale") != std::string::npos,
		"Navigation asset accepted incompatible bake settings"))
	{
		return false;
	}

	VansNavigationSettings equalCostSettings = highCostSettings;
	equalCostSettings.areas.definitions.back().traversalCost = 1.0f;
	VansNavigationMesh equalCostMesh;
	if (!Expect(equalCostMesh.Load(areaAssetPath, equalCostSettings, error), error.c_str()))
		return false;
	const VansNavigationPath equalCostPath = equalCostMesh.FindPath(
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(equalCostPath.status == VansNavigationPathStatus::Complete &&
		!usesAreaDetour(equalCostPath),
		"Equal-cost navigation areas did not preserve the shorter direct route"))
	{
		return false;
	}

	VansNavigationSettings forbiddenSettings = equalCostSettings;
	forbiddenSettings.areas.definitions.back().traversable = false;
	VansNavigationMesh forbiddenMesh;
	if (!Expect(forbiddenMesh.Load(areaAssetPath, forbiddenSettings, error), error.c_str()))
		return false;
	const VansNavigationPath forbiddenPath = forbiddenMesh.FindPath(
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(forbiddenPath.status == VansNavigationPathStatus::Complete &&
		usesAreaDetour(forbiddenPath),
		"Forbidden navigation area remained queryable after asset loading"))
	{
		return false;
	}

	const std::filesystem::path assetPath =
		std::filesystem::temp_directory_path() / "ForestNavigationContract.vnavmesh";
	ScopedFileRemoval assetRemoval(assetPath);
	const VansNavigationSource navigationTestSource =
		testSource(settings, "Scenes/NavigationContract.json");
	if (!Expect(mesh.Save(assetPath, navigationTestSource, error), error.c_str()))
		return false;
	VansNavigationMesh loaded;
	const bool loadedOk = loaded.Load(assetPath, settings, error);
	if (!Expect(loadedOk, error.c_str()))
		return false;
	const VansNavigationPath loadedPath = loaded.FindPath(
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(loadedPath.status == VansNavigationPathStatus::Complete &&
		loadedPath.points.size() >= 3u,
		"Saved navigation asset did not preserve query behavior"))
	{
		return false;
	}

	VansNavigationSettings corridorLimit = settings;
	corridorLimit.query.maximumCorridorPolygons = 2;
	VansNavigationMesh corridorLimitedMesh;
	if (!Expect(corridorLimitedMesh.Load(assetPath, corridorLimit, error),
		error.c_str()))
	{
		return false;
	}
	const VansNavigationPath corridorLimitedPath = corridorLimitedMesh.FindPath(
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(corridorLimitedPath.status == VansNavigationPathStatus::Failed &&
		corridorLimitedPath.failure ==
			VansNavigationPathFailure::CorridorCapacityExceeded &&
		corridorLimitedPath.points.empty(),
		"Detour corridor capacity exhaustion was not reported structurally"))
	{
		return false;
	}

	VansNavigationSettings cornerLimit = settings;
	cornerLimit.query.maximumPathPoints = 2;
	VansNavigationMesh cornerLimitedMesh;
	if (!Expect(cornerLimitedMesh.Load(assetPath, cornerLimit, error), error.c_str()))
		return false;
	const VansNavigationPath cornerLimitedPath = cornerLimitedMesh.FindPath(
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(cornerLimitedPath.status == VansNavigationPathStatus::Failed &&
		cornerLimitedPath.failure ==
			VansNavigationPathFailure::CornerCapacityExceeded &&
		cornerLimitedPath.points.empty(),
		"Detour corner capacity exhaustion was not reported structurally"))
	{
		return false;
	}

	VansNavigationSettings nodeLimit = settings;
	nodeLimit.query.maximumSearchNodes = 4;
	VansNavigationMesh nodeLimitedMesh;
	if (!Expect(nodeLimitedMesh.Load(assetPath, nodeLimit, error), error.c_str()))
		return false;
	const VansNavigationPath nodeLimitedPath = nodeLimitedMesh.FindPath(
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(nodeLimitedPath.status == VansNavigationPathStatus::Failed &&
		nodeLimitedPath.failure ==
			VansNavigationPathFailure::SearchNodeCapacityExceeded &&
		nodeLimitedPath.points.empty(),
		"Detour search-node exhaustion was not reported structurally"))
	{
		return false;
	}

	const std::filesystem::path corruptAssetPath =
		std::filesystem::temp_directory_path() / "ForestNavigationContractCorrupt.vnavmesh";
	ScopedFileRemoval corruptAssetRemoval(corruptAssetPath);
	std::ifstream validAsset(assetPath, std::ios::binary);
	const std::istreambuf_iterator<char> assetBegin(validAsset);
	const std::istreambuf_iterator<char> assetEnd;
	std::vector<unsigned char> corruptAsset(assetBegin, assetEnd);
	const std::size_t headerSize = sizeof(std::uint32_t) * 2u +
		sizeof(float) * 12u + sizeof(int) + sizeof(std::uint8_t) +
		sizeof(std::uint32_t) + navigationTestSource.scene.size() +
		sizeof(std::uint64_t) * 4u;
	if (!Expect(!validAsset.bad() && corruptAsset.size() > headerSize,
		"Could not prepare a corrupt navigation asset for rollback validation"))
	{
		return false;
	}
	std::fill(corruptAsset.begin() + static_cast<std::ptrdiff_t>(headerSize),
		corruptAsset.end(), 0u);
	std::ofstream corruptAssetStream(corruptAssetPath,
		std::ios::binary | std::ios::trunc);
	corruptAssetStream.write(reinterpret_cast<const char*>(corruptAsset.data()),
		static_cast<std::streamsize>(corruptAsset.size()));
	corruptAssetStream.close();
	if (!Expect(static_cast<bool>(corruptAssetStream),
		"Could not write the corrupt navigation asset"))
	{
		return false;
	}

	const bool corruptLoadSucceeded = loaded.Load(
		corruptAssetPath, settings, error);
	const VansNavigationPath preservedPath = loaded.FindPath(
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	return Expect(!corruptLoadSucceeded,
		"Corrupt navigation data unexpectedly loaded") &&
		Expect(loaded.IsReady() &&
			preservedPath.status == VansNavigationPathStatus::Complete &&
			preservedPath.points.size() >= 3u,
			"Failed navigation loading destroyed the previous query state");
}
