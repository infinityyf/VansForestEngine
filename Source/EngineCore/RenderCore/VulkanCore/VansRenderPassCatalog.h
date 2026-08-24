#pragma once

#include "VansRenderGraph.h"
#include "../VansRenderSceneSnapshot.h"

namespace VansGraphics
{
	enum class VansRenderPassCondition
	{
		Always,
		HasPunctualShadowJobs,
		HasWater,
		HasDecal,
		HasForwardOpaqueAfterDeferred
	};

	namespace VansRenderPassNames
	{
		constexpr const char* VideoTextureUpload = "Video Texture Upload";
		constexpr const char* ClothVertexUpload = "Cloth Vertex Upload";
		constexpr const char* VegetationCompute = "Vegetation Compute";
		constexpr const char* CascadeShadow = "Cascade Shadow";
		constexpr const char* PunctualShadow = "Punctual Shadow";
		constexpr const char* HairDeepOpacity = "Hair Deep Opacity";
		constexpr const char* MainCameraHiZCull = "Main Camera HiZ Cull";
		constexpr const char* SkyMotionVector = "Sky Motion Vector";
		constexpr const char* GBuffer = "GBuffer";
		constexpr const char* WaterWaveCompute = "Water Wave Compute";
		constexpr const char* WaterGBuffer = "Water GBuffer";
		constexpr const char* Decal = "Decal";
		constexpr const char* TileLightBuild = "Tile Light Build";
		constexpr const char* HZB = "HZB";
		constexpr const char* PunctualShadowDebug = "Punctual Shadow Debug Preview";
		constexpr const char* ScreenSpaceShadow = "Screen Space Shadow";
		constexpr const char* ScreenSpaceEffects = "Screen Space Effects";
		constexpr const char* SSAOFilter = "SSAO Filter";
		constexpr const char* GIData = "GI Data";
		constexpr const char* SSR = "SSR";
		constexpr const char* RayTracing = "Ray Tracing";
		constexpr const char* VolumetricFog = "Volumetric Fog";
		constexpr const char* CloudRayMarch = "Cloud Ray March";
		constexpr const char* DepthOfFieldPrepare = "Depth Of Field Prepare";
		constexpr const char* ExposureBloom = "Exposure Bloom";
		constexpr const char* DeferredSkybox = "Deferred Skybox";
		constexpr const char* WaterPreCompute = "Water Pre Compute";
		constexpr const char* ForwardOpaqueAfterDeferred = "Forward Opaque After Deferred";
		constexpr const char* HairVisibility = "Hair Visibility";
		constexpr const char* HairLighting = "Hair Lighting";
		constexpr const char* TransparentSceneColorPrepare = "Transparent SceneColor Prepare";
		constexpr const char* TransparentPostProcess = "Transparent PostProcess";
		constexpr const char* TemporalUpscale = "Temporal Upscale";
		constexpr const char* DisplayPostProcess = "Display PostProcess";
		constexpr const char* RuntimeUI = "Runtime UI";
		constexpr const char* ReflectionProbeBakeQueue = "Reflection Probe Bake Queue";
	}

	class VansRenderPassCatalog
	{
	public:
		static void BuildCompatibilityFramePlan(
			VansRenderFramePlan& outPlan,
			const VansRenderFeatureFrameFlags& features,
			uint64_t frameNumber,
			bool asyncComputeEnabled = false);

		static bool IsPassEnabled(
			VansRenderPassCondition condition,
			const VansRenderFeatureFrameFlags& features);

		static bool IsKnownPassName(const char* passName);
		static bool AuditAsyncMigrationContracts(std::vector<std::string>& outErrors);

		static void GetPreservedFeatureAuditList(
			const VansRenderFeatureFrameFlags& features,
			std::vector<std::string>& outRequiredFeatures,
			std::vector<std::string>& outConditionallyDisabledFeatures);
	};
}
