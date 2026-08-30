#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Vans
{
using VansSceneFloat2 = std::array<float, 2>;
using VansSceneFloat3 = std::array<float, 3>;
using VansSceneFloat4 = std::array<float, 4>;

struct VansSceneTerrainNoiseDetailConfig
{
	std::optional<bool> enabled;
	std::optional<float> strength;
	std::optional<float> frequency;
	std::optional<float> lacunarity;
	std::optional<float> gain;
	std::optional<int> octaves;
	std::optional<float> warpStrength;
	std::optional<float> fadeStart;
};

struct VansSceneTerrainTessellationConfig
{
	std::optional<bool> enabled;
	std::optional<float> distance;
	std::optional<float> maxLevel;
	std::optional<float> power;
	std::optional<float> lodBias;
	std::optional<float> displacementStrength;
	VansSceneTerrainNoiseDetailConfig noiseDetail;
};

struct VansSceneTerrainLayerConfig
{
	std::optional<std::string> albedoTexture;
	std::optional<std::string> albedoPath;
	std::optional<std::string> normalTexture;
	std::optional<std::string> normalPath;
	std::optional<std::string> roughnessTexture;
	std::optional<std::string> roughnessPath;
	std::optional<float> tiling;
};

struct VansSceneTerrainPhysicsMaterialConfig
{
	std::optional<float> staticFriction;
	std::optional<float> dynamicFriction;
	std::optional<float> restitution;
};

struct VansSceneTerrainCollisionConfig
{
	std::optional<bool> enabled;
	std::optional<float> terrainSize;
	std::optional<float> maxHeight;
	std::optional<float> heightOffset;
	std::optional<std::string> layer;
	std::optional<bool> flipX;
	std::optional<bool> flipZ;
	VansSceneTerrainPhysicsMaterialConfig material;
};

struct VansSceneTerrainNodeConfig
{
	std::optional<std::string> heightmap;
	std::optional<float> terrainSize;
	std::optional<float> maxHeight;
	std::optional<float> heightOffset;
	std::optional<float> splitDistMult;
	std::optional<float> lodDistanceRatio;
	std::optional<float> morphStartRatio;
	std::optional<unsigned int> maxPatchInstances;
	VansSceneTerrainTessellationConfig tessellation;
	std::vector<std::string> splatmaps;
	std::vector<VansSceneTerrainLayerConfig> layers;
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

struct VansSceneWaterCausticsConfig
{
	std::optional<bool> enabled;
	std::optional<float> intensity;
	std::optional<float> maxDistance;
	std::optional<float> maxGain;
	std::optional<float> filterRadius;
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
	VansSceneWaterCausticsConfig caustics;
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

struct VansScenePcgMaskConfig
{
	std::optional<std::string> id;
	std::optional<std::string> path;
	std::optional<std::string> assetGuid;
	std::optional<std::string> textureValue;
	std::optional<std::string> channel;
	std::optional<VansSceneFloat2> boundsMin;
	std::optional<VansSceneFloat2> boundsMax;
	std::optional<VansSceneFloat2> worldMin;
	std::optional<VansSceneFloat2> worldMax;
	std::optional<float> threshold;
	std::optional<float> densityScale;
	std::optional<bool> invert;
};

struct VansScenePcgMaskReferenceConfig
{
	std::optional<std::string> ref;
	std::optional<VansScenePcgMaskConfig> inlineMask;
};

struct VansSceneVegetationPlacementConfig
{
	std::optional<VansSceneFloat2> boundsMin;
	std::optional<VansSceneFloat2> boundsMax;
	std::optional<float> grassScaleMin;
	std::optional<float> grassScaleMax;
	std::optional<VansScenePcgMaskReferenceConfig> mask;
};

enum class VansSceneVegetationTreePartType : std::uint32_t
{
	Trunk = 0,
	Leaves = 1,
	Custom = 2,
};

struct VansSceneVegetationTreePartConfig
{
	VansSceneVegetationTreePartType type = VansSceneVegetationTreePartType::Custom;
	std::string mesh;
	std::string material;
	std::optional<std::int32_t> submeshIndex;
};

struct VansSceneVegetationTreeSpeciesConfig
{
	std::string name;
	std::optional<float> boundsRadius;
	std::vector<VansSceneVegetationTreePartConfig> parts;
};

struct VansSceneVegetationTreeInstanceConfig
{
	std::optional<std::string> species;
	std::optional<VansSceneFloat3> position;
	std::optional<float> yaw;
	std::optional<float> scale;
	std::optional<std::int32_t> submeshIndex;
};

struct VansSceneVegetationRandomTreeConfig
{
	std::optional<std::uint32_t> count;
	std::optional<std::uint32_t> seed;
	std::optional<float> scaleMin;
	std::optional<float> scaleMax;
	std::optional<VansSceneFloat2> boundsMin;
	std::optional<VansSceneFloat2> boundsMax;
	std::optional<std::string> species;
	std::optional<std::int32_t> submeshIndex;
	std::optional<VansScenePcgMaskReferenceConfig> mask;
};

struct VansSceneVegetationTreesConfig
{
	std::optional<bool> enabled;
	std::optional<float> cullDistance;
	std::optional<bool> cullEnabled;
	std::optional<bool> hizEnabled;
	std::vector<VansSceneVegetationTreeSpeciesConfig> species;
	std::vector<VansSceneVegetationTreeInstanceConfig> instances;
	std::optional<VansSceneVegetationRandomTreeConfig> randomInstances;
	std::optional<std::uint32_t> fallbackCount;
	std::optional<float> placementRadius;
	std::optional<VansSceneFloat3> center;
};

struct VansSceneVegetationRenderConfig
{
	std::optional<std::string> mesh;
	std::optional<std::string> material;
	std::optional<float> percent;
};

struct VansSceneVegetationNodeConfig
{
	bool valid = true;
	std::optional<std::uint32_t> instanceCount;
	std::optional<std::uint32_t> boneCount;
	std::optional<float> bladeHeight;
	std::optional<float> windDirX;
	std::optional<float> windDirZ;
	std::optional<float> leanDeviation;
	std::optional<std::string> material;
	std::optional<std::string> name;
	std::optional<std::uint32_t> subBladeCount;
	std::optional<float> subBladeScatterRadiusMin;
	std::optional<float> subBladeScatterRadiusMax;
	std::optional<float> windStrength;
	std::optional<float> windFrequency;
	std::optional<float> windSpeed;
	std::optional<float> windBendMult;
	std::optional<float> stiffness;
	std::optional<float> damping;
	std::optional<float> softness;
	std::optional<float> lodFullDist;
	std::optional<float> lodFadeDist;
	std::optional<float> terrainMaxHeight;
	std::optional<float> terrainHeightOffset;
	std::optional<float> hizSampleBias;
	std::optional<float> grassScaleMin;
	std::optional<float> grassScaleMax;
	std::optional<VansSceneVegetationPlacementConfig> placement;
	std::optional<VansSceneVegetationTreesConfig> trees;
	std::vector<VansSceneVegetationRenderConfig> renderConfigs;
	std::vector<VansScenePcgMaskConfig> pcgMasks;
};
}
