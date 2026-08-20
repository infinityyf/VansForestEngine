#include "VansLight.h"
#include "../../../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../../EngineCore/RenderCore/VulkanCore/VansDescriptorSetLayouts.h"
#include "../../../EngineCore/Configration/VansConfigration.h"
#include "../../../EngineCore/VansTimer.h"
#include "../VansCamera.h"
#include <iostream>
#include <algorithm>
#include <array>
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
		const float t = glm::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
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

	template<typename T>
	void UploadPaddedLightData(
		VansGraphics::VansVKBuffer& lightBuffer,
		uint32_t offset,
		uint32_t maxCount,
		const std::vector<T>& lights)
	{
		std::vector<T> paddedLights(maxCount);
		const size_t copyCount = (std::min)(lights.size(), static_cast<size_t>(maxCount));
		if (copyCount > 0)
		{
			std::copy_n(lights.begin(), copyCount, paddedLights.begin());
		}

		lightBuffer.SetBufferData(paddedLights.data(), offset, sizeof(T) * maxCount);
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
	m_PointShadowRegistrations.push_back({ m_NextStableLightId++, shadowSettings });
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
	m_PunctualShadowManager.RemoveLight(m_PointShadowRegistrations[index].stableLightId);
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
	m_PunctualShadowManager.RemoveLight(m_SpotShadowRegistrations[index].stableLightId);
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
	m_PunctualShadowManager.RemoveLight(m_RectShadowRegistrations[index].stableLightId);
	if (index + 1u != m_RectLights.size())
	{
		m_RectLights[index] = m_RectLights.back();
		m_RectShadowRegistrations[index] = m_RectShadowRegistrations.back();
	}
	m_RectLights.pop_back();
	m_RectShadowRegistrations.pop_back();
	return true;
}

// 将 baseColor 乘以大气仰角衰减，得到 GPU 上传用的有效太阳颜色。
// 系数与 VolumetricFog.comp AtmSunColor / CloudCommon CalcCloudSunAbsorbLight 完全一致。
glm::vec3 VansGraphics::VansLightManager::ComputeAtmosphereSunColor(
	const glm::vec3& sunDir, const glm::vec3& baseColor)
{
	// 硬编码简化大气系数（Rayleigh + Mie），与 shader 端保持一致
	static const glm::vec3 kAtmRayleigh = glm::vec3(0.27e-5f, 0.5e-5f, 1.0e-5f);
	static const glm::vec3 kAtmMie      = glm::vec3(0.5e-6f, 0.5e-6f, 0.5e-6f);
	static const glm::vec3 kAtmTotal    = kAtmRayleigh + kAtmMie;

	const float sinElev = glm::dot(glm::normalize(sunDir), glm::vec3(0.0f, 1.0f, 0.0f));
	const float d       = (std::max)(sinElev * 2.0f + 0.01f, 0.01f);
	const float od      = 100000.0f / d;
	// exp2(-coeff * od) = pow(2, -coeff * od)，逐分量计算
	const glm::vec3 exponent    = -kAtmTotal * od;
	const glm::vec3 attenuation = glm::vec3(std::pow(2.0f, exponent.x),
	                                         std::pow(2.0f, exponent.y),
	                                         std::pow(2.0f, exponent.z));
	return baseColor * attenuation;
}

VansGraphics::VansCelestialLightingState VansGraphics::VansLightManager::ComputeCelestialLightingState(
	const VansDirectionalLight& light)
{
	VansCelestialLightingState state{};
	const glm::vec3 sunDir = NormalizeLightDirectionSafe(light.m_Direction, glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::vec3 moonDir = -sunDir;
	const float sunElevation = sunDir.y;
	const float nightBlend = 1.0f - SmoothStep01(-0.08f, 0.12f, sunElevation);
	const bool useMoonKey = nightBlend > 0.5f;

	const glm::vec3 moonTint = glm::vec3(0.42f, 0.48f, 0.70f);
	const float moonKeyIntensityScale = 0.035f;
	state.sunDirection = sunDir;
	state.moonDirection = moonDir;
	state.direction = useMoonKey ? moonDir : sunDir;
	state.color = useMoonKey
		? glm::vec3(
			(std::max)(light.m_Color.x * moonTint.x, 0.0f),
			(std::max)(light.m_Color.y * moonTint.y, 0.0f),
			(std::max)(light.m_Color.z * moonTint.z, 0.0f))
		: ComputeAtmosphereSunColor(sunDir, light.m_Color);
	state.intensity = (std::max)(light.m_Intensity * (useMoonKey ? moonKeyIntensityScale : 1.0f), 0.0f);
	state.moonBlend = nightBlend;

	const float moonElevation = SmoothStep01(-0.06f, 0.24f, moonDir.y);
	const float nightDiffuseScale = glm::mix(0.018f, 0.065f, moonElevation);
	const float nightSpecularScale = glm::mix(0.025f, 0.085f, moonElevation);
	state.skyDiffuseScale = glm::mix(1.0f, nightDiffuseScale, nightBlend);
	state.skySpecularScale = glm::mix(1.0f, nightSpecularScale, nightBlend);
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
		const VansCelestialLightingState celestialState = ComputeCelestialLightingState(dirLight);
		if (dirLightIndex == 0)
			m_MainCelestialLightingState = celestialState;
		auto lightDir = NormalizeLightDirectionSafe(celestialState.direction, glm::vec3(0.0f, -1.0f, 0.0f));

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

	std::vector<VansPunctualShadowLightInput> punctualInputs;
	punctualInputs.reserve(m_PointLights.size() + m_SpotLights.size() + m_RectLights.size());

	const uint32_t pointLightCount = static_cast<uint32_t>((std::min)(m_PointLights.size(), static_cast<size_t>(m_MaxPointLightCount)));
	for (uint32_t lightIndex = 0; lightIndex < pointLightCount && lightIndex < m_PointShadowRegistrations.size(); ++lightIndex)
	{
		VansPointLight& light = m_PointLights[lightIndex];
		VansPunctualShadowLightInput input;
		input.stableLightId = m_PointShadowRegistrations[lightIndex].stableLightId;
		input.type = VansPunctualShadowLightType::Point;
		input.gpuLightIndex = lightIndex;
		input.position = light.m_Position;
		input.color = light.m_Color;
		input.intensity = light.m_Intensity;
		input.radius = light.m_Radius;
		input.settings = m_PointShadowRegistrations[lightIndex].settings;
		punctualInputs.push_back(input);
	}

	const uint32_t spotLightCount = static_cast<uint32_t>((std::min)(m_SpotLights.size(), static_cast<size_t>(m_MaxSpotLightCount)));
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
		punctualInputs.push_back(input);
	}

	const uint32_t rectLightCount = static_cast<uint32_t>((std::min)(m_RectLights.size(), static_cast<size_t>(m_MaxRectLightCount)));
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
		punctualInputs.push_back(input);
	}

	VansPunctualShadowCameraData punctualCamera;
	punctualCamera.position = cameraData.position;
	punctualCamera.forward = cameraData.forward;
	punctualCamera.up = cameraData.up;
	punctualCamera.verticalFovRadians = cameraData.verticalFovRadians;
	punctualCamera.aspectRatio = cameraData.aspectRatio;
	punctualCamera.nearPlane = cameraData.nearPlane;
	punctualCamera.farPlane = cameraData.farPlane;
	punctualCamera.viewportWidth = cameraData.viewportWidth;
	punctualCamera.viewportHeight = cameraData.viewportHeight;
	m_PunctualShadowManager.PrepareFrame(punctualCamera, punctualInputs, ++m_ShadowFrameIndex);

	for (uint32_t lightIndex = 0; lightIndex < pointLightCount && lightIndex < m_PointShadowRegistrations.size(); ++lightIndex)
		m_PointLights[lightIndex].m_ShadowMetaIndex = m_PunctualShadowManager.GetShadowMetaIndex(m_PointShadowRegistrations[lightIndex].stableLightId);
	for (uint32_t lightIndex = 0; lightIndex < spotLightCount && lightIndex < m_SpotShadowRegistrations.size(); ++lightIndex)
		m_SpotLights[lightIndex].m_ShadowMetaIndex = m_PunctualShadowManager.GetShadowMetaIndex(m_SpotShadowRegistrations[lightIndex].stableLightId);
	for (uint32_t lightIndex = 0; lightIndex < rectLightCount && lightIndex < m_RectShadowRegistrations.size(); ++lightIndex)
		m_RectLights[lightIndex].m_ShadowMetaIndex = m_PunctualShadowManager.GetShadowMetaIndex(m_RectShadowRegistrations[lightIndex].stableLightId);
}

void VansGraphics::VansLightManager::UpdateLightShadowMatrixData(const glm::vec3& cameraPosition)
{
	UpdateLightShadowMatrixData(MakeFallbackCascadeCamera(cameraPosition));
}
void VansGraphics::VansLightManager::UpdateLightCPUData()
{
	//for (int spotLightIndex = 0; spotLightIndex < m_SpotLights.size(); spotLightIndex++)
	//{

	//	m_SpotLights[spotLightIndex].m_Position.x = std::sin(VansTimer::GetFrameTime() * 0.5f) * 6;
	//}

	auto vansConfigration = VansConfigration::GetInstance();
	const uint32_t punctualShadowSize = static_cast<uint32_t>(vansConfigration->GetPunctualShadowMapWidth());

	uint32_t offset = 0;
	uint32_t size = sizeof(uint32_t) * 4;
	m_LightCounts[0] = static_cast<uint32_t>((std::min)(m_PointLights.size(), static_cast<size_t>(m_MaxPointLightCount)));
	m_LightCounts[1] = static_cast<uint32_t>((std::min)(m_SpotLights.size(), static_cast<size_t>(m_MaxSpotLightCount)));
	m_LightCounts[2] = punctualShadowSize;
	m_LightCounts[3] = static_cast<uint32_t>(m_PunctualShadowManager.GetGPUShadowViews().size());
	m_LightBuffer.SetBufferData(m_LightCounts, offset, size);
	offset += size;
	size = sizeof(float) * 4;
	m_SoftShadowParams[0] = m_SoftShadowParams[0] + 1;
	m_SoftShadowParams[1] = 0.3f; // 软阴影半径控制
	// softShadowParams.z = RectLight 计数（shader 以 uint(softShadowParams.z) 读取）
	m_SoftShadowParams[2] = static_cast<float>((std::min)(m_RectLights.size(), static_cast<size_t>(m_MaxRectLightCount)));
	m_SoftShadowParams[3] = 0;
	m_LightBuffer.SetBufferData(m_SoftShadowParams, offset, size);
	offset += size;
	size = sizeof(VansDirectionalLight) * m_MaxDirectionLightCount;
	{
		// 上传前将颜色替换为大气衰减后的有效颜色
		// m_Color 保持为美术原始值；GPU buffer 中的 color 是最终有效光照颜色
		// 所有 include LightsData.glsl 的 shader 均通过 uDirectionLight.color 取到统一来源
		auto dirLightsForUpload = m_DirectionalLights;
		if (dirLightsForUpload.empty())
			m_MainCelestialLightingState = VansCelestialLightingState{};
		for (size_t lightIndex = 0; lightIndex < dirLightsForUpload.size(); ++lightIndex)
		{
			auto& dl = dirLightsForUpload[lightIndex];
			const VansCelestialLightingState celestialState = ComputeCelestialLightingState(dl);
			if (lightIndex == 0)
				m_MainCelestialLightingState = celestialState;
			dl.m_Direction = celestialState.direction;
			dl.m_Color = celestialState.color;
			dl.m_Intensity = celestialState.intensity;
		}
		UploadPaddedLightData(m_LightBuffer, offset, m_MaxDirectionLightCount, dirLightsForUpload);
	}
	offset += size;
	size = sizeof(VansPointLight) * m_MaxPointLightCount;
	UploadPaddedLightData(m_LightBuffer, offset, m_MaxPointLightCount, m_PointLights);
	offset += size;
	size = sizeof(VansSpotLight) * m_MaxSpotLightCount;
	UploadPaddedLightData(m_LightBuffer, offset, m_MaxSpotLightCount, m_SpotLights);
	offset += size;
	size = sizeof(VansRectLight) * m_MaxRectLightCount;
	UploadPaddedLightData(m_LightBuffer, offset, m_MaxRectLightCount, m_RectLights);
	offset += size;
	size = sizeof(VansPunctualShadowGPU) * VANS_MAX_PUNCTUAL_LIGHTS;
	UploadPaddedLightData(
		m_LightBuffer,
		offset,
		VANS_MAX_PUNCTUAL_LIGHTS,
		m_PunctualShadowManager.GetGPUShadowData());
	offset += size;
	size = sizeof(VansPunctualShadowViewGPU) * VANS_MAX_PUNCTUAL_SHADOW_VIEWS;
	UploadPaddedLightData(
		m_LightBuffer,
		offset,
		VANS_MAX_PUNCTUAL_SHADOW_VIEWS,
		m_PunctualShadowManager.GetGPUShadowViews());

}

void VansGraphics::VansLightManager::SyncLightGPUData(const glm::vec3& cameraPosition)
{
	UpdateLightShadowMatrixData(cameraPosition);
	UpdateLightCPUData();
}

void VansGraphics::VansLightManager::SyncLightGPUData(const VansCascadeCameraData& cameraData)
{
	UpdateLightShadowMatrixData(cameraData);
	UpdateLightCPUData();
}

void VansGraphics::VansLightManager::CreateLightUniformData(VkDevice& logic_device)
{
	uint32_t bufferSize = sizeof(uint32_t) * 4 + sizeof(VansDirectionalLight) * m_MaxDirectionLightCount +
		sizeof(VansPointLight) * m_MaxPointLightCount +
		sizeof(VansSpotLight) * m_MaxSpotLightCount +
		sizeof(VansRectLight) * m_MaxRectLightCount +
		sizeof(VansPunctualShadowGPU) * VANS_MAX_PUNCTUAL_LIGHTS +
		sizeof(VansPunctualShadowViewGPU) * VANS_MAX_PUNCTUAL_SHADOW_VIEWS +
		sizeof(float) * 4;
	m_LightBuffer.CreatVulkanBuffer(
		logic_device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
	);

	//创建资源
	VkDescriptorSetLayoutBinding lightBufferBinding =
	{
		PassBinding::CBUFFER_0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		1,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
		nullptr
	};
	VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
		{ lightBufferBinding },
		m_LightDataDescriptorSetLayout,
		m_LightDataDescriptorSets);

	//update descriptor
	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();
	descManager->WriteBufferDescriptor(
		m_LightDataDescriptorSets[0],
		PassBinding::CBUFFER_0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{
			m_LightBuffer.GetNativeBuffer(),
			0,
			m_LightBuffer.GetBufferSize()
		}});
	descManager->CommitDescriptorUpdates();
}

VansGraphics::VansLightManager::~VansLightManager()
{
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(m_LightDataDescriptorSets);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(m_LightDataDescriptorSetLayout);
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
	m_PunctualShadowManager.Reset();
	m_NextStableLightId = 1;
	m_ShadowFrameIndex = 0;
	memset(m_LightCounts, 0, sizeof(m_LightCounts));
	memset(m_SoftShadowParams, 0, sizeof(m_SoftShadowParams));
}

void VansGraphics::VansLightManager::DestroyGPUResources(VkDevice device)
{
	m_LightBuffer.DestroyVulkanBuffer(device);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(m_LightDataDescriptorSets);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(m_LightDataDescriptorSetLayout);
}
