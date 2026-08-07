#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
	std::optional<float> sigmaTRef;
	std::optional<float> viewAbsorption;
	std::optional<float> lightAbsorption;
	std::optional<float> singleScatteringAlbedo;
	std::optional<float> forwardEccentricity;
	std::optional<float> backwardEccentricity;
	std::optional<float> msAttenuation;
	std::optional<float> msContribution;
	std::optional<float> msEccentricity;
	std::optional<float> scatteringTintR;
	std::optional<float> scatteringTintG;
	std::optional<float> scatteringTintB;
	std::optional<float> scatterSourceODScale;
	std::optional<float> scatterSourceCurvePow;
	std::optional<float> aoUpwardScale;
	std::optional<float> ambientBottomStrength;
	std::optional<float> ambientTopStrength;
	std::optional<float> ambientDuskWarmth;
	std::optional<float> boundaryConfidence;
	std::optional<float> boundaryWrap;
	std::optional<float> phiFwdIntensity;
	std::optional<float> phiFwdDepthPow;
	std::optional<float> phiFwdDepthBias;
	std::optional<float> phiFwdMSBuildScale;
	std::optional<float> phiFwdCompress;
	std::optional<float> phiFwdMaxDistance;
	std::optional<float> phiFwdConeRatio;
	std::optional<float> phiFwdMinStep;
	std::optional<float> lightStepCount;
	std::optional<float> boundaryGradientStep;
	std::optional<float> boundaryGradientStrength;
	std::optional<float> shadingDebugMode;
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

struct VansSceneGIRegionSettingsConfig
{
	std::optional<uint32_t> stableId;
	std::optional<std::string> name;
	std::optional<bool> enabled;
	std::optional<std::array<float, 3>> center;
	std::optional<std::array<float, 3>> size;
	std::optional<std::array<uint32_t, 3>> gridDimensions;
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
	std::optional<std::array<uint32_t, 3>> gridDimensions;
	std::optional<uint32_t> gridSize;
	std::optional<float> probeSpacing;
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
