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

		VansRenderPassNodeDesc MakeNodeDesc(
			const VansRenderPassCatalogEntry& entry,
			const VansScene& scene)
		{
			VansRenderPassNodeDesc desc{};
			desc.name = entry.name;
			desc.queue = entry.queue;
			desc.resizeDependent = entry.resizeDependent;
			desc.allowAsyncCompute = entry.allowAsyncCompute;
			desc.enabled = VansRenderPassCatalog::IsPassEnabled(entry.condition, scene);
			desc.reads = entry.reads;
			desc.writes = entry.writes;
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
					{ { "VegetationInputs", VansRenderResourceUsage::StorageRead } },
					{ { "VegetationIndirectArgs", VansRenderResourceUsage::StorageWrite } },
					{ "GPU vegetation wind/cull/indirect" } },
				{ VansRenderPassNames::CascadeShadow, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneGeometry", VansRenderResourceUsage::SampledRead } },
					{ { "CascadeShadowDepth", VansRenderResourceUsage::DepthStencilAttachmentWrite } },
					{ "Cascaded directional shadows" } },
				{ VansRenderPassNames::PunctualShadow, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::HasPunctualShadowJobs,
					{ { "SceneGeometry", VansRenderResourceUsage::SampledRead } },
					{ { "PunctualShadowAtlas", VansRenderResourceUsage::DepthStencilAttachmentWrite } },
					{ "Point and spot shadows" } },
				{ VansRenderPassNames::HairDeepOpacity, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "HairGeometry", VansRenderResourceUsage::SampledRead } },
					{ { "HairDeepOpacity", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Hair deep opacity" } },
				{ VansRenderPassNames::MotionVector, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneGeometry", VansRenderResourceUsage::SampledRead } },
					{ { "MotionVectors", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Motion vectors" } },
				{ VansRenderPassNames::GBuffer, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneGeometry", VansRenderResourceUsage::SampledRead } },
					{ { "Normal", VansRenderResourceUsage::ColorAttachmentWrite },
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
					{ { "HZB", VansRenderResourceUsage::StorageWrite } },
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
				{ VansRenderPassNames::RayTracing, VansRenderQueueClass::Compute, false, false, VansRenderPassCondition::Always,
					{ { "TLAS", VansRenderResourceUsage::AccelerationStructureBuildRead },
					  { "GBuffer", VansRenderResourceUsage::SampledRead } },
					{ { "RayTracingGI", VansRenderResourceUsage::StorageWrite } },
					{ "BLAS/TLAS", "Ray tracing dispatch", "GI/probe integration" } },
				{ VansRenderPassNames::GIData, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "GBuffer", VansRenderResourceUsage::SampledRead },
					  { "RayTracingGI", VansRenderResourceUsage::SampledRead },
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
					  { "PunctualShadowAtlas", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowMeta", VansRenderResourceUsage::StorageRead } },
					{ { "VolumetricFog", VansRenderResourceUsage::StorageWrite } },
					{ "Volumetric fog" } },
				{ VansRenderPassNames::CloudRayMarch, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "Atmosphere", VansRenderResourceUsage::StorageRead } },
					{ { "CloudRayMarch", VansRenderResourceUsage::StorageWrite } },
					{ "Cloud ray marching" } },
				{ VansRenderPassNames::ExposureBloom, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::Always,
					{ { "SceneColor", VansRenderResourceUsage::SampledRead } },
					{ { "Exposure", VansRenderResourceUsage::StorageWrite },
					  { "Bloom", VansRenderResourceUsage::StorageWrite } },
					{ "Exposure", "Bloom" } },
				{ VansRenderPassNames::DeferredSkybox, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "GBuffer", VansRenderResourceUsage::SampledRead },
					  { "HZB", VansRenderResourceUsage::SampledRead },
					  { "ScreenSpaceEffects", VansRenderResourceUsage::SampledRead },
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
					  { "WaterMicroSlopeSpectrum", VansRenderResourceUsage::StorageWrite } },
					{ "Water FFT/wave compute", "Water spectral micro slopes" } },
				{ VansRenderPassNames::WaterGBuffer, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::HasWater,
					{ { "Depth", VansRenderResourceUsage::DepthStencilAttachmentRead },
					  { "WaterWaveSpectrum", VansRenderResourceUsage::SampledRead },
					  { "WaterMicroSlopeSpectrum", VansRenderResourceUsage::SampledRead } },
					{ { "WaterGBuffer", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Water GBuffer tested against main scene depth" } },
				{ VansRenderPassNames::WaterPreCompute, VansRenderQueueClass::Compute, true, true, VansRenderPassCondition::HasWater,
					{ { "SceneColor", VansRenderResourceUsage::SampledRead },
					  { "HZB", VansRenderResourceUsage::SampledRead },
					  { "WaterGBuffer", VansRenderResourceUsage::SampledRead } },
					{ { "WaterThickness", VansRenderResourceUsage::StorageWrite },
					  { "WaterSSR", VansRenderResourceUsage::StorageWrite },
					  { "WaterRefraction", VansRenderResourceUsage::StorageWrite } },
					{ "Water SSR/composite/precompute" } },
				{ VansRenderPassNames::HairVisibility, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "HairGeometry", VansRenderResourceUsage::SampledRead } },
					{ { "HairOIT", VansRenderResourceUsage::StorageWrite } },
					{ "Hair visibility", "Hair OIT buffers" } },
				{ VansRenderPassNames::HairLighting, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "HairOIT", VansRenderResourceUsage::StorageRead },
					  { "GBuffer", VansRenderResourceUsage::SampledRead } },
					{ { "HairLighting", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Hair lighting" } },
				{ VansRenderPassNames::TransparentPostProcess, VansRenderQueueClass::Graphics, true, false, VansRenderPassCondition::Always,
					{ { "SceneColor", VansRenderResourceUsage::SampledRead },
					  { "OpaqueSceneColor", VansRenderResourceUsage::SampledRead },
					  { "Depth", VansRenderResourceUsage::DepthStencilAttachmentRead },
					  { "PunctualShadowAtlas", VansRenderResourceUsage::SampledRead },
					  { "PunctualShadowMeta", VansRenderResourceUsage::StorageRead } },
					{ { "PostProcessOutput", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "Hair composite", "Transparent rendering", "Particle rendering", "Transmission/refraction",
					  { "Water composite", VansRenderPassCondition::HasWater }, "Post-process chain" } },
				{ VansRenderPassNames::FSRRuntimeUI, VansRenderQueueClass::Compute, true, false, VansRenderPassCondition::Always,
					{ { "PostProcessOutput", VansRenderResourceUsage::SampledRead } },
					{ { "FSROutput", VansRenderResourceUsage::StorageWrite },
					  { "SceneUI", VansRenderResourceUsage::ColorAttachmentWrite } },
					{ "FSR", "Scene UI rendering" } },
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
		uint64_t frameNumber)
	{
		outPlan.Begin(frameNumber);

		const auto& catalog = GetCompatibilityCatalog();

		for (const auto& entry : catalog)
		{
			outPlan.AddPass(MakeNodeDesc(entry, scene));
		}
	}
}
