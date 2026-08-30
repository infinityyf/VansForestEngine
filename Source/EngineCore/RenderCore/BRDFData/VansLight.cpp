#include "VansLight.h"
#include "../../../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../../EngineCore/RenderCore/VulkanCore/VansDescriptorSetLayouts.h"
#include "../../../EngineCore/Configration/VansConfigration.h"
#include "../../../EngineCore/VansTimer.h"
#include "../VansCamera.h"
#include <iostream>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
	constexpr float MIN_DIRECTION_LENGTH_SQ = 1e-6f;
	constexpr float MIN_SPOT_RADIUS = 0.01f;
	constexpr float MIN_SPOT_OUTER_CUTOFF = 0.00174532925f;
	constexpr float MAX_SPOT_OUTER_CUTOFF = 1.56206968f;

	bool IsFiniteFloat(float value)
	{
		return std::isfinite(value) != 0;
	}

	bool IsFiniteVec3(const glm::vec3& value)
	{
		return IsFiniteFloat(value.x) && IsFiniteFloat(value.y) && IsFiniteFloat(value.z);
	}

	glm::vec3 NormalizeLightDirectionSafe(const glm::vec3& direction, const glm::vec3& fallbackDirection)
	{
		if (IsFiniteVec3(direction) && glm::dot(direction, direction) > MIN_DIRECTION_LENGTH_SQ)
		{
			return glm::normalize(direction);
		}

		if (IsFiniteVec3(fallbackDirection) && glm::dot(fallbackDirection, fallbackDirection) > MIN_DIRECTION_LENGTH_SQ)
		{
			return glm::normalize(fallbackDirection);
		}

		return glm::vec3(0.0f, -1.0f, 0.0f);
	}

	float SmoothStep01(float edge0, float edge1, float value)
	{
		const float t = glm::clamp(
			(value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// 为 glm::lookAt 选取与 forward 不共线的稳定 up 向量。
	// 修复：原来硬切阈值 0.99f（约 8°），在临界角度附近会引发每帧反复跳变。
	// 现改为从三个世界轴候选中选取与 forward 点积绝对值最小（最垂直）的轴，
	// 彻底消除单一阈值带来的不稳定区域。
	glm::vec3 ChooseStableUpVector(const glm::vec3& forward)
	{
		const glm::vec3 candidates[3] = {
			glm::vec3(0.0f, 1.0f, 0.0f),   // World Y
			glm::vec3(0.0f, 0.0f, 1.0f),   // World Z
			glm::vec3(1.0f, 0.0f, 0.0f),   // World X
		};
		glm::vec3 best = candidates[0];
		float bestDot  = std::abs(glm::dot(forward, candidates[0]));
		for (int k = 1; k < 3; ++k)
		{
			float d = std::abs(glm::dot(forward, candidates[k]));
			if (d < bestDot)
			{
				bestDot = d;
				best    = candidates[k];
			}
		}
		return best;
	}

	std::array<glm::vec3, 8> BuildFrustumSliceCornersWS(
		const VansGraphics::VansCascadeCameraData& camera,
		float splitNear,
		float splitFar)
	{
		glm::vec3 forward = NormalizeLightDirectionSafe(camera.forward, glm::vec3(0.0f, 0.0f, -1.0f));
		glm::vec3 up = NormalizeLightDirectionSafe(camera.up, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::vec3 right = glm::cross(forward, up);
		if (glm::dot(right, right) <= MIN_DIRECTION_LENGTH_SQ)
		{
			up = ChooseStableUpVector(forward);
			right = glm::cross(forward, up);
		}
		right = glm::normalize(right);
		up = glm::normalize(glm::cross(right, forward));

		const float tanHalfFov = std::tan(camera.verticalFovRadians * 0.5f);
		const float nearHalfY = tanHalfFov * splitNear;
		const float nearHalfX = nearHalfY * camera.aspectRatio;
		const float farHalfY = tanHalfFov * splitFar;
		const float farHalfX = farHalfY * camera.aspectRatio;

		const glm::vec3 nearCenter = camera.position + forward * splitNear;
		const glm::vec3 farCenter = camera.position + forward * splitFar;

		return {
			nearCenter - right * nearHalfX - up * nearHalfY,
			nearCenter + right * nearHalfX - up * nearHalfY,
			nearCenter + right * nearHalfX + up * nearHalfY,
			nearCenter - right * nearHalfX + up * nearHalfY,
			farCenter - right * farHalfX - up * farHalfY,
			farCenter + right * farHalfX - up * farHalfY,
			farCenter + right * farHalfX + up * farHalfY,
			farCenter - right * farHalfX + up * farHalfY,
		};
	}

	float ComputeCascadeNormalBias(int cascade, float worldUnitsPerTexel)
	{
		const float multipliers[4] = { 1.5f, 2.0f, 2.5f, 3.0f };
		return worldUnitsPerTexel * multipliers[(std::min)(cascade, 3)];
	}

	float ComputeCascadeFilterRadius(int cascade)
	{
		// Maximum PCSS footprint.  The actual radius is derived from blocker /
		// receiver separation in world units.  Far cascades use a smaller texel
		// cap because each texel already spans a larger world-space footprint.
		const float radius[4] = { 24.0f, 20.0f, 16.0f, 12.0f };
		return radius[(std::min)(cascade, 3)];
	}

	float ComputeCascadeBlendBand(const float* cascadeSplits, int cascade, float nearPlane, float farPlane)
	{
		const float prevSplit = (cascade > 0) ? (std::max)(cascadeSplits[cascade - 1], nearPlane) : nearPlane;
		const float nextSplit = (cascade < 3) ? (std::min)(cascadeSplits[cascade + 1], farPlane) : farPlane;
		const float cascadeSpan = (std::max)(nextSplit - prevSplit, 1.0f);
		return std::clamp(cascadeSpan * 0.12f, 1.5f, 35.0f);
	}

	struct CascadeBuildResult
	{
		glm::mat4 viewProj;
		float worldUnitsPerTexel;
		float lightDepthRange;
		float normalBias;
		float filterRadiusTexels;
	};

	CascadeBuildResult BuildStableCascade(
		const VansGraphics::VansCascadeCameraData& camera,
		const glm::vec3& lightDirection,
		float splitNear,
		float splitFar,
		int shadowMapSize,
		int cascadeIndex)
	{
		auto corners = BuildFrustumSliceCornersWS(camera, splitNear, splitFar);

		glm::vec3 center(0.0f);
		for (const glm::vec3& p : corners)
		{
			center += p;
		}
		center /= 8.0f;

		float radius = 0.0f;
		for (const glm::vec3& p : corners)
		{
			radius = (std::max)(radius, glm::length(p - center));
		}
		radius = (std::max)(std::ceil(radius * 16.0f) / 16.0f, 0.01f);
		const float receiverPadding = (std::max)(radius * 0.08f, 1.0f);
		radius += receiverPadding;

		glm::vec3 lightForward = NormalizeLightDirectionSafe(-lightDirection, glm::vec3(0.0f, -1.0f, 0.0f));
		glm::vec3 up = ChooseStableUpVector(lightForward);
		glm::vec3 lightRight = glm::normalize(glm::cross(up, lightForward));
		glm::vec3 lightUp = glm::normalize(glm::cross(lightForward, lightRight));

		float worldUnitsPerTexel = (2.0f * radius) / (std::max)(shadowMapSize, 1);

		// Snap in a fixed light-space basis.  Snapping center after a lookAt that
		// already targets center is a no-op because centerLS.xy is always zero.
		// Quantising the world-space projections onto lightRight/lightUp keeps the
		// orthographic projection locked to the shadow texel grid while the camera
		// translates.
		const float centerRight = glm::dot(center, lightRight);
		const float centerUp = glm::dot(center, lightUp);
		const float snappedRight = std::floor(centerRight / worldUnitsPerTexel + 0.5f) * worldUnitsPerTexel;
		const float snappedUp = std::floor(centerUp / worldUnitsPerTexel + 0.5f) * worldUnitsPerTexel;
		const glm::vec3 snappedCenter = center
			+ lightRight * (snappedRight - centerRight)
			+ lightUp * (snappedUp - centerUp);
		glm::mat4 lightView = glm::lookAt(
			snappedCenter - lightForward * radius,
			snappedCenter,
			lightUp);

		float minZ = (std::numeric_limits<float>::max)();
		float maxZ = -(std::numeric_limits<float>::max)();
		for (const glm::vec3& p : corners)
		{
			glm::vec3 pLS = glm::vec3(lightView * glm::vec4(p, 1.0f));
			minZ = (std::min)(minZ, pLS.z);
			maxZ = (std::max)(maxZ, pLS.z);
		}

		const float casterMargin = radius * 3.0f;
		minZ -= casterMargin;
		maxZ += casterMargin;

		glm::mat4 lightProj = glm::ortho(
			-radius,
			radius,
			-radius,
			radius,
			-maxZ,
			-minZ);

		CascadeBuildResult result{};
		result.viewProj = lightProj * lightView;
		result.worldUnitsPerTexel = worldUnitsPerTexel;
		result.lightDepthRange = (std::max)(maxZ - minZ, 0.001f);
		result.normalBias = ComputeCascadeNormalBias(cascadeIndex, worldUnitsPerTexel);
		result.filterRadiusTexels = ComputeCascadeFilterRadius(cascadeIndex);
		return result;
	}

	VansGraphics::VansCascadeCameraData MakeFallbackCascadeCamera(const glm::vec3& cameraPosition)
	{
		VansGraphics::VansCascadeCameraData camera{};
		camera.position = cameraPosition;
		camera.forward = glm::vec3(0.0f, 0.0f, -1.0f);
		camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
		camera.verticalFovRadians = glm::radians(45.0f);
		camera.aspectRatio = 1.0f;
		camera.nearPlane = 0.01f;
		camera.farPlane = 10000.0f;
		return camera;
	}

	float ClampSpotOuterCutoff(float angle)
	{
		if (!IsFiniteFloat(angle))
		{
			return MAX_SPOT_OUTER_CUTOFF;
		}

		return std::clamp(angle, MIN_SPOT_OUTER_CUTOFF, MAX_SPOT_OUTER_CUTOFF);
	}

	void AppendFramePayloadBytes(
		std::vector<std::uint8_t>& payload,
		std::size_t& offset,
		const void* source,
		std::size_t byteCount)
	{
		assert(offset + byteCount <= payload.size());
		if (byteCount > 0)
			std::memcpy(payload.data() + offset, source, byteCount);
		offset += byteCount;
	}

	template<typename T>
	void AppendPaddedFramePayload(
		std::vector<std::uint8_t>& payload,
		std::size_t& offset,
		uint32_t maxCount,
		const std::vector<T>& values)
	{
		const std::size_t paddedByteCount = sizeof(T) * static_cast<std::size_t>(maxCount);
		assert(offset + paddedByteCount <= payload.size());
		const size_t copyCount = (std::min)(values.size(), static_cast<size_t>(maxCount));
		if (copyCount > 0)
			std::memcpy(payload.data() + offset, values.data(), sizeof(T) * copyCount);
		offset += paddedByteCount;
	}
}

void VansGraphics::VansLightManager::AddDirectionalLight(const VansDirectionalLight& light)
{
	m_DirectionalLights.push_back(light);
}

void VansGraphics::VansLightManager::AddPointLight(
	const VansPointLight& light,
	const VansPunctualShadowSettings& shadowSettings)
{
	VansPointLight gpuLight = light;
	gpuLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
	m_PointLights.push_back(gpuLight);
	VansPunctualShadowSettings pointShadowSettings = shadowSettings;
	pointShadowSettings.updateMode = VansShadowUpdateMode::EveryFrame;
	m_PointShadowRegistrations.push_back({ m_NextStableLightId++, pointShadowSettings });
}

void VansGraphics::VansLightManager::AddSpotLight(
	const VansSpotLight& light,
	const VansPunctualShadowSettings& shadowSettings)
{
	VansSpotLight gpuLight = light;
	gpuLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
	m_SpotLights.push_back(gpuLight);
	m_SpotShadowRegistrations.push_back({ m_NextStableLightId++, shadowSettings });
}

void VansGraphics::VansLightManager::AddRectLight(
	const VansRectLight& light,
	const VansPunctualShadowSettings& shadowSettings)
{
	VansRectLight gpuLight = light;
	gpuLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
	m_RectLights.push_back(gpuLight);
	m_RectShadowRegistrations.push_back({ m_NextStableLightId++, shadowSettings });
}

bool VansGraphics::VansLightManager::RemovePointLight(uint32_t index)
{
	if (index >= m_PointLights.size() || index >= m_PointShadowRegistrations.size())
		return false;
	if (index + 1u != m_PointLights.size())
	{
		m_PointLights[index] = m_PointLights.back();
		m_PointShadowRegistrations[index] = m_PointShadowRegistrations.back();
	}
	m_PointLights.pop_back();
	m_PointShadowRegistrations.pop_back();
	return true;
}

bool VansGraphics::VansLightManager::RemoveSpotLight(uint32_t index)
{
	if (index >= m_SpotLights.size() || index >= m_SpotShadowRegistrations.size())
		return false;
	if (index + 1u != m_SpotLights.size())
	{
		m_SpotLights[index] = m_SpotLights.back();
		m_SpotShadowRegistrations[index] = m_SpotShadowRegistrations.back();
	}
	m_SpotLights.pop_back();
	m_SpotShadowRegistrations.pop_back();
	return true;
}

bool VansGraphics::VansLightManager::RemoveRectLight(uint32_t index)
{
	if (index >= m_RectLights.size() || index >= m_RectShadowRegistrations.size())
		return false;
	if (index + 1u != m_RectLights.size())
	{
		m_RectLights[index] = m_RectLights.back();
		m_RectShadowRegistrations[index] = m_RectShadowRegistrations.back();
	}
	m_RectLights.pop_back();
	m_RectShadowRegistrations.pop_back();
	return true;
}

glm::vec3 VansGraphics::VansLightManager::ComputeAtmosphereSunColor(
	const glm::vec3& sunDir,
	const glm::vec3& baseColor)
{
	static const glm::vec3 rayleigh(0.27e-5f, 0.5e-5f, 1.0e-5f);
	static const glm::vec3 mie(0.5e-6f);
	const float sineElevation = glm::dot(
		NormalizeLightDirectionSafe(sunDir, glm::vec3(0.0f, 1.0f, 0.0f)),
		glm::vec3(0.0f, 1.0f, 0.0f));
	const float opticalPathDenominator =
		(std::max)(sineElevation * 2.0f + 0.01f, 0.01f);
	const glm::vec3 exponent = -(rayleigh + mie) *
		(100000.0f / opticalPathDenominator);
	return baseColor * glm::vec3(
		std::pow(2.0f, exponent.x),
		std::pow(2.0f, exponent.y),
		std::pow(2.0f, exponent.z));
}

VansGraphics::VansCelestialLightingState
VansGraphics::VansLightManager::ComputeCelestialLightingState(
	const VansDirectionalLight& light)
{
	VansCelestialLightingState state{};
	const glm::vec3 sunDirection = NormalizeLightDirectionSafe(
		light.m_Direction, glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::vec3 moonDirection = -sunDirection;
	const float nightBlend = 1.0f - SmoothStep01(
		-0.08f, 0.12f, sunDirection.y);
	const bool useMoonKey = nightBlend > 0.5f;

	const glm::vec3 moonTint(0.42f, 0.48f, 0.70f);
	state.sunDirection = sunDirection;
	state.moonDirection = moonDirection;
	state.direction = useMoonKey ? moonDirection : sunDirection;
	state.color = useMoonKey
		? glm::max(light.m_Color * moonTint, glm::vec3(0.0f))
		: ComputeAtmosphereSunColor(sunDirection, light.m_Color);
	state.intensity = (std::max)(
		light.m_Intensity * (useMoonKey ? 0.035f : 1.0f), 0.0f);
	state.moonBlend = nightBlend;

	const float moonElevation = SmoothStep01(
		-0.06f, 0.24f, moonDirection.y);
	const float nightDiffuseScale = glm::mix(
		0.018f, 0.065f, moonElevation);
	const float nightSpecularScale = glm::mix(
		0.025f, 0.085f, moonElevation);
	state.skyDiffuseScale = glm::mix(
		1.0f, nightDiffuseScale, nightBlend);
	state.skySpecularScale = glm::mix(
		1.0f, nightSpecularScale, nightBlend);
	return state;
}

void VansGraphics::VansLightManager::UpdateLightShadowMatrixData(const VansCascadeCameraData& cameraData)
{
	auto vansConfig = VansConfigration::GetInstance();
	int cascadeCount = vansConfig->GetCascadeCount();
	const float* cascadeSplits = vansConfig->GetCascadeSplits();
	int cascadeMapSize = vansConfig->GetCascadeShadowMapSize();

	int directionLightCount = static_cast<int>(m_DirectionalLights.size());
	if (directionLightCount <= 0)
		m_MainCelestialLightingState = VansCelestialLightingState{};
	for (int dirLightIndex = 0; dirLightIndex < directionLightCount; dirLightIndex++)
	{
		auto& dirLight = m_DirectionalLights[dirLightIndex];
		const VansCelestialLightingState celestialState =
			ComputeCelestialLightingState(dirLight);
		if (dirLightIndex == 0)
			m_MainCelestialLightingState = celestialState;
		auto lightDir = NormalizeLightDirectionSafe(
			celestialState.direction, glm::vec3(0.0f, -1.0f, 0.0f));

		dirLight.m_CascadeSplits = glm::vec4(cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], cascadeSplits[3]);

		float splitNear = (std::max)(cameraData.nearPlane, 0.001f);
		for (int cascade = 0; cascade < cascadeCount; ++cascade)
		{
			float splitFar = (std::min)(cascadeSplits[cascade], cameraData.farPlane);
			splitFar = (std::max)(splitFar, splitNear + 0.01f);
			const float overlapBand = ComputeCascadeBlendBand(cascadeSplits, cascade, cameraData.nearPlane, cameraData.farPlane);
			const float buildNear = (std::max)(cameraData.nearPlane, splitNear - overlapBand);
			const float buildFar = (std::min)(cameraData.farPlane, splitFar + overlapBand);

			CascadeBuildResult cascadeData = BuildStableCascade(
				cameraData, lightDir, buildNear, buildFar, cascadeMapSize, cascade);

			dirLight.m_ShadowMatrix[cascade] = cascadeData.viewProj;
			dirLight.m_CascadeTexelSize[cascade] = cascadeData.worldUnitsPerTexel;
			dirLight.m_CascadeDepthScale[cascade] = 1.0f / cascadeData.lightDepthRange;
			dirLight.m_CascadeNormalBias[cascade] = cascadeData.normalBias;
			dirLight.m_CascadeFilterRadius[cascade] = cascadeData.filterRadiusTexels;

			splitNear = splitFar;
		}
	}

	// Punctual shadow allocation is intentionally not prepared here. Main only
	// resolves cascade matrices; backend consumes BuildPunctualShadowFrameInput.
}

VansGraphics::VansRenderPunctualShadowFrameInput
VansGraphics::VansLightManager::BuildPunctualShadowFrameInput(
	const VansCascadeCameraData& cameraData)
{
	VansRenderPunctualShadowFrameInput frameInput;
	frameInput.lights.reserve(m_PointLights.size() + m_SpotLights.size() + m_RectLights.size());

	const uint32_t pointLightCount = static_cast<uint32_t>((std::min)(m_PointLights.size(), static_cast<size_t>(VANS_MAX_POINT_LIGHTS)));
	for (uint32_t lightIndex = 0; lightIndex < pointLightCount && lightIndex < m_PointShadowRegistrations.size(); ++lightIndex)
	{
		VansPointLight& light = m_PointLights[lightIndex];
		const VansPunctualShadowRegistration& registration = m_PointShadowRegistrations[lightIndex];
		VansPunctualShadowLightInput input;
		input.stableLightId = registration.stableLightId;
		input.type = VansPunctualShadowLightType::Point;
		input.gpuLightIndex = lightIndex;
		input.position = light.m_Position;
		input.color = light.m_Color;
		input.intensity = light.m_Intensity;
		input.radius = light.m_Radius;
		input.settings = registration.settings;
		// 点光没有缓存更新模式：只要投影，就固定每帧刷新完整六面。
		input.settings.updateMode = VansShadowUpdateMode::EveryFrame;
		frameInput.lights.push_back(input);
	}

	const uint32_t spotLightCount = static_cast<uint32_t>((std::min)(m_SpotLights.size(), static_cast<size_t>(VANS_MAX_SPOT_LIGHTS)));
	for (uint32_t lightIndex = 0; lightIndex < spotLightCount && lightIndex < m_SpotShadowRegistrations.size(); ++lightIndex)
	{
		VansSpotLight& light = m_SpotLights[lightIndex];
		light.m_Direction = NormalizeLightDirectionSafe(light.m_Direction, glm::vec3(0.0f, -1.0f, 0.0f));
		light.m_OuterCutOff = ClampSpotOuterCutoff(light.m_OuterCutOff);
		light.m_Radius = (std::max)(light.m_Radius, MIN_SPOT_RADIUS);

		VansPunctualShadowLightInput input;
		input.stableLightId = m_SpotShadowRegistrations[lightIndex].stableLightId;
		input.type = VansPunctualShadowLightType::Spot;
		input.gpuLightIndex = lightIndex;
		input.position = light.m_Position;
		input.direction = light.m_Direction;
		input.color = light.m_Color;
		input.intensity = light.m_Intensity;
		input.radius = light.m_Radius;
		input.outerConeRadians = light.m_OuterCutOff;
		input.settings = m_SpotShadowRegistrations[lightIndex].settings;
		frameInput.lights.push_back(input);
	}

	const uint32_t rectLightCount = static_cast<uint32_t>((std::min)(m_RectLights.size(), static_cast<size_t>(VANS_MAX_RECT_LIGHTS)));
	for (uint32_t lightIndex = 0; lightIndex < rectLightCount && lightIndex < m_RectShadowRegistrations.size(); ++lightIndex)
	{
		VansRectLight& light = m_RectLights[lightIndex];
		light.m_Normal = NormalizeLightDirectionSafe(light.m_Normal, glm::vec3(0.0f, 0.0f, 1.0f));

		VansPunctualShadowLightInput input;
		input.stableLightId = m_RectShadowRegistrations[lightIndex].stableLightId;
		input.type = VansPunctualShadowLightType::Rect;
		input.gpuLightIndex = lightIndex;
		input.position = light.m_Position;
		input.direction = light.m_Normal;
		input.color = light.m_Color;
		input.intensity = light.m_Intensity;
		input.radius = light.m_Range;
		input.halfWidth = light.m_HalfWidth;
		input.halfHeight = light.m_HalfHeight;
		input.settings = m_RectShadowRegistrations[lightIndex].settings;
		frameInput.lights.push_back(input);
	}

	frameInput.camera.position = cameraData.position;
	frameInput.camera.forward = cameraData.forward;
	frameInput.camera.up = cameraData.up;
	frameInput.camera.verticalFovRadians = cameraData.verticalFovRadians;
	frameInput.camera.aspectRatio = cameraData.aspectRatio;
	frameInput.camera.nearPlane = cameraData.nearPlane;
	frameInput.camera.farPlane = cameraData.farPlane;
	frameInput.camera.viewportWidth = cameraData.viewportWidth;
	frameInput.camera.viewportHeight = cameraData.viewportHeight;
	frameInput.prepared = true;
	return frameInput;
}

void VansGraphics::VansLightManager::UpdateLightShadowMatrixData(const glm::vec3& cameraPosition)
{
	UpdateLightShadowMatrixData(MakeFallbackCascadeCamera(cameraPosition));
}

std::vector<VansGraphics::VansDirectionalLight>
VansGraphics::VansLightManager::BuildPreparedDirectionalLights()
{
	std::vector<VansDirectionalLight> preparedLights = m_DirectionalLights;
	if (preparedLights.empty())
		m_MainCelestialLightingState = VansCelestialLightingState{};
	for (size_t lightIndex = 0; lightIndex < preparedLights.size(); ++lightIndex)
	{
		auto& light = preparedLights[lightIndex];
		const VansCelestialLightingState celestialState =
			ComputeCelestialLightingState(light);
		if (lightIndex == 0)
			m_MainCelestialLightingState = celestialState;
		light.m_Direction = celestialState.direction;
		light.m_Color = celestialState.color;
		light.m_Intensity = celestialState.intensity;
	}
	return preparedLights;
}

void VansGraphics::VansLightManager::RefreshDerivedLightingState()
{
	(void)BuildPreparedDirectionalLights();
}

std::size_t VansGraphics::VansLightManager::GetLightBufferPayloadSize()
{
	return sizeof(uint32_t) * 4 + sizeof(float) * 4 +
		sizeof(VansDirectionalLight) * VANS_MAX_DIRECTION_LIGHTS +
		sizeof(VansPointLight) * VANS_MAX_POINT_LIGHTS +
		sizeof(VansSpotLight) * VANS_MAX_SPOT_LIGHTS +
		sizeof(VansRectLight) * VANS_MAX_RECT_LIGHTS +
		sizeof(VansPunctualShadowGPU) * VANS_MAX_PUNCTUAL_LIGHTS +
		sizeof(VansPunctualShadowViewGPU) * VANS_MAX_PUNCTUAL_SHADOW_VIEWS;
}

bool VansGraphics::VansLightManager::BuildRenderLightBufferPayload(
	const VansRenderLightFrameData& frameData,
	const std::vector<VansPunctualShadowGPU>& shadowData,
	const std::vector<VansPunctualShadowViewGPU>& shadowViews,
	std::vector<std::uint8_t>& outPayload)
{
	if (!frameData.IsComplete() ||
		frameData.directionalLights.size() > VANS_MAX_DIRECTION_LIGHTS ||
		frameData.pointLights.size() > VANS_MAX_POINT_LIGHTS ||
		frameData.spotLights.size() > VANS_MAX_SPOT_LIGHTS ||
		frameData.rectLights.size() > VANS_MAX_RECT_LIGHTS ||
		shadowData.size() > VANS_MAX_PUNCTUAL_LIGHTS ||
		shadowViews.size() > VANS_MAX_PUNCTUAL_SHADOW_VIEWS)
	{
		return false;
	}

	const std::array<uint32_t, 4> lightCounts = {
		static_cast<uint32_t>(frameData.pointLights.size()),
		static_cast<uint32_t>(frameData.spotLights.size()),
		frameData.punctualShadowMapWidth,
		static_cast<uint32_t>(shadowViews.size())
	};
	const std::array<float, 4> softShadowParams = {
		frameData.frameSequence,
		0.3f,
		static_cast<float>(frameData.rectLights.size()),
		0.0f
	};

	outPayload.assign(GetLightBufferPayloadSize(), 0u);
	std::size_t offset = 0;
	AppendFramePayloadBytes(outPayload, offset, lightCounts.data(), sizeof(lightCounts));
	AppendFramePayloadBytes(outPayload, offset, softShadowParams.data(), sizeof(softShadowParams));
	AppendPaddedFramePayload(outPayload, offset, VANS_MAX_DIRECTION_LIGHTS, frameData.directionalLights);
	AppendPaddedFramePayload(outPayload, offset, VANS_MAX_POINT_LIGHTS, frameData.pointLights);
	AppendPaddedFramePayload(outPayload, offset, VANS_MAX_SPOT_LIGHTS, frameData.spotLights);
	AppendPaddedFramePayload(outPayload, offset, VANS_MAX_RECT_LIGHTS, frameData.rectLights);
	AppendPaddedFramePayload(outPayload, offset, VANS_MAX_PUNCTUAL_LIGHTS, shadowData);
	AppendPaddedFramePayload(outPayload, offset, VANS_MAX_PUNCTUAL_SHADOW_VIEWS, shadowViews);
	assert(offset == outPayload.size());
	return true;
}

VansGraphics::VansRenderLightFrameData
VansGraphics::VansLightManager::BuildRenderLightFrameData()
{
	const auto vansConfigration = VansConfigration::GetInstance();
	const std::vector<VansDirectionalLight> preparedDirectionalLights =
		BuildPreparedDirectionalLights();
	VansRenderLightFrameData frameData;
	frameData.directionalLights.assign(
		preparedDirectionalLights.begin(),
		preparedDirectionalLights.begin() + (std::min)(
			preparedDirectionalLights.size(),
			static_cast<size_t>(VANS_MAX_DIRECTION_LIGHTS)));
	frameData.pointLights.assign(
		m_PointLights.begin(),
		m_PointLights.begin() + (std::min)(m_PointLights.size(), static_cast<size_t>(VANS_MAX_POINT_LIGHTS)));
	frameData.spotLights.assign(
		m_SpotLights.begin(),
		m_SpotLights.begin() + (std::min)(m_SpotLights.size(), static_cast<size_t>(VANS_MAX_SPOT_LIGHTS)));
	frameData.rectLights.assign(
		m_RectLights.begin(),
		m_RectLights.begin() + (std::min)(m_RectLights.size(), static_cast<size_t>(VANS_MAX_RECT_LIGHTS)));
	for (VansPointLight& light : frameData.pointLights)
		light.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
	for (VansSpotLight& light : frameData.spotLights)
		light.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
	for (VansRectLight& light : frameData.rectLights)
		light.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
	frameData.punctualShadowMapWidth =
		static_cast<uint32_t>(vansConfigration->GetPunctualShadowMapWidth());
	frameData.frameSequence = m_LightFrameSequence += 1.0f;
	frameData.prepared = true;
	return frameData;
}

void VansGraphics::VansLightManager::ClearLights()
{
	m_DirectionalLights.clear();
	m_PointLights.clear();
	m_SpotLights.clear();
	m_RectLights.clear();
	m_PointShadowRegistrations.clear();
	m_SpotShadowRegistrations.clear();
	m_RectShadowRegistrations.clear();
	m_NextStableLightId = 1;
	m_LightFrameSequence = 0.0f;
	m_MainCelestialLightingState = VansCelestialLightingState{};
}

