#include "VansRenderFrame.h"

#include <algorithm>
#include <unordered_set>

namespace
{
	bool HasLightPayload(const VansGraphics::VansRenderLightFrameData& light)
	{
		return light.prepared || light.punctualShadowMapWidth != 0 || light.frameSequence != 0.0f ||
			!light.directionalLights.empty() || !light.pointLights.empty() ||
			!light.spotLights.empty() || !light.rectLights.empty();
	}

	bool HasPunctualShadowInput(
		const VansGraphics::VansRenderPunctualShadowFrameInput& input)
	{
		return input.prepared || !input.lights.empty() || !input.casters.empty();
	}

	bool ValidatePunctualShadowInput(
		const VansGraphics::VansRenderPunctualShadowFrameInput& input,
		const VansGraphics::VansRenderLightFrameData& light)
	{
		using namespace VansGraphics;
		std::unordered_set<std::uint32_t> stableLightIds;
		stableLightIds.reserve(input.lights.size());
		for (const VansPunctualShadowLightInput& shadowLight : input.lights)
		{
			if (shadowLight.stableLightId == 0 ||
				!stableLightIds.insert(shadowLight.stableLightId).second)
			{
				return false;
			}
			switch (shadowLight.type)
			{
			case VansPunctualShadowLightType::Point:
				if (shadowLight.gpuLightIndex >= light.pointLights.size()) return false;
				break;
			case VansPunctualShadowLightType::Spot:
				if (shadowLight.gpuLightIndex >= light.spotLights.size()) return false;
				break;
			case VansPunctualShadowLightType::Rect:
				if (shadowLight.gpuLightIndex >= light.rectLights.size()) return false;
				break;
			default:
				return false;
			}
		}

		std::unordered_set<VansRenderProxyHandle, VansRenderProxyHandleHash> casterHandles;
		casterHandles.reserve(input.casters.size());
		for (const VansRenderPunctualShadowCasterInput& caster : input.casters)
		{
			if (!caster.proxy.IsValid() || !casterHandles.insert(caster.proxy).second)
				return false;
		}
		return true;
	}
}

VansGraphics::VansRenderFrameBuilder::VansRenderFrameBuilder(
	VansRenderFrameId frameId,
	VansLogicFrameId sourceLogicFrameId,
	VansSurfaceEpoch surfaceEpoch)
	: m_FrameId(frameId),
	  m_SourceLogicFrameId(sourceLogicFrameId),
	  m_SurfaceEpoch(surfaceEpoch)
{
}

bool VansGraphics::VansRenderFrameBuilder::SetView(VansRenderViewSnapshot view)
{
	if (m_Finalized || m_View.has_value() ||
		view.viewportWidth == 0 || view.viewportHeight == 0)
	{
		return false;
	}

	m_View = std::move(view);
	return true;
}

bool VansGraphics::VansRenderFrameBuilder::SetTiming(
	VansRenderFrameTimingSnapshot timing)
{
	if (m_Finalized || m_Timing.has_value() ||
		timing.elapsedSeconds < 0.0 || timing.deltaSeconds < 0.0 ||
		timing.renderDeltaSeconds < 0.0)
	{
		return false;
	}

	m_Timing = timing;
	return true;
}

bool VansGraphics::VansRenderFrameBuilder::SetScene(
	VansRenderSceneFrameSnapshot scene)
{
	const auto validMaterialBuffer = [](const VansRenderMaterialBufferSnapshot& buffer)
	{
		return buffer.elementStride > 0 &&
			(buffer.bytes.size() % buffer.elementStride) == 0;
	};
	const bool validMaterialFrame = scene.materials.prepared &&
		validMaterialBuffer(scene.materials.pbr) &&
		validMaterialBuffer(scene.materials.cloth) &&
		validMaterialBuffer(scene.materials.treeLeaf) &&
		validMaterialBuffer(scene.materials.skin) &&
		validMaterialBuffer(scene.materials.custom);
	if (m_Finalized || m_Scene.has_value() ||
		(!scene.sceneReady &&
			(HasLightPayload(scene.light) || HasPunctualShadowInput(scene.punctualShadow) ||
				!scene.transforms.empty() ||
				!scene.animations.empty() || !scene.cloth.empty() ||
				!scene.particles.empty() || !scene.rectLightVideos.empty() ||
				scene.materials.prepared || scene.atmosphere.prepared ||
				scene.gi.prepared || scene.postProcess.prepared ||
				!scene.mainCameraCullInputs.empty() ||
				!scene.punctualShadowJobs.empty() || scene.features.Any())) ||
		(scene.sceneReady &&
			(!scene.light.IsComplete() || !scene.punctualShadow.IsComplete() ||
				!scene.gi.prepared || !scene.postProcess.prepared ||
				!validMaterialFrame ||
				scene.features.hasPunctualShadowJobs ||
				!ValidatePunctualShadowInput(scene.punctualShadow, scene.light))) ||
		std::any_of(
			scene.transforms.begin(),
			scene.transforms.end(),
			[](const VansRenderTransformFrameData& transform)
			{
				return !transform.proxy.IsValid();
			}) ||
		std::any_of(
			scene.mainCameraCullInputs.begin(),
			scene.mainCameraCullInputs.end(),
			[](const VansRenderMainCameraCullInput& input)
			{
				return !input.proxy.IsValid();
			}) ||
		!scene.punctualShadowJobs.empty())
		return false;

	m_Scene = std::move(scene);
	return true;
}

std::optional<VansGraphics::VansRenderFramePacket>
VansGraphics::VansRenderFrameBuilder::Finalize() &&
{
	if (m_Finalized || !m_View.has_value() || !m_Timing.has_value() ||
		!m_Scene.has_value())
		return std::nullopt;

	m_Finalized = true;
	return VansRenderFramePacket(
		m_FrameId,
		m_SourceLogicFrameId,
		m_SurfaceEpoch,
		std::move(*m_View),
		*m_Timing,
		std::move(*m_Scene));
}
