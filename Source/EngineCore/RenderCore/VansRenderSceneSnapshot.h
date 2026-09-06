#pragma once

#include "VansRenderBounds.h"
#include "VansRenderWorld.h"
#include "BRDFData/VansLightFrameTypes.h"
#include "BRDFData/VansPBR.h"
#include "ShadowCore/VansPunctualShadowTypes.h"
#include "GICore/VansGISettings.h"
#include "VansPostProcessProfile.h"
#include "../AnimationCore/VansAnimationTypes.h"
#include "../ParticleCore/VansParticleInstanceData.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace VansGraphics
{
	enum class VansMainCameraCullClass : std::uint8_t
	{
		Opaque,
		Hair,
		Transparent,
		ForwardOpaquePreAtmosphere,
		Decal
	};

	// Main-owned project/runtime configuration copied into every immutable frame.
	// Backend visibility state consumes this value and never reads Scene settings.
	struct VansMainCameraHiZCullSettings final
	{
		bool enabled = true;
		bool enableOpaque = true;
		bool enableHair = true;
		bool enableTransparent = false;
		bool enableDecal = true;
		bool enableForwardOpaquePreAtmosphere = true;
		float depthBiasMeters = 0.35f;
		float cameraMotionDisableDistance = 1.0f;
		float cameraMotionDisableAngleRadians = glm::radians(8.0f);
		uint32_t forceVisibleFramesAfterChange = 1;
		uint32_t refreshCulledEveryNFrames = 30;
		float maxScreenCoverageForCull = 0.65f;
	};

	// Main 提取 camera-independent candidate value；frustum/HiZ/history 仍由 RT 决定。
	struct VansRenderMainCameraCullInput final
	{
		VansRenderProxyHandle proxy;
		std::string nodeName;
		VansRenderBounds bounds;
		VansMainCameraCullClass cullClass = VansMainCameraCullClass::Opaque;
		bool hasBounds = false;
	};

	struct VansRenderFeatureFrameFlags final
	{
		bool hasPunctualShadowJobs = false;
		bool hasWater = false;
		bool hasDecal = false;
		bool hasForwardOpaquePreAtmosphere = false;

		bool Any() const
		{
			return hasPunctualShadowJobs || hasWater || hasDecal ||
				hasForwardOpaquePreAtmosphere;
		}
	};

	struct VansRenderPunctualShadowCasterInput final
	{
		VansRenderProxyHandle proxy;
		VansRenderBounds bounds;
		uint32_t shadowCasterMask = 0xffffffffu;
		bool dynamic = false;
		bool hasBounds = false;
	};

	// Main 只发布灯光与 caster 的值输入。Atlas 分配、驻留、dirty 状态、
	// job 生成和提交确认全部由 backend-owned shadow state 处理。
	struct VansRenderPunctualShadowFrameInput final
	{
		VansPunctualShadowCameraData camera;
		std::vector<VansPunctualShadowLightInput> lights;
		std::vector<VansRenderPunctualShadowCasterInput> casters;
		bool prepared = false;

		bool IsComplete() const
		{
			return prepared && camera.viewportWidth > 0 && camera.viewportHeight > 0;
		}
	};

	// Main-owned, structured light values. Backend resolves punctual-shadow meta
	// indices and packs the exact shader ABI only after its shadow state is ready.
	struct VansRenderLightFrameData final
	{
		std::vector<VansDirectionalLight> directionalLights;
		std::vector<VansPointLight> pointLights;
		std::vector<VansSpotLight> spotLights;
		std::vector<VansRectLight> rectLights;
		uint32_t punctualShadowMapWidth = 0;
		float frameSequence = 0.0f;
		bool prepared = false;

		bool IsComplete() const { return prepared && punctualShadowMapWidth > 0; }
	};

	// Main-owned value data for one render-frame transform upload. This is a
	// transport contract, not the Vulkan ModelDataStruct ABI.
	struct VansRenderTransformFrameData final
	{
		VansRenderProxyHandle proxy;
		glm::mat4 modelMatrix{ 1.0f };
		glm::mat4 normalMatrix{ 1.0f };
		glm::vec4 position{ 0.0f };
		glm::vec4 scale{ 1.0f };
		glm::mat4 previousModelMatrix{ 1.0f };
	};

	// Dynamic producer results are copied on Main and addressed by their stable
	// scene-list slot for the lifetime of sceneEpoch.  Scene structural changes
	// are ordered behind a RenderSystem idle barrier, so RT never dereferences
	// gameplay/controller state while Main prepares the next frame.
	struct VansRenderAnimationFrameData final
	{
		std::uint32_t animationNodeIndex = 0;
		BoneMatricesSSBO boneMatrices{};
	};

	struct VansRenderClothFrameData final
	{
		std::uint32_t clothNodeIndex = 0;
		std::vector<std::uint16_t> simulatedVertices;
	};

	struct VansRenderParticleFrameData final
	{
		std::uint32_t particleRenderNodeIndex = UINT32_MAX;
		std::vector<VansParticleInstanceData> instances;
		// 体积粒子为独立的可选帧通道；未启用时始终为空且 feature=false。
		std::vector<VansVolumetricParticleInstanceData> volumetricInstances;
		bool volumetricInjectionEnabled = false;
	};

	struct VansRenderRectLightVideoFrameData final
	{
		std::uint32_t videoRuntimeIndex = 0;
		std::int32_t rectLightLayer = -1;
	};

	struct VansRenderMaterialBufferSnapshot final
	{
		std::uint32_t elementStride = 0;
		std::vector<std::uint8_t> bytes;
	};

	// Material/editor/timeline parameters remain Main-owned.  The published frame
	// carries complete value buffers; only RT writes the mapped Vulkan buffers.
	struct VansRenderMaterialFrameData final
	{
		VansRenderMaterialBufferSnapshot pbr;
		VansRenderMaterialBufferSnapshot cloth;
		VansRenderMaterialBufferSnapshot treeLeaf;
		VansRenderMaterialBufferSnapshot skin;
		VansRenderMaterialBufferSnapshot custom;
		bool rewriteBindlessTextures = false;
		bool prepared = false;
	};

	struct VansRenderGIFrameData final
	{
		VansGISettings settings;
		bool rebuildProbeResources = false;
		bool updateParameters = false;
		bool prepared = false;
	};

	struct VansRenderPostProcessFrameData final
	{
		VansPostProcessParamsGPU params{};
		VansExposureAdaptParamsGPU exposure{};
		VansBloomParamsGPU bloom{};
		VansBloomShapeParamsGPU bloomShape{};
		VansDepthOfFieldParamsGPU depthOfField{};
		bool enableAutoExposure = false;
		bool enableDepthOfField = false;
		bool enableBloom = false;
		bool blurTransmissionBackground = true;
		bool staticParametersDirty = false;
		bool prepared = false;
	};

	// Owned by VansRenderFramePacket after publication. No Scene/RenderNode/Vk
	// pointers or borrowed spans are permitted here.
	struct VansRenderSceneFrameSnapshot final
	{
		std::uint64_t sceneEpoch = 0;
		bool sceneReady = false;
		VansRenderLightFrameData light;
		std::vector<VansRenderTransformFrameData> transforms;
		std::vector<VansRenderAnimationFrameData> animations;
		std::vector<VansRenderClothFrameData> cloth;
		std::vector<VansRenderParticleFrameData> particles;
		std::vector<VansRenderRectLightVideoFrameData> rectLightVideos;
		VansRenderMaterialFrameData materials;
		VansRenderGIFrameData gi;
		VansRenderPostProcessFrameData postProcess;
		VansMainCameraHiZCullSettings mainCameraHiZCullSettings;
		std::vector<VansRenderMainCameraCullInput> mainCameraCullInputs;
		VansRenderPunctualShadowFrameInput punctualShadow;
		// RenderThread 接管快照所有权后可在录制前填充 jobs；Main 发布时必须为空。
		std::vector<VansPunctualShadowRenderJob> punctualShadowJobs;
		VansRenderFeatureFrameFlags features;
	};
}
