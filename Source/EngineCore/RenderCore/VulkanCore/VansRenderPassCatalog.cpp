#include "VansRenderPassCatalog.h"

#include "../VansScene.h"
#include <cstring>

namespace VansGraphics
{
	namespace
	{
		struct VansPreservedRenderFeatureDesc
		{
			VansPreservedRenderFeatureDesc() = default;
			VansPreservedRenderFeatureDesc(const char* featureName)
				: name(featureName)
			{
			}
			VansPreservedRenderFeatureDesc(
				const char* featureName,
				VansRenderPassCondition featureCondition)
				: name(featureName)
				, condition(featureCondition)
			{
			}

			const char* name = "";
			VansRenderPassCondition condition = VansRenderPassCondition::Always;
		};

		struct VansRenderPassCatalogEntry
		{
			const char* name = "";
			VansRenderQueueClass queue = VansRenderQueueClass::Graphics;
			bool resizeDependent = false;
			bool allowAsyncCompute = false;
			VansRenderPassCondition condition = VansRenderPassCondition::Always;
			std::vector<VansRenderResourceAccess> reads;
			std::vector<VansRenderResourceAccess> writes;
			std::vector<VansPreservedRenderFeatureDesc> preservedFeatures;
		};

		bool IsMigratedAsyncPass(const char* passName)
		{
			return std::strcmp(passName, VansRenderPassNames::VegetationCompute) == 0
				|| std::strcmp(passName, VansRenderPassNames::MainCameraHiZCull) == 0
				|| std::strcmp(passName, VansRenderPassNames::TileLightBuild) == 0
				|| std::strcmp(passName, VansRenderPassNames::CloudRayMarch) == 0
				|| std::strcmp(passName, VansRenderPassNames::RayTracing) == 0
				|| std::strcmp(passName, VansRenderPassNames::GIData) == 0;
		}

		VansRenderPassNodeDesc MakeNodeDesc(
			const VansRenderPassCatalogEntry& entry,
			const VansScene& scene,
			bool asyncComputeEnabled)
		{
			VansRenderPassNodeDesc desc{};
			desc.name = entry.name;
			desc.passId = VansRenderGraphIntern::InternName(entry.name);
			desc.queue = asyncComputeEnabled && IsMigratedAsyncPass(entry.name)
				? VansRenderQueueClass::AsyncCompute
				: VansRenderQueueClass::Graphics;
			desc.resizeDependent = entry.resizeDependent;
			desc.allowAsyncCompute = entry.allowAsyncCompute;
			desc.enabled = VansRenderPassCatalog::IsPassEnabled(entry.condition, scene);
			desc.reads = entry.reads;
			desc.writes = entry.writes;
			for (auto& read : desc.reads)
			{
				read.resourceId = VansRenderGraphIntern::InternName(read.name);
			}
			for (auto& write : desc.writes)
			{
				write.resourceId = VansRenderGraphIntern::InternName(write.name);
			}
			for (const auto& feature : entry.preservedFeatures)
			{
				if (VansRenderPassCatalog::IsPassEnabled(feature.condition, scene))
				{
					desc.preservedFeatures.emplace_back(feature.name);
				}
			}
			return desc;
		}

		const std::vector<VansRenderPassCatalogEntry>& GetCompatibilityCatalog()
		{
			static const std::vector<VansRenderPassCatalogEntry> catalog =
			{
				{ VansRenderPassNames::VideoTextureUpload, VansRenderQueueClass::Graphics, false, true, VansRenderPassCondition::Always,
					{},
					{ { "VideoTextures", VansRenderResourceUsage::TransferDst } },
					{ "Video texture streaming" } },
				{ VansRenderPassNames::ClothVertexUpload, VansRenderQueueClass::Graphics, false, true, VansRenderPassCondition::Always,
					{ { "ClothStagingBuffers", VansRenderResourceUsage::TransferSrc } },
					{ { "ClothVertexBuffers", VansRenderResourceUsage::TransferDst } },
					{ "Cloth render vertex update" } },
				{ VansRenderPassNames::VegetationCompute, VansRenderQueueClass::Compute, false, true, VansRenderPassCondition::Always,
					{ { "VegetationInputs", VansRenderResourceUsage::StorageRead },
					  { "OcclusionHZB", VansRenderResourceUsage::SampledRead } },
					{ { "VegetationDrawData", VansRenderResourceUsage::StorageWrite },
					  { "VegetationIndirectArgs", VansRenderResourceUsage::StorageWrite },
					  { "VegetationVisibleIndices", VansRenderResourceUsage::StorageWrite },
					  { "VegetationBoneMatrices", VansRenderResourceUsage::StorageWrite } },
					{ "GPU vegetation wind/cull/indirect" } },
				{ VansRenderPassNames::CascadeShadow, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneGeometry", VansRenderResourceUsage::SampledRead },
					  { "VegetationDrawData", VansRenderResourceUsage::IndirectArgumentRead } },
					{ { "CascadeShadowDepth", VansRenderResourceUsage::DepthStencilAttachmentWrite } },
					{ "Cascaded directional shadows" } },
				{ VansRenderPassNames::PunctualShadow, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::HasPunctualShadowJobs,
					{ { "SceneGeometry", VansRenderResourceUsage::SampledRead },
					  { "VegetationDrawData", VansRenderResourceUsage::IndirectArgumentRead } },
					{ { "PunctualShadowAtlas", VansRenderResourceUsage::DepthStencilAttachmentWrite } },
					{ "Point and spot shadows" } },
				{ VansRenderPassNames::HairDeepOpacity, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "HairGeometry", VansRenderResourceUsage::SampledRead } },
					{ { "HairDeepOpacity", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Hair deep opacity" } },
				{ VansRenderPassNames::MainCameraHiZCull, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "OcclusionHZB", VansRenderResourceUsage::SampledRead },
					  { "MainCameraCullObjects", VansRenderResourceUsage::StorageRead } },
					{ { "MainCameraVisibilityReadback", VansRenderResourceUsage::StorageWrite } },
					{ "Main camera HiZ occlusion culling" } },
				{ VansRenderPassNames::MotionVector, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneGeometry", VansRenderResourceUsage::SampledRead } },
					{ { "MotionVectors", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Motion vectors" } },
				{ VansRenderPassNames::GBuffer, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneGeometry", VansRenderResourceUsage::SampledRead },
					  { "VegetationDrawData", VansRenderResourceUsage::IndirectArgumentRead } },
					{ { "Normal", VansRenderResourceUsage::ColorAttachmentWrite },
					  { "GBuffer", VansRenderResourceUsage::ColorAttachmentWrite },
					  { "MaterialBuffers", VansRenderResourceUsage::ColorAttachmentWrite },
					  { "Depth", VansRenderResourceUsage::DepthStencilAttachmentWrite } },
					{ "Deferred GBuffer", "GBuffer normal", "GBuffer material buffers", "GBuffer depth",
					  "Existing material types and shader pass coverage" } },
				{ VansRenderPassNames::Decal, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::HasDecal,
					{ { "Depth", VansRenderResourceUsage::DepthStencilAttachmentRead },
					  { "GBuffer", VansRenderResourceUsage::SampledRead } },
					{ { "GBuffer", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Decals" } },
				{ VansRenderPassNames::TileLightBuild, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "LightBuffers", VansRenderResourceUsage::StorageRead },
					  { "PunctualShadowMeta", VansRenderResourceUsage::StorageRead } },
					{ { "TileLightLists", VansRenderResourceUsage::StorageWrite } },
					{ "Deferred lighting tiled light build" } },
				{ VansRenderPassNames::HZB, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "Depth", VansRenderResourceUsage::SampledRead } },
					{ { "HZB", VansRenderResourceUsage::StorageWrite },
					  { "OcclusionHZB", VansRenderResourceUsage::StorageWrite } },
					{ "HZB" } },
				{ VansRenderPassNames::PunctualShadowDebug, VansRenderQueueClass::Compute, false, false, VansRenderPassCondition::Always,
					{ { "PunctualShadowAtlas", VansRenderResourceUsage::SampledRead } },
					{ { "PunctualShadowDebugPreview", VansRenderResourceUsage::StorageWrite } },
					{} },
				{ VansRenderPassNames::ScreenSpaceShadow, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "Depth", VansRenderResourceUsage::SampledRead },
					  { "CascadeShadowDepth", VansRenderResourceUsage::SampledRead } },
					{ { "ScreenSpaceShadow", VansRenderResourceUsage::StorageWrite } },
					{ "Screen-space shadows" } },
				{ VansRenderPassNames::ScreenSpaceEffects, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "Normal", VansRenderResourceUsage::SampledRead },
					  { "GBuffer", VansRenderResourceUsage::SampledRead },
					  { "Depth", VansRenderResourceUsage::SampledRead } },
					{ { "SSAO", VansRenderResourceUsage::StorageWrite } },
					{ "SSAO raw" } },
				{ VansRenderPassNames::RayTracing, VansRenderQueueClass::Compute, false, true, VansRenderPassCondition::Always,
					{ { "TLAS", VansRenderResourceUsage::AccelerationStructureBuildRead },
					  { "GBuffer", VansRenderResourceUsage::SampledRead } },
					{ { "RayTracingGI", VansRenderResourceUsage::StorageWrite } },
					{ "BLAS/TLAS", "Ray tracing dispatch", "GI/probe integration" } },
				{ VansRenderPassNames::GIData, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "GBuffer", VansRenderResourceUsage::SampledRead },
					  { "HZB", VansRenderResourceUsage::SampledRead },
					  { "RayTracingGI", VansRenderResourceUsage::SampledRead },
					  { "CascadeShadowDepth", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowAtlas", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowMeta", VansRenderResourceUsage::StorageRead } },
					{ { "GIData", VansRenderResourceUsage::StorageWrite } },
					{ "SSGI", "GI SH paths" } },
				{ VansRenderPassNames::SSR, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "HZB", VansRenderResourceUsage::SampledRead },
					  { "SceneColor", VansRenderResourceUsage::SampledRead } },
					{ { "SSR", VansRenderResourceUsage::StorageWrite } },
					{ "SSR" } },
				{ VansRenderPassNames::VolumetricFog, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "Depth", VansRenderResourceUsage::SampledRead },
					  { "LightBuffers", VansRenderResourceUsage::StorageRead },
					  { "TileLightLists", VansRenderResourceUsage::StorageRead },
					  { "PunctualShadowAtlas", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowMeta", VansRenderResourceUsage::StorageRead } },
					{ { "VolumetricFog", VansRenderResourceUsage::StorageWrite } },
					{ "Volumetric fog" } },
				{ VansRenderPassNames::CloudRayMarch, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "Atmosphere", VansRenderResourceUsage::StorageRead } },
					{ { "CloudRayMarch", VansRenderResourceUsage::StorageWrite } },
					{ "Cloud ray marching" } },
				{ VansRenderPassNames::DeferredSkybox, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "GBuffer", VansRenderResourceUsage::SampledRead },
					  { "HZB", VansRenderResourceUsage::SampledRead },
					  { "SSAO", VansRenderResourceUsage::SampledRead },
					  { "ScreenSpaceShadow", VansRenderResourceUsage::SampledRead },
					  { "GIData", VansRenderResourceUsage::SampledRead },
					  { "SSR", VansRenderResourceUsage::SampledRead },
					  { "VolumetricFog", VansRenderResourceUsage::SampledRead },
					  { "CloudRayMarch", VansRenderResourceUsage::SampledRead },
					  { "TileLightLists", VansRenderResourceUsage::StorageRead },
					  { "PunctualShadowAtlas", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowMeta", VansRenderResourceUsage::StorageRead } },
					{ { "SceneColor", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Deferred lighting", "Skybox" } },
				{ VansRenderPassNames::ForwardOpaqueAfterDeferred, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::HasForwardOpaqueAfterDeferred,
					{ { "SceneColor", VansRenderResourceUsage::ColorAttachmentWrite },
					  { "Depth", VansRenderResourceUsage::DepthStencilAttachmentRead },
					  { "PunctualShadowAtlas", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowMeta", VansRenderResourceUsage::StorageRead } },
					{ { "SceneColor", VansRenderResourceUsage::ColorAttachmentWrite },
					  { "Depth", VansRenderResourceUsage::DepthStencilAttachmentWrite } },
					{ "Forward opaque after deferred" } },
				{ VansRenderPassNames::WaterWaveCompute, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::HasWater,
					{ { "WaterWaveInputs", VansRenderResourceUsage::StorageRead } },
					{ { "WaterWaveSpectrum", VansRenderResourceUsage::StorageWrite },
					  { "WaterWaveDerivatives", VansRenderResourceUsage::StorageWrite },
					  { "WaterFlowMap", VansRenderResourceUsage::StorageWrite } },
					{ "Water FFT/wave compute", "Water flow map" } },
				{ VansRenderPassNames::WaterGBuffer, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::HasWater,
					{ { "Depth", VansRenderResourceUsage::DepthStencilAttachmentRead },
					  { "WaterWaveSpectrum", VansRenderResourceUsage::SampledRead },
					  { "WaterWaveDerivatives", VansRenderResourceUsage::SampledRead },
					  { "WaterFlowMap", VansRenderResourceUsage::SampledRead } },
					{ { "WaterGBuffer", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Water GBuffer tested against main scene depth" } },
				{ VansRenderPassNames::WaterPreCompute, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::HasWater,
					{ { "SceneColor", VansRenderResourceUsage::SampledRead },
					  { "HZB", VansRenderResourceUsage::SampledRead },
					  { "WaterGBuffer", VansRenderResourceUsage::SampledRead } },
					{ { "WaterThickness", VansRenderResourceUsage::StorageWrite },
					  { "WaterSSR", VansRenderResourceUsage::StorageWrite },
					  { "WaterRefractionData", VansRenderResourceUsage::StorageWrite },
					  { "WaterVolumeRaw", VansRenderResourceUsage::StorageWrite },
					  { "WaterVolumeFiltered", VansRenderResourceUsage::StorageWrite } },
					{ "PBRWater refraction data, volume integration/filter, SSR and caustics" } },
				{ VansRenderPassNames::HairVisibility, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "HairGeometry", VansRenderResourceUsage::SampledRead } },
					{ { "HairOIT", VansRenderResourceUsage::StorageWrite } },
					{ "Hair visibility", "Hair OIT buffers" } },
				{ VansRenderPassNames::HairLighting, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "HairOIT", VansRenderResourceUsage::StorageRead },
					  { "GBuffer", VansRenderResourceUsage::SampledRead } },
					{ { "HairLighting", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Hair lighting" } },
				{ VansRenderPassNames::DepthOfFieldPrepare, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "SceneColor", VansRenderResourceUsage::SampledRead } },
					{ { "DepthOfField", VansRenderResourceUsage::StorageWrite } },
					{ "Depth of field" } },
				{ VansRenderPassNames::TransparentSceneColorPrepare, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneColor", VansRenderResourceUsage::TransferSrc },
					  { "DepthOfField", VansRenderResourceUsage::TransferSrc } },
					{ { "SceneColor", VansRenderResourceUsage::TransferDst },
					  { "OpaqueSceneColor", VansRenderResourceUsage::TransferDst } },
					{ "Resolve opaque DOF into SceneColor", "Refresh transmission/refraction snapshot" } },
				{ VansRenderPassNames::TransparentPostProcess, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneColor", VansRenderResourceUsage::SampledRead },
					  { "OpaqueSceneColor", VansRenderResourceUsage::SampledRead },
					  { "TileLightLists", VansRenderResourceUsage::StorageRead },
					  { "Depth", VansRenderResourceUsage::DepthStencilAttachmentRead },
					  { "PunctualShadowAtlas", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowMeta", VansRenderResourceUsage::StorageRead } },
					{ { "SceneColor", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Hair composite", "Transparent rendering", "Particle rendering", "Transmission/refraction",
					  { "Water composite", VansRenderPassCondition::HasWater } } },
				{ VansRenderPassNames::ExposureBloom, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "SceneColor", VansRenderResourceUsage::SampledRead } },
					{ { "Exposure", VansRenderResourceUsage::StorageWrite },
					  { "FSRExposure", VansRenderResourceUsage::StorageWrite },
					  { "Bloom", VansRenderResourceUsage::StorageWrite } },
					{ "Exposure", "Bloom" } },
				{ VansRenderPassNames::FSRUpscale, VansRenderQueueClass::Compute, true, false, VansRenderPassCondition::Always,
					{ { "SceneColor", VansRenderResourceUsage::SampledRead },
					  { "OpaqueSceneColor", VansRenderResourceUsage::SampledRead },
					  { "Depth", VansRenderResourceUsage::SampledRead },
					  { "MotionVector", VansRenderResourceUsage::SampledRead },
					  { "FSRExposure", VansRenderResourceUsage::SampledRead } },
					{ { "FSRReactiveMask", VansRenderResourceUsage::StorageWrite },
					  { "FSRTransparencyCompositionMask", VansRenderResourceUsage::StorageWrite },
					  { "FSRHDRColor", VansRenderResourceUsage::StorageWrite } },
					{ "FSR mask generation", "FSR upscale" } },
				{ VansRenderPassNames::DisplayPostProcess, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "FSRHDRColor", VansRenderResourceUsage::SampledRead },
					  { "Exposure", VansRenderResourceUsage::SampledRead },
					  { "Bloom", VansRenderResourceUsage::SampledRead } },
					{ { "FinalDisplayColor", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Exposure", "Bloom composite", "Tone mapping", "Color grading" } },
				{ VansRenderPassNames::RuntimeUI, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "FinalDisplayColor", VansRenderResourceUsage::SampledRead } },
					{ { "FinalDisplayColor", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Scene UI rendering" } },
				{ VansRenderPassNames::ReflectionProbeBakeQueue, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneColor", VansRenderResourceUsage::SampledRead },
					  { "ShadowMaps", VansRenderResourceUsage::SampledRead } },
					{ { "ReflectionProbeCache", VansRenderResourceUsage::StorageWrite } },
					{ "Reflection probe runtime capture", "Reflection probe prefilter", "Reflection probe cache" } },
			};
			return catalog;
		}
	}

	bool VansRenderPassCatalog::IsPassEnabled(
		VansRenderPassCondition condition,
		const VansScene& scene)
	{
		switch (condition)
		{
		case VansRenderPassCondition::Always:
			return true;
		case VansRenderPassCondition::HasPunctualShadowJobs:
			return scene.HasPunctualShadowJobs();
		case VansRenderPassCondition::HasWater:
			return scene.HasWaterNodes();
		case VansRenderPassCondition::HasDecal:
			return scene.HasDecalNodes();
		case VansRenderPassCondition::HasForwardOpaqueAfterDeferred:
			return scene.HasForwardOpaqueAfterDeferredNodes();
		}

		return true;
	}

	bool VansRenderPassCatalog::IsKnownPassName(const char* passName)
	{
		if (passName == nullptr)
		{
			return false;
		}

		const auto& catalog = GetCompatibilityCatalog();
		for (const auto& entry : catalog)
		{
			if (std::strcmp(entry.name, passName) == 0)
			{
				return true;
			}
		}

		return false;
	}

	bool VansRenderPassCatalog::AuditAsyncMigrationContracts(std::vector<std::string>& outErrors)
	{
		outErrors.clear();
		const auto& catalog = GetCompatibilityCatalog();
		auto findPass = [&](const char* passName) -> const VansRenderPassCatalogEntry*
		{
			for (const auto& entry : catalog)
			{
				if (std::strcmp(entry.name, passName) == 0)
					return &entry;
			}
			return nullptr;
		};
		auto requireAccess = [&](const char* passName, const char* resourceName, bool write)
		{
			const VansRenderPassCatalogEntry* pass = findPass(passName);
			if (pass == nullptr)
			{
				outErrors.emplace_back(std::string("missing pass: ") + passName);
				return;
			}
			const auto& accesses = write ? pass->writes : pass->reads;
			for (const auto& access : accesses)
			{
				if (access.name == resourceName)
					return;
			}
			outErrors.emplace_back(std::string(passName) + (write ? " missing write: " : " missing read: ") + resourceName);
		};

		for (const char* passName : {
			VansRenderPassNames::VegetationCompute,
			VansRenderPassNames::MainCameraHiZCull,
			VansRenderPassNames::TileLightBuild,
			VansRenderPassNames::CloudRayMarch,
			VansRenderPassNames::RayTracing,
			VansRenderPassNames::GIData })
		{
			const VansRenderPassCatalogEntry* pass = findPass(passName);
			if (pass == nullptr || !pass->allowAsyncCompute)
				outErrors.emplace_back(std::string("async pass is not marked allowAsyncCompute: ") + passName);
		}

		requireAccess(VansRenderPassNames::VegetationCompute, "VegetationDrawData", true);
		requireAccess(VansRenderPassNames::CascadeShadow, "VegetationDrawData", false);
		requireAccess(VansRenderPassNames::GBuffer, "VegetationDrawData", false);
		requireAccess(VansRenderPassNames::MainCameraHiZCull, "OcclusionHZB", false);
		requireAccess(VansRenderPassNames::MainCameraHiZCull, "MainCameraVisibilityReadback", true);
		requireAccess(VansRenderPassNames::TileLightBuild, "TileLightLists", true);
		requireAccess(VansRenderPassNames::VolumetricFog, "TileLightLists", false);
		requireAccess(VansRenderPassNames::DeferredSkybox, "TileLightLists", false);
		requireAccess(VansRenderPassNames::TransparentPostProcess, "TileLightLists", false);
		requireAccess(VansRenderPassNames::CloudRayMarch, "CloudRayMarch", true);
		requireAccess(VansRenderPassNames::DeferredSkybox, "CloudRayMarch", false);
		requireAccess(VansRenderPassNames::GIData, "HZB", false);
		requireAccess(VansRenderPassNames::GIData, "GIData", true);
		requireAccess(VansRenderPassNames::DeferredSkybox, "GIData", false);
		return outErrors.empty();
	}

	void VansRenderPassCatalog::GetPreservedFeatureAuditList(
		const VansScene& scene,
		std::vector<std::string>& outRequiredFeatures,
		std::vector<std::string>& outConditionallyDisabledFeatures)
	{
		outRequiredFeatures.clear();
		outConditionallyDisabledFeatures.clear();

		const auto& catalog = GetCompatibilityCatalog();
		for (const auto& entry : catalog)
		{
			const bool passEnabled = IsPassEnabled(entry.condition, scene);
			for (const auto& feature : entry.preservedFeatures)
			{
				if (passEnabled && IsPassEnabled(feature.condition, scene))
				{
					outRequiredFeatures.emplace_back(feature.name);
				}
				else
				{
					outConditionallyDisabledFeatures.emplace_back(feature.name);
				}
			}
		}
	}

	void VansRenderPassCatalog::BuildCompatibilityFramePlan(
		VansRenderFramePlan& outPlan,
		VansScene& scene,
		uint64_t frameNumber,
		bool asyncComputeEnabled)
	{
		outPlan.Begin(frameNumber);

		const auto& catalog = GetCompatibilityCatalog();

		for (const auto& entry : catalog)
		{
			outPlan.AddPass(MakeNodeDesc(entry, scene, asyncComputeEnabled));
		}
	}
}
