#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace Vans
{
struct VansSceneHeightFogSettingsConfig
{
	std::optional<float> fogDensity;
	std::optional<float> heightFalloff;
	std::optional<float> sunScatterScale;
	std::optional<float> ambientScale;
	std::optional<float> fogMinHeight;
	std::optional<float> skyFogDistance;
};

struct VansSceneVolumetricFogSettingsConfig
{
	std::optional<float> density;
	std::optional<float> anisotropy;
	std::optional<float> scatterScale;
	std::optional<float> ambientScale;
	std::optional<float> volumeNear;
	std::optional<float> volumeFar;
	std::optional<float> slicePower;
	std::optional<std::array<float, 3>> fogBoxMin;
	std::optional<std::array<float, 3>> fogBoxMax;
};

struct VansSceneVolumetricCloudSettingsConfig
{
	std::optional<float> planetRadius;
	std::optional<float> seaLevel;
	std::optional<float> cloudBaseHeight;
	std::optional<float> cloudMinHeight;
	std::optional<float> cloudThickness;
	std::optional<float> cloudMaxHeight;
	std::optional<float> density;
	std::optional<float> coverage;
	std::optional<float> sunBrightness;
	std::optional<float> phaseG;
	std::optional<float> mainTileMeters;
	std::optional<float> detailTileMeters;
	std::optional<float> mainHeightScale;
	std::optional<float> detailHeightScale;
	std::optional<float> thresholdLowCoverage;
	std::optional<float> thresholdHighCoverage;
	std::optional<float> densityRemapLow;
	std::optional<float> densityRemapHigh;
	std::optional<float> mainErosionStrength;
	std::optional<float> detailErosionStrength;
	std::optional<float> edgeErosionStrength;
	std::optional<float> verticalShapePower;
	std::optional<float> detailErosionLow;
	std::optional<float> detailErosionHigh;
	std::optional<float> detailEdgeStrength;
	std::optional<float> shadowDensityScale;
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
	std::optional<int32_t> toneMapperType;
	std::optional<float> whitePoint;
	std::optional<bool> enableColorGrading;
	std::optional<float> contrast;
	std::optional<float> saturation;
	std::optional<float> hueShift;
	std::optional<float> temperature;
	std::optional<float> tint;
};

struct VansSceneGISettingsConfig
{
	std::optional<std::array<uint32_t, 3>> gridDimensions;
	std::optional<std::array<float, 3>> probeSpacingAxes;
	std::optional<std::array<float, 3>> regionCenter;
	std::optional<uint32_t> raysPerProbe;
	std::optional<uint32_t> spatialUpdateDivisor;
	std::optional<uint32_t> directionUpdateSlices;
	std::optional<float> maxRayDistance;
	std::optional<float> normalBias;
	std::optional<float> environmentIntensity;
	std::optional<float> maxIndirectRadiance;
	std::optional<float> maxSHL0;
	std::optional<float> volumeFadeDistance;
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
	std::optional<bool> enableForwardOpaqueAfterDeferred;
	std::optional<float> depthBiasMeters;
	std::optional<float> cameraMotionDisableDistance;
	std::optional<float> cameraMotionDisableAngleRadians;
	std::optional<uint32_t> forceVisibleFramesAfterChange;
	std::optional<uint32_t> refreshCulledEveryNFrames;
	std::optional<float> maxScreenCoverageForCull;
};

struct VansSceneRenderSettingsConfig
{
	std::optional<VansSceneHeightFogSettingsConfig> heightFog;
	std::optional<VansSceneVolumetricFogSettingsConfig> volumetricFog;
	std::optional<VansSceneVolumetricCloudSettingsConfig> volumetricClouds;
	std::optional<VansScenePostProcessSettingsConfig> postProcess;
	std::optional<VansSceneGISettingsConfig> globalIllumination;
	std::optional<VansSceneMainCameraHiZCullSettingsConfig> mainCameraHiZCulling;
};
}
