#include "VansVKDevice.h"

#include "../BRDFData/VansLight.h"
#include "../VansRenderFrame.h"

bool VansGraphics::VansVKDevice::InitializeCameraFrameResources()
{
	if (m_CameraFrameResourcesReady)
		return true;

	m_CameraFrameResourcesReady = m_CameraDataBuffer.CreatVulkanBuffer(
		m_VansVKLogicDevice,
		sizeof(m_CameraData),
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
			VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	return m_CameraFrameResourcesReady;
}

void VansGraphics::VansVKDevice::DestroyCameraFrameResources()
{
	if (!m_CameraFrameResourcesReady)
		return;

	m_CameraDataBuffer.DestroyVulkanBuffer(m_VansVKLogicDevice);
	m_CameraFrameResourcesReady = false;
}

bool VansGraphics::VansVKDevice::InitializeLightFrameResources()
{
	if (m_LightFrameResourcesReady)
		return true;

	m_LightFrameResourcesReady = m_LightDataBuffer.CreatVulkanBuffer(
		m_VansVKLogicDevice,
		VansLightManager::GetLightBufferPayloadSize(),
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	return m_LightFrameResourcesReady;
}

void VansGraphics::VansVKDevice::DestroyLightFrameResources()
{
	if (!m_LightFrameResourcesReady)
		return;

	m_LightDataBuffer.DestroyVulkanBuffer(m_VansVKLogicDevice);
	m_LightFrameResourcesReady = false;
}

bool VansGraphics::VansVKDevice::UploadRenderLightFrameData(
	const VansRenderLightFrameData& frameData)
{
	const VkDeviceSize expectedSize = VansLightManager::GetLightBufferPayloadSize();
	std::vector<std::uint8_t> payload;
	if (!m_LightFrameResourcesReady || !frameData.IsComplete() ||
		m_LightDataBuffer.GetBufferSize() != expectedSize ||
		!VansLightManager::BuildRenderLightBufferPayload(
			frameData,
			m_PunctualShadowFrameState.GetGPUShadowData(),
			m_PunctualShadowFrameState.GetGPUShadowViews(),
			payload) ||
		payload.size() != static_cast<std::size_t>(expectedSize))
	{
		return false;
	}

	return m_LightDataBuffer.SetBufferData(
		payload.data(),
		0,
		expectedSize);
}

VansGraphics::VansRenderSubmissionPrepareResult
VansGraphics::VansVKDevice::PrepareRenderSubmission(
	const VansRenderFrameSubmission& submission)
{
	if (!m_RenderWorld.Apply(submission.MutationsBeforeFrame()))
	{
		m_HasCurrentRenderView = false;
		return {
			VansRenderSubmissionPrepareStatus::FatalProtocolViolation,
			"Render mutation batch violated stable-handle ordering or generation"
		};
	}

	const VansRenderFramePacket& frame = submission.Frame();
	m_CurrentRenderView = frame.View();
	m_CurrentRenderSceneSnapshot = frame.Scene();
	m_CurrentRenderTiming = frame.Timing();
	m_CurrentTransformKeys.clear();
	m_CurrentTransformIndices.clear();
	for (std::uint32_t index = 0;
		index < static_cast<std::uint32_t>(m_CurrentRenderSceneSnapshot.transforms.size());
		++index)
	{
		const VansRenderProxyHandle proxy =
			m_CurrentRenderSceneSnapshot.transforms[index].proxy;
		if (!proxy.IsValid())
			continue;
		if (proxy.index >= m_CurrentTransformKeys.size())
		{
			m_CurrentTransformKeys.resize(
				static_cast<std::size_t>(proxy.index) + 1u, 0u);
			m_CurrentTransformIndices.resize(
				static_cast<std::size_t>(proxy.index) + 1u, UINT32_MAX);
		}
		m_CurrentTransformKeys[proxy.index] = MakeRenderProxyStableId(proxy);
		m_CurrentTransformIndices[proxy.index] = index;
	}
	m_HasCurrentRenderView = true;
	if (!m_CurrentRenderSceneSnapshot.sceneReady)
		m_MainCameraVisibilityState.Reset();
	if (!m_PunctualShadowFrameState.PrepareFrame(
		m_CurrentRenderSceneSnapshot,
		frame.FrameId().Value() + 1u))
	{
		m_HasCurrentRenderView = false;
		return {
			VansRenderSubmissionPrepareStatus::FatalProtocolViolation,
			"Punctual-shadow frame input violated the backend ownership contract"
		};
	}

	if (!m_CameraFrameResourcesReady ||
		(m_CurrentRenderSceneSnapshot.sceneReady &&
			!UploadRenderLightFrameData(m_CurrentRenderSceneSnapshot.light)))
	{
		m_HasCurrentRenderView = false;
		return {
			VansRenderSubmissionPrepareStatus::RecoverableFailure,
			"Renderer-owned camera/light frame resources are unavailable or rejected the frame payload"
		};
	}

	const VansRenderViewSnapshot& view = m_CurrentRenderView;
	const float width = static_cast<float>(view.viewportWidth);
	const float height = static_cast<float>(view.viewportHeight);
	m_CurrentCameraFrameIndex = m_CameraRenderFrameIndex++;
	const std::uint32_t sequenceIndex = m_CurrentCameraFrameIndex & 1023u;
	float jitterPixelX = 0.0f;
	float jitterPixelY = 0.0f;
	GetTemporalUpscaleJitterOffset(
		sequenceIndex,
		jitterPixelX,
		jitterPixelY);

	m_CameraTemporalJitter = BuildVulkanTemporalJitter(
		glm::vec2(jitterPixelX, jitterPixelY),
		glm::vec2(width, height));
	const glm::mat4 jitteredProjection = m_CameraTemporalJitter.valid
		? ApplyClipSpaceJitter(view.projection, m_CameraTemporalJitter.ndcOffset)
		: view.projection;
	const glm::mat4 inverseView = glm::inverse(view.view);
	const glm::mat4 viewProjection = jitteredProjection * view.view;
	const glm::mat4 unjitteredViewProjection = view.projection * view.view;
	const bool resetHistory = view.historyReset != VansRenderViewHistoryReset::None ||
		m_CurrentCameraFrameIndex == 0;

	m_CameraData.cameraPosition = glm::vec4(view.position, 1.0f);
	m_CameraData.cameraDirection = glm::vec4(view.forward, 0.0f);
	if (resetHistory)
	{
		m_CameraData.lastPreviousViewMatrix = view.view;
		m_CameraData.lastPreviousProjectionMatrix = jitteredProjection;
		m_CameraData.lastPreviousViewProjectionMatrix = viewProjection;
		m_CameraData.lastViewMatrix = view.view;
		m_CameraData.lastProjectionMatrix = jitteredProjection;
		m_CameraData.lastViewProjectionMatrix = viewProjection;
		m_CameraData.lastUnjitteredViewProjectionMatrix = unjitteredViewProjection;
	}
	else
	{
		m_CameraData.lastPreviousViewMatrix = m_CameraData.lastViewMatrix;
		m_CameraData.lastPreviousProjectionMatrix = m_CameraData.lastProjectionMatrix;
		m_CameraData.lastPreviousViewProjectionMatrix = m_CameraData.lastViewProjectionMatrix;
		m_CameraData.lastViewMatrix = m_CameraData.viewMatrix;
		m_CameraData.lastProjectionMatrix = m_CameraData.projectionMatrix;
		m_CameraData.lastViewProjectionMatrix = m_CameraData.viewProjectionMatrix;
		m_CameraData.lastUnjitteredViewProjectionMatrix =
			m_CameraData.unjitteredViewProjectionMatrix;
	}

	m_CameraData.viewMatrix = view.view;
	m_CameraData.projectionMatrix = jitteredProjection;
	m_CameraData.viewProjectionMatrix = viewProjection;
	m_CameraData.unjitteredViewProjectionMatrix = unjitteredViewProjection;
	m_UnjitteredCameraProjection = view.projection;
	m_CameraData.inverseViewMatrix = inverseView;
	m_CameraData.inverseProjectionMatrix = glm::inverse(jitteredProjection);
	m_CameraData.screenParams = glm::vec4(width, height, 1.0f / width, 1.0f / height);
	m_CameraData.frameParams = glm::vec4(
		static_cast<float>(m_CurrentCameraFrameIndex),
		static_cast<float>(frame.Timing().elapsedSeconds),
		GetTemporalUpscaleMipBias(),
		0.0f);
	m_CameraData.cameraParams = glm::vec4(
		view.nearClip,
		view.farClip,
		glm::degrees(view.fieldOfViewRadians),
		view.aspectRatio);

	if (!m_CameraDataBuffer.SetBufferData(&m_CameraData, 0, sizeof(m_CameraData)))
	{
		return {
			VansRenderSubmissionPrepareStatus::RecoverableFailure,
			"Renderer-owned camera frame buffer upload failed"
		};
	}
	return { VansRenderSubmissionPrepareStatus::Ready, {} };
}

const VansGraphics::VansRenderTransformFrameData*
VansGraphics::VansVKDevice::FindCurrentRenderTransform(
	VansRenderProxyHandle proxy) const
{
	if (!proxy.IsValid() || proxy.index >= m_CurrentTransformKeys.size() ||
		m_CurrentTransformKeys[proxy.index] != MakeRenderProxyStableId(proxy))
	{
		return nullptr;
	}
	const std::uint32_t index = m_CurrentTransformIndices[proxy.index];
	return index < m_CurrentRenderSceneSnapshot.transforms.size()
		? &m_CurrentRenderSceneSnapshot.transforms[index]
		: nullptr;
}

VansGraphics::VansTemporalCameraSnapshot
VansGraphics::VansVKDevice::CaptureTemporalCameraSnapshot() const
{
	VansTemporalCameraSnapshot snapshot;
	snapshot.view = m_CameraData.viewMatrix;
	snapshot.projection = m_UnjitteredCameraProjection;
	snapshot.previousViewProjection = m_CameraData.lastUnjitteredViewProjectionMatrix;
	snapshot.position = glm::vec3(m_CameraData.cameraPosition);
	snapshot.up = glm::normalize(glm::vec3(m_CameraData.inverseViewMatrix[1]));
	snapshot.right = glm::normalize(glm::vec3(m_CameraData.inverseViewMatrix[0]));
	snapshot.forward = glm::normalize(glm::vec3(m_CameraData.cameraDirection));
	snapshot.jitter = m_CameraTemporalJitter;
	snapshot.frameIndex = m_CurrentCameraFrameIndex;
	snapshot.nearClip = m_CameraData.cameraParams.x;
	snapshot.farClip = m_CameraData.cameraParams.y;
	snapshot.fovRadians = glm::radians(m_CameraData.cameraParams.z);
	return snapshot;
}
