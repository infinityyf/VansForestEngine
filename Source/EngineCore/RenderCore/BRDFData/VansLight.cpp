#include "VansLight.h"
#include "../../../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../../EngineCore/Configration/VansConfigration.h"
#include "../../../EngineCore/VansTimer.h"
#include "../VansCamera.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>

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
		const size_t copyCount = std::min<size_t>(lights.size(), maxCount);
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

void VansGraphics::VansLightManager::UpdateLightShadowMatrixData(const glm::vec3& cameraPosition)
{
	auto vansConfig = VansConfigration::GetInstance();
	int cascadeCount = vansConfig->GetCascadeCount();
	const float* cascadeSplits = vansConfig->GetCascadeSplits();
	int cascadeMapSize = vansConfig->GetCascadeShadowMapSize();

	int directionLightCount = m_DirectionalLights.size();
	for (int dirLightIndex = 0; dirLightIndex < directionLightCount; dirLightIndex++)
	{
		auto lightDir = glm::normalize(m_DirectionalLights[dirLightIndex].m_Direction);

		// Store cascade split distances for shader usage
		m_DirectionalLights[dirLightIndex].m_CascadeSplits = glm::vec4(
			cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], cascadeSplits[3]);

		// Coverage margin: each cascade covers 1.5x its split distance
		const float coverageMargin = 1.5f;

		for (int cascade = 0; cascade < cascadeCount; ++cascade)
		{
			float halfExtent = cascadeSplits[cascade] * coverageMargin;
			float texelSize = 2.0f * halfExtent / (float)cascadeMapSize;

			// Build light view matrix centered on the camera position.
			// The light "eye" is placed far behind the camera along the light direction
			// so the ortho frustum covers geometry around where the camera is looking.
			glm::vec3 lightEye = cameraPosition + lightDir * halfExtent * 2.0f;

			// 使用 ChooseStableUpVector 以避免 lightDir 接近垂直时 cross(forward, up) 退化。
			// 修复：原来直接比较 0.99f（仅约 8°）在临界角度附近会产生不稳定的硬切，
			// 这里统一通过已有的 ChooseStableUpVector 函数处理，阈值为 0.9962（约 5°）。
			glm::vec3 up = ChooseStableUpVector(lightDir);
			glm::mat4x4 viewMatrix = glm::lookAt(lightEye, cameraPosition, up);

			// 将摄像机位置投影到光源视图空间，对正交投影坐标按 texelSize 对齐以减少阴影游泳。
			// 修复：原来使用 std::fmod，对负数返回负余数，导致反向偏移；
			// 改为 floor 对齐，保证 snapOffset 始终非负且方向正确。
			glm::vec4 camLS = viewMatrix * glm::vec4(cameraPosition, 1.0f);
			float snapOffsetX = camLS.x - std::floor(camLS.x / texelSize) * texelSize;
			float snapOffsetY = camLS.y - std::floor(camLS.y / texelSize) * texelSize;
			glm::mat4x4 snapMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(-snapOffsetX, -snapOffsetY, 0.0f));

			// Generous Z range to capture shadow casters behind the camera (from the light's perspective)
			float zRange = halfExtent * 4.0f;
			glm::mat4x4 projectionMatrix = glm::ortho<float>(
				-halfExtent, halfExtent,
				-halfExtent, halfExtent,
				-zRange, zRange);

			m_DirectionalLights[dirLightIndex].m_ShadowMatrix[cascade] = projectionMatrix * snapMatrix * viewMatrix;
		}
	}

	int pointLightCount = m_PointLights.size();
	for (int pointLightIndex = 0; pointLightIndex < pointLightCount; pointLightIndex++)
	{
		glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.001f, m_PointLights[pointLightIndex].m_Radius);
		glm::vec3 lightPos = m_PointLights[pointLightIndex].m_Position;

		m_PointLights[pointLightIndex].m_PointShadowMatrix[0] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(1, 0, 0), glm::vec3(0, -1, 0)); // +X

		m_PointLights[pointLightIndex].m_PointShadowMatrix[1] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0)); // -X

		m_PointLights[pointLightIndex].m_PointShadowMatrix[2] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)); // +Y

		m_PointLights[pointLightIndex].m_PointShadowMatrix[3] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, -1, 0), glm::vec3(0, 0, -1)); // -Y

		m_PointLights[pointLightIndex].m_PointShadowMatrix[4] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 0, 1), glm::vec3(0, -1, 0)); // +Z
		
		m_PointLights[pointLightIndex].m_PointShadowMatrix[5] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 0, -1), glm::vec3(0, -1, 0)); // -Z
		
		m_PointLights[pointLightIndex].m_ShadowIndex = pointLightIndex;
	}
	int spotLightCount = m_SpotLights.size();
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
		m_SpotLights[spotLightIndex].m_ShadowIndex = spotLightIndex;
	}

	// ── RectLight VP 矩阵生成（Phase 3 shadow 使用，与 shadow 开关无关，预先计算）──────────
	int rectLightCount = (int)std::min<size_t>(m_RectLights.size(), m_MaxRectLightCount);
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
		rl.m_ShadowIndex = (float)rectLightIndex;
	}
}

void VansGraphics::VansLightManager::UpdateLightCPUData()
{
	//for (int spotLightIndex = 0; spotLightIndex < m_SpotLights.size(); spotLightIndex++)
	//{

	//	m_SpotLights[spotLightIndex].m_Position.x = std::sin(VansTimer::GetFrameTime() * 0.5f) * 6;
	//}

	auto vansConfigration = VansConfigration::GetInstance();
	float punctualShadowSize = vansConfigration->GetPunctualShadowMapWidth();
	float patchShadowSize = punctualShadowSize / 8;

	uint32_t offset = 0;
	uint32_t size = sizeof(uint32_t) * 4;
	m_LightCounts[0] = static_cast<uint32_t>(std::min<size_t>(m_PointLights.size(), m_MaxPointLightCount));
	m_LightCounts[1] = static_cast<uint32_t>(std::min<size_t>(m_SpotLights.size(), m_MaxSpotLightCount));
	m_LightCounts[2] = patchShadowSize;
	m_LightCounts[3] = 8;   // tilesPerRow，阴影 atlas 采样依赖该值，不可复用
	m_LightBuffer.SetBufferData(m_LightCounts, offset, size);
	offset += size;
	size = sizeof(float) * 4;
	m_SoftShadowParams[0] = m_SoftShadowParams[0] + 1;
	m_SoftShadowParams[1] = 0.3; // 软阴影半径控制
	// softShadowParams.z = RectLight 计数（shader 以 uint(softShadowParams.z) 读取）
	m_SoftShadowParams[2] = static_cast<float>(std::min<size_t>(m_RectLights.size(), m_MaxRectLightCount));
	m_SoftShadowParams[3] = 0;
	m_LightBuffer.SetBufferData(m_SoftShadowParams, offset, size);
	offset += size;
	size = sizeof(VansDirectionalLight) * m_MaxDirectionLightCount;
	UploadPaddedLightData(m_LightBuffer, offset, m_MaxDirectionLightCount, m_DirectionalLights);
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
