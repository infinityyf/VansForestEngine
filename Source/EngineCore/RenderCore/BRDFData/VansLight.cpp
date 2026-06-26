#include "VansLight.h"
#include "../../../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
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
		const float radius[4] = { 1.0f, 1.25f, 1.5f, 2.0f };
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

		glm::mat4 lightView = glm::lookAt(center - lightForward * radius, center, lightUp);
		glm::vec3 centerLS = glm::vec3(lightView * glm::vec4(center, 1.0f));

		float worldUnitsPerTexel = (2.0f * radius) / (std::max)(shadowMapSize, 1);
		centerLS.x = std::floor(centerLS.x / worldUnitsPerTexel + 0.5f) * worldUnitsPerTexel;
		centerLS.y = std::floor(centerLS.y / worldUnitsPerTexel + 0.5f) * worldUnitsPerTexel;
		const glm::vec3 snappedCenter = glm::vec3(glm::inverse(lightView) * glm::vec4(centerLS, 1.0f));
		lightView = glm::lookAt(snappedCenter - lightForward * radius, snappedCenter, lightUp);

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
			minZ,
			maxZ);

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

void VansGraphics::VansLightManager::AddPointLight(const VansPointLight& light)
{
	m_PointLights.push_back(light);
}

void VansGraphics::VansLightManager::AddSpotLight(const VansSpotLight& light)
{
	m_SpotLights.push_back(light);
}

void VansGraphics::VansLightManager::AddRectLight(const VansRectLight& light)
{
	m_RectLights.push_back(light);
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

void VansGraphics::VansLightManager::UpdateLightShadowMatrixData(const VansCascadeCameraData& cameraData)
{
	auto vansConfig = VansConfigration::GetInstance();
	int cascadeCount = vansConfig->GetCascadeCount();
	const float* cascadeSplits = vansConfig->GetCascadeSplits();
	int cascadeMapSize = vansConfig->GetCascadeShadowMapSize();

	int directionLightCount = static_cast<int>(m_DirectionalLights.size());
	for (int dirLightIndex = 0; dirLightIndex < directionLightCount; dirLightIndex++)
	{
		auto& dirLight = m_DirectionalLights[dirLightIndex];
		auto lightDir = NormalizeLightDirectionSafe(dirLight.m_Direction, glm::vec3(0.0f, -1.0f, 0.0f));

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

	int pointLightCount = static_cast<int>(m_PointLights.size());
	for (int pointLightIndex = 0; pointLightIndex < pointLightCount; pointLightIndex++)
	{
		glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.001f, m_PointLights[pointLightIndex].m_Radius);
		glm::vec3 lightPos = m_PointLights[pointLightIndex].m_Position;

		m_PointLights[pointLightIndex].m_PointShadowMatrix[0] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(1, 0, 0), glm::vec3(0, -1, 0));
		m_PointLights[pointLightIndex].m_PointShadowMatrix[1] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0));
		m_PointLights[pointLightIndex].m_PointShadowMatrix[2] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 1, 0), glm::vec3(0, 0, 1));
		m_PointLights[pointLightIndex].m_PointShadowMatrix[3] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, -1, 0), glm::vec3(0, 0, -1));
		m_PointLights[pointLightIndex].m_PointShadowMatrix[4] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 0, 1), glm::vec3(0, -1, 0));
		m_PointLights[pointLightIndex].m_PointShadowMatrix[5] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 0, -1), glm::vec3(0, -1, 0));
		m_PointLights[pointLightIndex].m_ShadowIndex = static_cast<float>(pointLightIndex);
	}

	int spotLightCount = static_cast<int>(m_SpotLights.size());
	for (int spotLightIndex = 0; spotLightIndex < spotLightCount; spotLightIndex++)
	{
		glm::vec3 sanitizedDirection = NormalizeLightDirectionSafe(
			m_SpotLights[spotLightIndex].m_Direction,
			glm::vec3(0.0f, -1.0f, 0.0f));
		m_SpotLights[spotLightIndex].m_Direction = sanitizedDirection;

		float spotAngle = ClampSpotOuterCutoff(m_SpotLights[spotLightIndex].m_OuterCutOff);
		m_SpotLights[spotLightIndex].m_OuterCutOff = spotAngle;
		m_SpotLights[spotLightIndex].m_Radius = (std::max)(m_SpotLights[spotLightIndex].m_Radius, MIN_SPOT_RADIUS);

		glm::mat4 shadowProj = glm::perspective(spotAngle * 2.0f, 1.0f, 0.001f, m_SpotLights[spotLightIndex].m_Radius);
		glm::vec3 lightPos = m_SpotLights[spotLightIndex].m_Position;
		glm::vec3 lightDir = -sanitizedDirection;
		glm::vec3 upVector = ChooseStableUpVector(lightDir);
		glm::mat4 shadowView = glm::lookAt(lightPos, lightPos + lightDir, upVector);
		m_SpotLights[spotLightIndex].m_SpotShadowMatrix = shadowProj * shadowView;
		m_SpotLights[spotLightIndex].m_ShadowIndex = static_cast<float>(spotLightIndex);
	}

	int rectLightCount = static_cast<int>((std::min)(m_RectLights.size(), static_cast<size_t>(m_MaxRectLightCount)));
	for (int rectLightIndex = 0; rectLightIndex < rectLightCount; rectLightIndex++)
	{
		auto& rl = m_RectLights[rectLightIndex];
		if (rl.m_ShadowIndex < 0.0f) continue;

		float halfDiag = std::sqrt(rl.m_HalfWidth * rl.m_HalfWidth + rl.m_HalfHeight * rl.m_HalfHeight);
		float fovY = 2.0f * std::atan2(halfDiag + rl.m_Range * 0.05f, 0.001f);
		fovY = (std::min)(fovY, glm::radians(160.0f));
		glm::mat4 shadowProj = glm::perspective(fovY, 1.0f, 0.001f, (std::max)(rl.m_Range, 0.01f));

		glm::vec3 lightPos = rl.m_Position;
		glm::vec3 lightDir = NormalizeLightDirectionSafe(rl.m_Normal, glm::vec3(0.0f, 0.0f, 1.0f));
		rl.m_Normal = lightDir;
		glm::vec3 upVector = ChooseStableUpVector(lightDir);
		glm::mat4 shadowView = glm::lookAt(lightPos, lightPos + lightDir, upVector);
		rl.m_ShadowMatrix = shadowProj * shadowView;
		rl.m_ShadowIndex = static_cast<float>(rectLightIndex);
	}
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
	float punctualShadowSize = static_cast<float>(vansConfigration->GetPunctualShadowMapWidth());
	float patchShadowSize = punctualShadowSize / 8;

	uint32_t offset = 0;
	uint32_t size = sizeof(uint32_t) * 4;
	m_LightCounts[0] = static_cast<uint32_t>((std::min)(m_PointLights.size(), static_cast<size_t>(m_MaxPointLightCount)));
	m_LightCounts[1] = static_cast<uint32_t>((std::min)(m_SpotLights.size(), static_cast<size_t>(m_MaxSpotLightCount)));
	m_LightCounts[2] = static_cast<uint32_t>(patchShadowSize);
	m_LightCounts[3] = 8;   // tilesPerRow，阴影 atlas 采样依赖该值，不可复用
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
		for (auto& dl : dirLightsForUpload)
		{
			if (glm::dot(dl.m_Direction, dl.m_Direction) > 1e-6f)
				dl.m_Color = ComputeAtmosphereSunColor(dl.m_Direction, dl.m_Color);
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
		sizeof(VansRectLight) * m_MaxRectLightCount + sizeof(float) * 4;
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
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ lightBufferBinding }, m_LightDataDescriptorSetLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_LightDataDescriptorSetLayout }, m_LightDataDescriptorSets);

	//update descriptor
	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
		{
			m_LightDataDescriptorSets[0],
			PassBinding::CBUFFER_0,
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{
				{
					m_LightBuffer.GetNativeBuffer(),
					0,
					m_LightBuffer.GetBufferSize()
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
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
	memset(m_LightCounts, 0, sizeof(m_LightCounts));
	memset(m_SoftShadowParams, 0, sizeof(m_SoftShadowParams));
}

void VansGraphics::VansLightManager::DestroyGPUResources(VkDevice device)
{
	m_LightBuffer.DestroyVulkanBuffer(device);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(m_LightDataDescriptorSets);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(m_LightDataDescriptorSetLayout);
}
