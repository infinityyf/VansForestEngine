#include "VansRenderPassCatalog.h"

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
				|| std::strcmp(passName, VansRenderPassNames::HZB) == 0
				|| std::strcmp(passName, VansRenderPassNames::RayTracing) == 0
				|| std::strcmp(passName, VansRenderPassNames::GIData) == 0;
		}

		VansRenderPassNodeDesc MakeNodeDesc(
			const VansRenderPassCatalogEntry& entry,
			const VansRenderFeatureFrameFlags& features,
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
			desc.enabled = VansRenderPassCatalog::IsPassEnabled(entry.condition, features);
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
				if (VansRenderPassCatalog::IsPassEnabled(feature.condition, features))
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
					{ { "PunctualShadowAtlas0", VansRenderResourceUsage::DepthStencilAttachmentWrite },
					  { "PunctualShadowAtlas1", VansRenderResourceUsage::DepthStencilAttachmentWrite } },
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
				{ VansRenderPassNames::GBuffer, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneGeometry", VansRenderResourceUsage::SampledRead },
					  { "VegetationDrawData", VansRenderResourceUsage::IndirectArgumentRead } },
					{ { "Normal", VansRenderResourceUsage::ColorAttachmentWrite },
					  { "GBuffer", VansRenderResourceUsage::ColorAttachmentWrite },
					  { "MaterialBuffers", VansRenderResourceUsage::ColorAttachmentWrite },
					  { "MotionVector", VansRenderResourceUsage::ColorAttachmentWrite },
					  { "Depth", VansRenderResourceUsage::DepthStencilAttachmentWrite } },
					{ "Deferred GBuffer", "GBuffer normal", "GBuffer material buffers", "GBuffer depth",
					  "Existing material types and shader pass coverage" } },
				{ VansRenderPassNames::SkyMotionVector, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "Depth", VansRenderResourceUsage::DepthStencilAttachmentRead } },
					{ { "MotionVector", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Sky motion vectors on uncovered background pixels" } },
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
					{ { "PunctualShadowAtlas0", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowAtlas1", VansRenderResourceUsage::SampledRead } },
					{ { "PunctualShadowDebugPreview", VansRenderResourceUsage::StorageWrite } },
					{} },
				{ VansRenderPassNames::ScreenSpaceShadow, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "Depth", VansRenderResourceUsage::SampledRead },
					  { "CascadeShadowDepth", VansRenderResourceUsage::SampledRead } },
					{ { "CascadeShadowMinMax", VansRenderResourceUsage::StorageWrite },
					  { "ScreenSpaceShadow", VansRenderResourceUsage::StorageWrite } },
					{ "Screen-space shadows" } },
				{ VansRenderPassNames::ScreenSpaceEffects, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "Normal", VansRenderResourceUsage::SampledRead },
					  { "GBuffer", VansRenderResourceUsage::SampledRead },
					  { "Depth", VansRenderResourceUsage::SampledRead } },
					{ { "SSAORaw", VansRenderResourceUsage::StorageWrite } },
					{ "SSAO raw" } },
				{ VansRenderPassNames::SSAOFilter, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "SSAORaw", VansRenderResourceUsage::SampledRead },
					  { "Depth", VansRenderResourceUsage::SampledRead } },
					{ { "SSAO", VansRenderResourceUsage::StorageWrite } },
					{ "SSAO bilateral filter" } },
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
					  { "PunctualShadowAtlas0", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowAtlas1", VansRenderResourceUsage::SampledRead },
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
					  { "PunctualShadowAtlas0", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowAtlas1", VansRenderResourceUsage::SampledRead },
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
					  { "PunctualShadowAtlas0", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowAtlas1", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowMeta", VansRenderResourceUsage::StorageRead } },
					{ { "SceneColor", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Deferred lighting", "Skybox" } },
				{ VansRenderPassNames::ForwardOpaqueAfterDeferred, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::HasForwardOpaqueAfterDeferred,
					{ { "SceneColor", VansRenderResourceUsage::ColorAttachmentWrite },
					  { "Depth", VansRenderResourceUsage::DepthStencilAttachmentRead },
					  { "PunctualShadowAtlas0", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowAtlas1", VansRenderResourceUsage::SampledRead },
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
					  { "PunctualShadowAtlas0", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowAtlas1", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowMeta", VansRenderResourceUsage::StorageRead } },
					{ { "SceneColor", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Hair composite", "Transparent rendering", "Particle rendering", "Transmission/refraction",
					  { "Water composite", VansRenderPassCondition::HasWater } } },
				{ VansRenderPassNames::ExposureBloom, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "SceneColor", VansRenderResourceUsage::SampledRead } },
					{ { "Exposure", VansRenderResourceUsage::StorageWrite },
					  { "UpscalerExposure", VansRenderResourceUsage::StorageWrite },
					  { "Bloom", VansRenderResourceUsage::StorageWrite } },
					{ "Exposure", "Bloom" } },
				{ VansRenderPassNames::TemporalUpscale, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneColor", VansRenderResourceUsage::SampledRead },
					  { "OpaqueSceneColor", VansRenderResourceUsage::SampledRead },
					  { "Depth", VansRenderResourceUsage::SampledRead },
					  { "MotionVector", VansRenderResourceUsage::SampledRead },
					  { "UpscalerExposure", VansRenderResourceUsage::SampledRead } },
					{ { "FSRReactiveMask", VansRenderResourceUsage::StorageWrite },
					  { "FSRTransparencyCompositionMask", VansRenderResourceUsage::StorageWrite },
					  { "UpscalerHDRColor", VansRenderResourceUsage::StorageWrite } },
					{ "Temporal upscaler auxiliary masks", "Temporal upscale dispatch" } },
				{ VansRenderPassNames::DisplayPostProcess, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "UpscalerHDRColor", VansRenderResourceUsage::SampledRead },
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
		const VansRenderFeatureFrameFlags& features)
	{
		switch (condition)
		{
		case VansRenderPassCondition::Always:
			return true;
		case VansRenderPassCondition::HasPunctualShadowJobs:
			return features.hasPunctualShadowJobs;
		case VansRenderPassCondition::HasWater:
			return features.hasWater;
		case VansRenderPassCondition::HasDecal:
			return features.hasDecal;
		case VansRenderPassCondition::HasForwardOpaqueAfterDeferred:
			return features.hasForwardOpaqueAfterDeferred;
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
			VansRenderPassNames::HZB,
			VansRenderPassNames::RayTracing,
			VansRenderPassNames::GIData })
		{
			const VansRenderPassCatalogEntry* pass = findPass(passName);
			if (pass == nullptr || !pass->allowAsyncCompute)
				outErrors.emplace_back(std::string("async pass is not marked allowAsyncCompute: ") + passName);
		}

		const VansRenderPassCatalogEntry* temporalUpscale =
			findPass(VansRenderPassNames::TemporalUpscale);
		if (temporalUpscale == nullptr)
		{
			outErrors.emplace_back("missing pass: Temporal Upscale");
		}
		else
		{
			if (temporalUpscale->queue != VansRenderQueueClass::Graphics)
				outErrors.emplace_back("Temporal Upscale must remain on the graphics queue");
			if (temporalUpscale->allowAsyncCompute)
				outErrors.emplace_back("Temporal Upscale must not be migrated to async compute");
		}

		auto findPassIndex = [&](const char* passName)
		{
			for (std::size_t index = 0; index < catalog.size(); ++index)
			{
				if (std::strcmp(catalog[index].name, passName) == 0)
					return index;
			}
			return catalog.size();
		};
		const std::size_t transparentIndex =
			findPassIndex(VansRenderPassNames::TransparentPostProcess);
		const std::size_t exposureIndex = findPassIndex(VansRenderPassNames::ExposureBloom);
		const std::size_t temporalIndex = findPassIndex(VansRenderPassNames::TemporalUpscale);
		const std::size_t displayIndex = findPassIndex(VansRenderPassNames::DisplayPostProcess);
		if (!(transparentIndex < exposureIndex && exposureIndex < temporalIndex &&
			temporalIndex < displayIndex))
		{
			outErrors.emplace_back(
				"Temporal Upscale must execute after transparent/exposure work and before display post-process");
		}

		requireAccess(VansRenderPassNames::VegetationCompute, "VegetationDrawData", true);
		requireAccess(VansRenderPassNames::VegetationCompute, "OcclusionHZB", false);
		requireAccess(VansRenderPassNames::CascadeShadow, "VegetationDrawData", false);
		requireAccess(VansRenderPassNames::GBuffer, "VegetationDrawData", false);
		requireAccess(VansRenderPassNames::GBuffer, "MotionVector", true);
		requireAccess(VansRenderPassNames::SkyMotionVector, "Depth", false);
		requireAccess(VansRenderPassNames::SkyMotionVector, "MotionVector", true);
		requireAccess(VansRenderPassNames::MainCameraHiZCull, "OcclusionHZB", false);
		requireAccess(VansRenderPassNames::MainCameraHiZCull, "MainCameraVisibilityReadback", true);
		requireAccess(VansRenderPassNames::TileLightBuild, "TileLightLists", true);
		requireAccess(VansRenderPassNames::VolumetricFog, "TileLightLists", false);
		requireAccess(VansRenderPassNames::DeferredSkybox, "TileLightLists", false);
		requireAccess(VansRenderPassNames::TransparentPostProcess, "TileLightLists", false);
		requireAccess(VansRenderPassNames::CloudRayMarch, "CloudRayMarch", true);
		requireAccess(VansRenderPassNames::DeferredSkybox, "CloudRayMarch", false);
		requireAccess(VansRenderPassNames::ScreenSpaceEffects, "SSAORaw", true);
		requireAccess(VansRenderPassNames::SSAOFilter, "SSAORaw", false);
		requireAccess(VansRenderPassNames::SSAOFilter, "SSAO", true);
		requireAccess(VansRenderPassNames::DeferredSkybox, "SSAO", false);
		requireAccess(VansRenderPassNames::ScreenSpaceShadow, "CascadeShadowMinMax", true);
		requireAccess(VansRenderPassNames::GIData, "HZB", false);
		requireAccess(VansRenderPassNames::GIData, "GIData", true);
		requireAccess(VansRenderPassNames::DeferredSkybox, "GIData", false);
		requireAccess(VansRenderPassNames::TemporalUpscale, "SceneColor", false);
		requireAccess(VansRenderPassNames::TemporalUpscale, "Depth", false);
		requireAccess(VansRenderPassNames::TemporalUpscale, "MotionVector", false);
		requireAccess(VansRenderPassNames::TemporalUpscale, "UpscalerExposure", false);
		requireAccess(VansRenderPassNames::TemporalUpscale, "UpscalerHDRColor", true);
		requireAccess(VansRenderPassNames::DisplayPostProcess, "UpscalerHDRColor", false);
		return outErrors.empty();
	}

	void VansRenderPassCatalog::GetPreservedFeatureAuditList(
		const VansRenderFeatureFrameFlags& features,
		std::vector<std::string>& outRequiredFeatures,
		std::vector<std::string>& outConditionallyDisabledFeatures)
	{
		outRequiredFeatures.clear();
		outConditionallyDisabledFeatures.clear();

		const auto& catalog = GetCompatibilityCatalog();
		for (const auto& entry : catalog)
		{
			const bool passEnabled = IsPassEnabled(entry.condition, features);
			for (const auto& feature : entry.preservedFeatures)
			{
				if (passEnabled && IsPassEnabled(feature.condition, features))
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
		const VansRenderFeatureFrameFlags& features,
		uint64_t frameNumber,
		bool asyncComputeEnabled)
	{
		outPlan.Begin(frameNumber);

		const auto& catalog = GetCompatibilityCatalog();

		for (const auto& entry : catalog)
		{
			outPlan.AddPass(MakeNodeDesc(entry, features, asyncComputeEnabled));
		}
	}
}
