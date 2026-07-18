#pragma once

#include "EngineIds.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Vans::EditorAPI
{
	using EditorTextureHandle = void*;

	struct Vec2
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct Vec3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct Vec4
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 0.0f;
	};

	struct Ray
	{
		Vec3 origin;
		Vec3 direction;
	};

	struct RuntimeTransformSnapshot
	{
		bool available = false;
		std::string entityGuid;
		Vec3 position;
		Vec3 rotationDegrees;
		Vec3 scale = { 1.0f, 1.0f, 1.0f };
	};

	struct RuntimeTransformEdit
	{
		std::string entityGuid;
		Vec3 position;
		Vec3 rotationDegrees;
		Vec3 scale = { 1.0f, 1.0f, 1.0f };
		bool writePosition = true;
		bool writeRotation = true;
		bool writeScale = true;
	};

	struct RuntimeMultiMeshChildSnapshot
	{
		std::uint32_t submeshIndex = 0;
		std::string sourceNode;
		std::string sourceMaterial;
		std::string materialGuid;
	};

	struct RuntimeMultiMeshGroupSnapshot
	{
		std::string parentName;
		std::vector<RuntimeMultiMeshChildSnapshot> children;
	};

	enum class PropertyType
	{
		Unknown,
		Bool,
		Int,
		UInt,
		Float,
		Vec2,
		Vec3,
		Vec4,
		String,
		Guid,
		Enum
	};

	enum class RuntimeSceneLoadMode
	{
		Editor,
		Runtime
	};

	using PropertyValue = std::variant<
		std::monostate,
		bool,
		std::int32_t,
		std::uint32_t,
		float,
		Vec2,
		Vec3,
		Vec4,
		std::string>;

	struct PropertyEntry
	{
		std::string name;
		PropertyType type = PropertyType::Unknown;
		PropertyValue value;
		bool isReadOnly = false;
		std::vector<std::string> enumOptions;
	};

	struct ComponentEntry
	{
		ComponentId id = InvalidComponentId;
		std::string typeName;
		bool isEnabled = true;
	};

	struct ComponentDataSnapshot
	{
		ComponentId id = InvalidComponentId;
		std::string typeName;
		std::vector<PropertyEntry> properties;
	};

	struct EntityEntry
	{
		EntityId id = InvalidEntityId;
		EntityId parentId = InvalidEntityId;
		std::string name;
		bool isActive = true;
		std::vector<ComponentEntry> components;
	};

	struct EntityDataSnapshot
	{
		EntityEntry entity;
		std::vector<ComponentDataSnapshot> components;
	};

	struct SceneDataSnapshot
	{
		std::string sceneName;
		std::vector<EntityEntry> entities;
	};

	enum class AssetType
	{
		Unknown,
		Model,
		Texture,
		Material,
		Shader,
		Audio,
		Video,
		Scene,
		Particle,
		AnimationClip,
		AnimatorController,
		ClothProfile,
		PostProcessProfile,
		RagdollProfile
	};

	struct AssetTypeFilter
	{
		AssetType type = AssetType::Unknown;
		bool includeUnknown = false;
	};

	struct AssetEntry
	{
		AssetId id = InvalidAssetId;
		std::string guid;
		std::string name;
		std::string relativePath;
		AssetType type = AssetType::Unknown;
	};

	struct AssetMetaSnapshot
	{
		AssetId id = InvalidAssetId;
		AssetType type = AssetType::Unknown;
		std::string sourcePath;
		std::string artifactPath;
		std::vector<PropertyEntry> settings;
	};

	struct ProjectBrowserRootSnapshot
	{
		bool projectLoaded = false;
		std::string rootPath;
		std::string rootLabel;
	};

	struct AssetDragPayload
	{
		bool available = false;
		std::string guid;
		std::string error;
	};

	struct AssetGuidResolution
	{
		bool found = false;
		AssetEntry asset;
		std::string sourcePath;
	};

	struct AssetRefreshResult
	{
		bool success = false;
		std::string message;
	};

	struct ProjectOpenRequest
	{
		std::string projectPath;
		std::string projectName;
		bool createNew = false;
	};

	struct ProjectOpenResult
	{
		bool success = false;
		std::string projectRootPath;
		std::string defaultSceneRelativePath;
		std::string defaultScenePath;
		std::string message;
	};

	struct RecentProjectEntry
	{
		std::string name;
		std::string path;
		std::string lastOpened;
	};

	struct RenderTextureFilter
	{
		std::string category;
		std::string name;
		std::uint32_t layer = 0;
		std::uint32_t probeIndex = 0;
		std::uint32_t face = 0;
		float roughness = 0.0f;
	};

	struct RenderTexturePreview
	{
		RenderTextureId id = 0;
		std::string name;
		EditorTextureHandle texture = nullptr;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	struct RenderBackendDiagnostics
	{
		bool available = false;
		bool compiledGraphValid = false;
		bool featureAuditPassed = false;
		bool frameSubmitSucceeded = false;
		bool shadowSubmitted = false;
		bool gbufferSubmitted = false;
		bool asyncComputeSubmitted = false;
		std::uint32_t framePlanPassCount = 0;
		std::uint32_t compiledResourceCount = 0;
		std::uint32_t barrierDependencyCount = 0;
		std::uint32_t descriptorStandardPoolCount = 0;
		std::uint32_t descriptorUpdateAfterBindPoolCount = 0;
		std::uint32_t descriptorTrackedSetCount = 0;
		std::uint32_t descriptorUpdateAfterBindLayoutCount = 0;
		std::uint32_t descriptorGlobalPersistentSetCount = 0;
		std::uint32_t descriptorScenePersistentSetCount = 0;
		std::uint32_t descriptorFrameTransientSetCount = 0;
		std::uint32_t descriptorPassPersistentSetCount = 0;
		std::uint32_t descriptorUploadScratchSetCount = 0;
		std::uint32_t descriptorRayTracingPersistentSetCount = 0;
		std::uint64_t deferredDeleteLastFlushCount = 0;
		std::uint64_t deferredDeletePendingCount = 0;
		std::uint64_t renderNodeDescriptorValidationFailureCount = 0;
		std::uint64_t textureUploadFailureCount = 0;
		std::uint64_t frameNumber = 0;
		std::uint32_t swapchainImageIndex = 0;
		std::string renderGraphSummary;
	};

	struct ReflectionProbeEditorSettings
	{
		int selectedProbeIndex = -1;
		bool showProbeGizmos = false;
		bool showInfluenceVolumes = true;
		bool showBlendVolumes = true;
		bool showPlacementGrid = false;
		bool showRegions = false;
		bool previewCubemap = false;
		int previewFace = 0;
		float previewRoughness = 0.0f;
		int debugView = 0;
	};

	struct ReflectionProbePlacementSettingsSnapshot
	{
		bool enabled = true;
		Vec3 volumeMin = { -20.0f, -14.0f, -20.0f };
		Vec3 volumeMax = { 20.0f, 26.0f, 20.0f };
		float uniformSpacing = 7.0f;
		float uniformBoxSizeScale = 1.0f;
		std::uint32_t uniformProbeResolution = 64;
		std::uint32_t maxProbeCount = 256;
	};

	struct ReflectionProbeLightingSettingsSnapshot
	{
		std::uint32_t maxBlendCount = 4;
		float ssrRoughnessFadeStart = 0.35f;
		float ssrRoughnessFadeEnd = 0.75f;
		float skyIntensity = 1.0f;
	};

	struct ReflectionProbeEntrySnapshot
	{
		std::string name;
		int type = 0;
		int shape = 1;
		Vec3 position;
		Vec3 capturePosition;
		Vec3 boxMin = { -5.0f, -5.0f, -5.0f };
		Vec3 boxMax = { 5.0f, 5.0f, 5.0f };
		float radius = 10.0f;
		float blendDistance = 1.0f;
		float priority = 1.0f;
		float intensity = 1.0f;
		float specularIntensity = 1.0f;
		bool enabled = true;
		bool boxProjection = false;
		bool autoGenerated = false;
		std::string bakeStatus;
	};

	struct ReflectionProbeSettingsSnapshot
	{
		bool available = false;
		std::uint32_t arrayResolution = 0;
		std::uint32_t mipCount = 0;
		ReflectionProbeEditorSettings editor;
		ReflectionProbePlacementSettingsSnapshot placement;
		ReflectionProbeLightingSettingsSnapshot lighting;
		std::vector<ReflectionProbeEntrySnapshot> probes;
		std::vector<std::string> validationErrors;
	};

	struct WaterMediumSettings
	{
		Vec3 absorptionCoeff = { 0.25f, 0.08f, 0.02f };
		Vec3 scatteringCoeff = { 0.02f, 0.04f, 0.06f };
		float ior = 1.33f;
		float fresnelPower = 5.0f;
		float anisotropy = 0.85f;
		float waterRoughness = 0.02f;
		Vec4 deepColor = { 0.01f, 0.04f, 0.18f, 1.0f };
		Vec4 shallowColor = { 0.05f, 0.18f, 0.55f, 1.0f };
	};

	struct WaterFFTSettings
	{
		bool useDerivativeNormal = true;
		int resolution = 256;
		int lodCount = 4;
		float spectrumAmplitude = 4.0f;
		float choppiness = 1.0f;
		float smallWaveDamping = 0.003f;
		float windDependency = 0.07f;
		float depth = 10000.0f;
		float repeatPeriod = 0.0f;
		float foamSlopeScale = 0.25f;
		float foamFoldScale = 1.0f;
	};

	struct WaterDetailNormalSettings
	{
		bool enabled = true;
		float intensity = 1.0f;
		float scale = 1.0f;
		int octaveCount = 4;
		float timeOffset = 0.0f;
		float detailBaseScale = 16.0f;
	};

	struct WaterWaveSettings
	{
		int mode = 0;
		float baseScale = 64.0f;
		int maxLod = 10;
		Vec2 windDirection = { 0.7071f, 0.7071f };
		float windSpeed = 12.0f;
		float swellAmplitude = 0.2f;
		float chopScale = 1.5f;
		int gerstnerWaveCount = 64;
		int fftLodCount = 4;
		int fftResolution = 256;
		WaterFFTSettings fft;
		WaterDetailNormalSettings detailNormal;
	};

	struct WaterLODSettings
	{
		int maxLod = 10;
		float basePatchSize = 16.0f;
		int meshDim = 65;
		float detailBalance = 2.0f;
		float morphWidthRatio = 0.5f;
	};

	struct WaterSettingsSnapshot
	{
		bool available = false;
		int type = 0;
		float waterLevel = 3.4f;
		float specularIntensity = 1.0f;
		WaterMediumSettings medium;
		WaterLODSettings lod;
		WaterWaveSettings waves;
		bool sssEnabled = true;
		float maxThicknessDistance = 15.0f;
		float deepWaterThicknessFallback = 0.8f;
		bool causticsEnabled = true;
		float causticsIntensity = 1.0f;
		float causticsScale = 0.5f;
		bool refractionEnabled = true;
		float refractionMaxDistance = 50.0f;
		float refractionScale = 0.5f;
		bool ssrEnabled = true;
		float ssrMaxDistance = 500.0f;
		float ssrMaxRoughness = 0.3f;
		bool foamEnabled = true;
		float foamIntensity = 1.0f;
	};

	struct WaterRuntimeStats
	{
		bool available = false;
		bool systemInitialized = false;
		bool fftAvailable = false;
		std::uint32_t patchCount = 0;
		int meshDim = 0;
		float basePatchSize = 0.0f;
		std::uint32_t indexCount = 0;
		int lodLevels = 0;
		float detailBalance = 2.0f;
		int maxWaterTextureLayer = 0;
		int maxFftLod = 0;
		int fftFieldCount = 0;
	};

	struct MeshLoadRequest
	{
		std::string meshName;
		std::string sourcePath;
	};

	struct MeshLoadResult
	{
		bool loaded = false;
		bool available = false;
	};

	struct ProjectSubmeshInfo
	{
		std::string sourceNodeName;
		std::string materialName;
		std::string diffuseTexturePath;
		std::uint32_t vertexCount = 0;
		std::uint32_t indexCount = 0;
	};

	struct ProjectMeshInfoSnapshot
	{
		bool available = false;
		bool isMultiMesh = false;
		std::vector<ProjectSubmeshInfo> submeshes;
	};

	struct ProjectMeshAliasRequest
	{
		std::string aliasName;
		std::string meshName;
	};

	struct ModelAssetPlacementRequest
	{
		std::string assetGuid;
		Vec3 worldPosition;
	};

	struct ModelAssetPlacementPayload
	{
		bool prepared = false;
		std::string message;
		std::vector<std::string> sceneEntityJsons;
		std::string runtimeEntityGuid;
	};

	struct VehicleChassisDebugSnapshot
	{
		Vec3 center;
		Vec3 axisX = { 1.0f, 0.0f, 0.0f };
		Vec3 axisY = { 0.0f, 1.0f, 0.0f };
		Vec3 axisZ = { 0.0f, 0.0f, 1.0f };
		Vec3 halfExtents;
	};

	struct VehicleWheelDebugSnapshot
	{
		Vec3 center;
		Vec3 lateralAxis = { 1.0f, 0.0f, 0.0f };
		Vec3 verticalAxis = { 0.0f, 1.0f, 0.0f };
		Vec3 longitudinalAxis = { 0.0f, 0.0f, 1.0f };
		Vec3 suspensionAttach;
		Vec3 suspensionRayEnd;
		float radius = 0.0f;
		float halfWidth = 0.0f;
	};

	struct VehicleDebugSnapshot
	{
		bool available = false;
		VehicleChassisDebugSnapshot chassis;
		std::vector<VehicleWheelDebugSnapshot> wheels;
	};

	struct MotionMatchingDebugVisual
	{
		Vec3 rootPosition;
		Vec3 velocity;
		std::string activeClip;
	};

	struct MotionMatchingDebugSnapshot
	{
		bool available = false;
		std::vector<MotionMatchingDebugVisual> visuals;
	};

	struct FootIKDebugSampleSnapshot
	{
		Vec3 rayStart;
		Vec3 rayEnd;
		Vec3 hitPosition;
		Vec3 hitNormal = { 0.0f, 1.0f, 0.0f };
		Vec3 rawHitPosition;
		bool hasHit = false;
		bool hasRawHit = false;
		bool accepted = false;
		float quality = 0.0f;
		std::uint32_t hitLayer = 0;
		std::uint32_t rawHitLayer = 0;
		std::string hitActorName;
		std::string rawHitActorName;
		std::string status;
	};

	struct FootIKDebugLegSnapshot
	{
		Vec3 hip;
		Vec3 knee;
		Vec3 foot;
		Vec3 target;
		Vec3 contact;
		Vec3 normal = { 0.0f, 1.0f, 0.0f };
		Vec3 overlapCenter;
		Vec3 overlapHalfExtents;
		std::vector<FootIKDebugSampleSnapshot> samples;
		bool hasContact = false;
		bool hasTarget = false;
		bool hasOverlap = false;
		float targetWeight = 0.0f;
		std::uint32_t overlapLayer = 0;
		std::string overlapActorName;
	};

	struct FootIKDebugSnapshot
	{
		bool available = false;
		std::vector<FootIKDebugLegSnapshot> leftLegs;
		std::vector<FootIKDebugLegSnapshot> rightLegs;
	};

	struct RuntimeModelEntityCreateRequest
	{
		std::string entityName;
		std::string meshName;
		std::string materialName;
		Vec3 position;
	};

	struct RuntimeModelEntityCreateResult
	{
		bool created = false;
		std::string entityGuid;
	};

	struct RuntimeEntityDestroyRequest
	{
		std::string entityName;
		std::string entityGuid;
	};

	struct RuntimeEntityDestroyResult
	{
		bool destroyed = false;
	};

	struct TerrainSettingsSnapshot
	{
		bool available = false;
		bool tessellationEnabled = false;
		float tessellationDistance = 0.0f;
		float maxTessellationLevel = 0.0f;
		float tessellationPower = 0.0f;
		float tessLodBias = 0.0f;
		bool noiseDetailEnabled = false;
		float noiseStrength = 0.0f;
		float noiseFrequency = 0.0f;
		int noiseOctaves = 0;
		float noiseGain = 0.0f;
		float noiseLacunarity = 0.0f;
		float noiseWarpStrength = 0.0f;
		float noiseFadeStart = 0.0f;
		float terrainSize = 0.0f;
		float splitDistMult = 0.0f;
		float lodDistanceRatio = 0.0f;
	};

	struct DirectionalLightSettings
	{
		Vec3 direction = { 0.0f, -1.0f, 0.0f };
		Vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
	};

	struct PointLightSettings
	{
		Vec3 position;
		Vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
		float radius = 1.0f;
	};

	struct SpotLightSettings
	{
		Vec3 position;
		Vec3 direction = { 0.0f, -1.0f, 0.0f };
		Vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
		float radius = 1.0f;
		float innerCutoffRadians = 0.0f;
		float outerCutoffRadians = 0.0f;
	};

	struct LightingSettingsSnapshot
	{
		std::vector<DirectionalLightSettings> directionalLights;
		std::vector<PointLightSettings> pointLights;
		std::vector<SpotLightSettings> spotLights;
	};

	struct FogSettings
	{
		float fogDensity = 0.01f;
		float heightFalloff = 0.05f;
		float sunScatterScale = 0.3f;
		float ambientScale = 0.5f;
		float fogMinHeight = -100.0f;
		float skyFogDistance = 3000000.0f;
	};

	struct FogVolumeSettings
	{
		float density = 0.05f;
		float anisotropy = 0.6f;
		float scatterScale = 1.0f;
		float ambientScale = 0.05f;
		float volumeNear = 2.0f;
		float volumeFar = 200.0f;
		float slicePower = 2.0f;
		float padding = 0.0f;
		float fogBoxMin[4] = { -50.0f, -50.0f, -50.0f, 0.0f };
		float fogBoxMax[4] = { 50.0f, 50.0f, 50.0f, 0.0f };
	};

	struct CloudSettings
	{
		float planetRadius = 6340000.0f;
		float seaLevel = 200.0f;
		float cloudMinHeight = 1070.0f;
		float cloudMaxHeight = 7410.0f;
		float density = 0.025f;
		float coverage = 0.350f;
		float sunBrightness = 0.380f;
		float phaseG = 0.365f;
		float mainTileMeters = 43300.0f;
		float detailTileMeters = 2200.0f;
		float mainHeightScale = 0.260f;
		float detailHeightScale = 3.070f;
		float thresholdLowCoverage = 0.115f;
		float thresholdHighCoverage = 0.720f;
		float densityRemapLow = 0.425f;
		float densityRemapHigh = 0.915f;
		float mainErosionStrength = 1.160f;
		float detailErosionStrength = 1.340f;
		float edgeErosionStrength = 0.500f;
		float verticalShapePower = 1.420f;
		float detailErosionLow = 0.280f;
		float detailErosionHigh = 0.810f;
		float detailEdgeStrength = 0.270f;
		float shadowDensityScale = 0.870f;
	};

	enum class EnginePlayState
	{
		Edit,
		Play,
		Pause
	};
}
