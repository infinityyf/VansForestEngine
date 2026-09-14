#pragma once

#include "../TerrainCore/VansTerrainAsset.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Vans
{
using VansSceneFloat2 = std::array<float, 2>;
using VansSceneFloat3 = std::array<float, 3>;
using VansSceneFloat4 = std::array<float, 4>;

struct VansSceneTerrainPhysicsMaterialConfig
{
	std::optional<float> staticFriction;
	std::optional<float> dynamicFriction;
	std::optional<float> restitution;
};

struct VansSceneTerrainCollisionConfig
{
	std::optional<bool> enabled;
	std::optional<std::string> layer;
	VansSceneTerrainPhysicsMaterialConfig material;
};

struct VansSceneTerrainNodeConfig
{
	bool valid = false;
	std::string assetGuid;
	std::shared_ptr<const VansTerrainAsset> asset;
	std::optional<std::string> name;
	std::optional<VansSceneTerrainCollisionConfig> collision;
};

enum class VansSceneWaterWaveMode : std::uint32_t
{
	Gerstner = 0,
	FFT = 1,
	WaveParticle = 2,
};

struct VansSceneWaterMediumConfig
{
	std::optional<VansSceneFloat3> absorptionCoeff;
	std::optional<VansSceneFloat3> scatteringCoeff;
	std::optional<float> ior;
	std::optional<float> anisotropy;
	std::optional<float> waterRoughness;
};

struct VansSceneWaterSpectrumConfig
{
	std::optional<VansSceneWaterWaveMode> mode;
	std::optional<int> cascadeCount;
	std::optional<float> baseCoverage;
	std::optional<float> cascadeScale;
	std::optional<VansSceneFloat2> windDirection;
	std::optional<float> windSpeed;
	std::optional<float> swellAmplitude;
	std::optional<float> choppiness;
	std::optional<int> gerstnerWaveCount;
	std::optional<float> spectrumAmplitude;
	std::optional<float> minWavelength;
	std::optional<float> smallWaveDamping;
	std::optional<float> windDependency;
	std::optional<float> depth;
	std::optional<float> repeatPeriod;
	std::optional<std::uint32_t> randomSeed;
};

struct VansSceneWaterWaveParticleConfig
{
	std::optional<int> particlesPerCascade;
	std::optional<float> rmsAmplitude;
	std::optional<float> packetWidth;
	std::optional<float> dispersionScale;
	std::optional<float> directionSpread;
	std::optional<float> cascadeAmplitudeFalloff;
	std::optional<float> foamThreshold;
	std::optional<float> foamSoftness;
	std::optional<std::uint32_t> randomSeed;
};

struct VansSceneWaterFlowMapConfig
{
	std::optional<bool> enabled;
	std::optional<float> strength;
	std::optional<float> speed;
	std::optional<float> phaseLength;
	std::optional<float> noiseAmount;
	std::optional<VansSceneFloat2> worldOrigin;
	std::optional<VansSceneFloat2> worldSize;
	std::optional<VansSceneFloat2> fallbackDirection;
};


struct VansSceneWaterRefractionConfig
{
	std::optional<bool> enabled;
	std::optional<float> distortionStrength;
};

enum class VansSceneWaterNormalDecodeMode : std::uint32_t
{
	RGReconstructZ = 0,
};

enum class VansSceneWaterEffectiveRoughnessMode : std::uint32_t
{
	BaseOnly = 0,
	DistanceHeuristic = 1,
};

struct VansSceneWaterDetailNormalLayerConfig
{
	std::optional<bool> enabled;
	std::optional<float> tileSizeMeters;
	std::optional<VansSceneFloat2> direction;
	std::optional<float> speedMetersPerSecond;
	std::optional<float> phase;
	std::optional<float> strength;
	std::optional<float> fadeStartMeters;
	std::optional<float> fadeEndMeters;
};

struct VansSceneWaterDetailNormalConfig
{
	std::optional<bool> enabled;
	std::optional<VansSceneWaterNormalDecodeMode> decodeMode;
	std::optional<bool> flipGreen;
	std::optional<float> globalStrength;
	std::optional<float> maxSlope;
	std::optional<float> mipBias;
	std::optional<float> anisotropy;
	std::vector<VansSceneWaterDetailNormalLayerConfig> layers;
};

struct VansSceneWaterEffectiveRoughnessConfig
{
	std::optional<VansSceneWaterEffectiveRoughnessMode> mode;
	std::optional<float> distanceStartMeters;
	std::optional<float> distanceEndMeters;
	std::optional<float> distanceStrength;
};

struct VansSceneWaterColorMipConfig
{
	std::optional<float> refractionScatterScale;
	std::optional<float> refractionRoughnessScale;
	std::optional<float> forwardScatterMipScale;
	std::optional<float> backgroundScatterScale;
	std::optional<float> lodBias;
};

struct VansSceneWaterShadowConfig
{
	std::optional<bool> enabled;
	std::optional<int> quality;
	std::optional<float> depthBias;
	std::optional<float> normalBias;
	std::optional<int> volumeStepStride;
};

struct VansSceneWaterSSRConfig
{
	std::optional<bool> enabled;
	std::optional<float> maxDistance;
	std::optional<float> maxRoughness;
	std::optional<float> roughnessFadeStart;
	std::optional<float> colorMipConeScale;
	std::optional<float> colorMipBias;
	std::optional<float> edgeFadePixels;
};

struct VansSceneWaterSSSConfig
{
	std::optional<bool> enabled;
	std::optional<float> maxThickness;
	std::optional<float> deepFallback;
};

struct VansSceneWaterOpticsConfig
{
	std::optional<float> maxCrossDistance;
	std::optional<float> maxRefractionCrossDistance;
	std::optional<float> multiScatterScale;
	std::optional<float> waterDispersionStrength;
	std::optional<float> sssPathScale;
	std::optional<float> sssNonlinearStrength;
	std::optional<float> sssScatterBoost;
	std::optional<float> backlitPathScale;
	std::optional<float> backlitPhaseG;
};

struct VansSceneWaterVolumeConfig
{
	std::optional<float> resolutionScale;
	std::optional<int> sampleCount;
	std::optional<int> spatialFilterIterations;
	std::optional<float> spatialDepthSensitivity;
};

struct VansSceneWaterGeometryConfig
{
	std::optional<int> lodCount;
	std::optional<float> basePatchSize;
	std::optional<int> meshDim;
	std::optional<float> morphStartRatio;
};

struct VansSceneWaterNodeConfig
{
	bool valid = true;
	std::optional<float> level;
	std::optional<float> specularIntensity;
	std::optional<std::string> name;
	VansSceneWaterMediumConfig medium;
	VansSceneWaterSpectrumConfig spectrum;
	VansSceneWaterWaveParticleConfig waveParticle;
	VansSceneWaterFlowMapConfig flowMap;
	VansSceneWaterRefractionConfig refraction;
	VansSceneWaterDetailNormalConfig detailNormal;
	VansSceneWaterEffectiveRoughnessConfig effectiveRoughness;
	VansSceneWaterColorMipConfig colorMip;
	VansSceneWaterShadowConfig shadow;
	VansSceneWaterSSRConfig ssr;
	VansSceneWaterSSSConfig sss;
	VansSceneWaterOpticsConfig optics;
	VansSceneWaterVolumeConfig volume;
	VansSceneWaterGeometryConfig geometry;
};

}
