#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Vans
{
struct VansScenePlanetSettingsConfig
{
	std::array<double, 3> centerWorldMeters{ 0.0, -6340200.0, 0.0 };
	double bottomRadiusMeters = 6340000.0;
	double atmosphereHeightMeters = 80000.0;
};

struct VansSceneRayleighSettingsConfig
{
	std::array<float, 3> scatteringPerMeterAtGround{ 5.802e-6f, 13.558e-6f, 33.1e-6f };
	float densityScaleHeightMeters = 8500.0f;
};

struct VansSceneMieSettingsConfig
{
	std::array<float, 3> scatteringPerMeterAtGround{ 3.996e-6f, 3.996e-6f, 3.996e-6f };
	std::array<float, 3> absorptionPerMeterAtGround{ 4.4e-6f, 4.4e-6f, 4.4e-6f };
	float densityScaleHeightMeters = 1200.0f;
	float anisotropy = 0.78f;
};

struct VansSceneOzoneSettingsConfig
{
	std::array<float, 3> absorptionPerMeter{ 0.650e-6f, 1.881e-6f, 0.085e-6f };
	float centerAltitudeMeters = 25000.0f;
	float halfWidthMeters = 15000.0f;
};

struct VansSceneCelestialDiskSettingsConfig
{
	bool enabled = true;
	float angularRadiusRadians = 0.018f;
	float featherRadians = 0.0015f;
	float radianceScale = 1.0f;
	float occlusionStrength = 8.0f;
};

struct VansSceneCelestialBodySettingsConfig
{
	std::string name;
	std::string lightEntityId;
	VansSceneCelestialDiskSettingsConfig disk;
};

struct VansSceneAerialPerspectiveSettingsConfig
{
	float distanceScale = 1.0f;
};

struct VansScenePhysicalAtmosphereSettingsConfig
{
	bool enabled = true;
	std::array<float, 3> groundAlbedo{ 0.4f, 0.4f, 0.4f };
	VansSceneRayleighSettingsConfig rayleigh;
	VansSceneMieSettingsConfig mie;
	VansSceneOzoneSettingsConfig ozone;
	VansSceneAerialPerspectiveSettingsConfig aerialPerspective;
	// 仅缩放物理大气的主光直射入散射，不改变 extinction/optical depth。
	float mainLightVolumetricScatteringScale = 1.0f;
	std::vector<VansSceneCelestialBodySettingsConfig> celestialBodies;
};

struct VansSceneCloudShadowSettingsConfig
{
	bool enabled = true;
	// 强度只在线性辐照度域混合 1 与物理云透射率，不缩放光学厚度。
	float atmosphereStrength = 1.0f;
	float ambientOcclusionStrength = 0.25f;
};

struct VansSceneHeightFogSettingsConfig
{
	bool enabled = true;
	float groundHeightWorldMeters = 0.0f;
	// 地面高度上达到 e^-1 透射率的距离，直接决定基准消光 1 / visibility。
	float visibilityAtGroundMeters = 600.0f;
	float densityFalloffHeightMeters = 100.0f;
	float startDistanceMeters = 0.0f;
	float nearFadeDistanceMeters = 20.0f;
	float maximumDistanceMeters = 1500.0f;
	float farFadeDistanceMeters = 300.0f;
	std::array<float, 3> singleScatteringAlbedo{ 0.98f, 0.98f, 0.98f };
	float anisotropy = 0.2f;
	std::array<float, 3> emissivePerMeter{ 0.0f, 0.0f, 0.0f };
	float skyLightingScale = 1.0f;
	float mainLightVolumetricScale = 1.0f;
	bool receiveCloudShadows = true;
};

struct VansSceneVolumetricCloudSettingsConfig
{
	bool enabled = true;
	float cloudMinHeight = 1070.0f;
	float cloudMaxHeight = 7410.0f;
	float density = 0.025f;
	float coverage = 0.350f;
	float sunBrightness = 0.380f;
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
	float sigmaTRef = 1.000f;
	float viewAbsorption = 1.000f;
	float lightAbsorption = 1.000f;
	float singleScatteringAlbedo = 0.999f;
	float forwardEccentricity = 0.700f;
	float backwardEccentricity = 0.250f;
	float msAttenuation = 0.500f;
	float msContribution = 0.500f;
	float msEccentricity = 0.500f;
	float scatteringTintR = 1.000f;
	float scatteringTintG = 1.000f;
	float scatteringTintB = 1.000f;
	float scatterSourceODScale = 0.120f;
	float scatterSourceCurvePow = 1.000f;
	float aoUpwardScale = 1.000f;
	float ambientBottomStrength = 0.100f;
	float ambientTopStrength = 0.350f;
	float ambientDuskWarmth = 0.650f;
	float boundaryConfidence = 0.750f;
	float boundaryWrap = 0.350f;
	float phiFwdIntensity = 0.800f;
	float phiFwdDepthPow = 1.000f;
	float phiFwdDepthBias = 0.050f;
	float phiFwdMSBuildScale = 1.000f;
	float phiFwdCompress = 1.000f;
	float phiFwdMaxDistance = 6000.000f;
	float phiFwdConeRatio = 1.450f;
	float phiFwdMinStep = 80.000f;
	float lightStepCount = 8.000f;
	float boundaryGradientStep = 250.000f;
	float boundaryGradientStrength = 0.000f;
	float shadingDebugMode = 0.000f;
	VansSceneCloudShadowSettingsConfig shadow;
};

struct VansSceneEnvironmentSettingsConfig
{
	VansScenePlanetSettingsConfig planet;
	VansScenePhysicalAtmosphereSettingsConfig physicalAtmosphere;
	VansSceneHeightFogSettingsConfig heightFog;
	VansSceneVolumetricCloudSettingsConfig volumetricClouds;
};

struct VansScenePostProcessSettingsConfig
{
	std::optional<bool> enableAutoExposure;
	std::optional<float> exposureCompensation;
	std::optional<float> minEV100;
	std::optional<float> maxEV100;
	std::optional<float> adaptationSpeedUp;
	std::optional<float> adaptationSpeedDown;
	std::optional<bool> enableBloom;
	std::optional<float> bloomThreshold;
	std::optional<float> bloomKnee;
	std::optional<float> bloomIntensity;
	std::optional<float> bloomScatter;
	std::optional<float> bloomClamp;
	std::optional<float> bloomTintR;
	std::optional<float> bloomTintG;
	std::optional<float> bloomTintB;
	std::optional<int32_t> bloomShapeMode;
	std::optional<float> bloomShapeIntensity;
	std::optional<float> bloomShapeBlend;
	std::optional<float> bloomShapeAngleDeg;
	std::optional<float> bloomAnamorphicStretch;
	std::optional<int32_t> bloomStreakCount;
	std::optional<float> bloomStreakLength;
	std::optional<float> bloomStreakAttenuation;
	std::optional<bool> enableDOF;
	std::optional<float> focusDistance;
	std::optional<float> focalLengthMm;
	std::optional<float> fStop;
	std::optional<float> sensorHeightMm;
	std::optional<float> maxCoC;
	std::optional<bool> dofBlurTransmissionBackground;
	std::optional<int32_t> toneMapperType;
	std::optional<float> whitePoint;
	std::optional<bool> enableColorGrading;
	std::optional<float> contrast;
	std::optional<float> saturation;
	std::optional<float> hueShift;
	std::optional<float> temperature;
	std::optional<float> tint;
};

struct VansSceneGIRegionSettingsConfig
{
	std::optional<uint32_t> stableId;
	std::optional<std::string> name;
	std::optional<bool> enabled;
	std::optional<std::array<float, 3>> center;
	std::optional<std::array<float, 3>> size;
	std::optional<std::array<uint32_t, 3>> gridDimensions;
	std::optional<float> probeSpacing;
	std::optional<std::array<float, 3>> probeSpacingAxes;
	std::optional<uint32_t> raysPerProbe;
	std::optional<uint32_t> spatialUpdateDivisor;
	std::optional<uint32_t> directionUpdateSlices;
	std::optional<float> maxRayDistance;
	std::optional<float> normalBias;
	std::optional<float> volumeFadeDistance;
	std::optional<float> priority;
};

struct VansSceneGISettingsConfig
{
	std::vector<VansSceneGIRegionSettingsConfig> regions;
	std::optional<float> environmentIntensity;
	std::optional<float> maxIndirectRadiance;
	std::optional<float> maxProbeRadiance;
	std::optional<float> irradianceHysteresis;
	std::optional<float> distanceHysteresis;
	std::optional<float> distanceSharpness;
	std::optional<float> brightnessChangeThreshold;
	std::optional<bool> showProbeGizmos;
	std::optional<bool> showProbeVolume;
	std::optional<uint32_t> gizmoStride;
};

struct VansSceneMainCameraHiZCullSettingsConfig
{
	std::optional<bool> enabled;
	std::optional<bool> enableOpaque;
	std::optional<bool> enableHair;
	std::optional<bool> enableTransparent;
	std::optional<bool> enableDecal;
	std::optional<bool> enableForwardOpaquePreAtmosphere;
	std::optional<float> depthBiasMeters;
	std::optional<float> cameraMotionDisableDistance;
	std::optional<float> cameraMotionDisableAngleRadians;
	std::optional<uint32_t> forceVisibleFramesAfterChange;
	std::optional<uint32_t> refreshCulledEveryNFrames;
	std::optional<float> maxScreenCoverageForCull;
};

struct VansSceneRenderSettingsConfig
{
	VansSceneEnvironmentSettingsConfig environment;
	std::optional<VansScenePostProcessSettingsConfig> postProcess;
	std::optional<VansSceneGISettingsConfig> globalIllumination;
	std::optional<VansSceneMainCameraHiZCullSettingsConfig> mainCameraHiZCulling;
};
}
