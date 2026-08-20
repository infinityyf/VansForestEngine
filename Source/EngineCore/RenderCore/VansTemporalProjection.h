#pragma once

#include "../ScriptCore/VansCommonUtils.h"
#include <../../GLM/vec2.hpp>

#include <cmath>

namespace VansGraphics
{
	struct VansTemporalJitter
	{
		glm::vec2 samplePixels{};
		glm::vec2 ndcOffset{};
		glm::vec2 framebufferPixels{};
		bool valid = false;
	};

	inline VansTemporalJitter BuildVulkanTemporalJitter(
		const glm::vec2 samplePixels,
		const glm::vec2 renderSize)
	{
		VansTemporalJitter result{};
		if (!std::isfinite(samplePixels.x) || !std::isfinite(samplePixels.y) ||
			!std::isfinite(renderSize.x) || !std::isfinite(renderSize.y) ||
			renderSize.x <= 0.0f || renderSize.y <= 0.0f)
		{
			return result;
		}

		result.samplePixels = samplePixels;
		// ForestEngine 的 Vulkan viewport 使用负高度，因此 NDC Y 与 framebuffer Y
		// 方向相反。该变换保证最终 framebuffer 位移与 SDK 像素样本同向。
		result.ndcOffset = glm::vec2(
			2.0f * samplePixels.x / renderSize.x,
			-2.0f * samplePixels.y / renderSize.y);
		result.framebufferPixels = glm::vec2(
			result.ndcOffset.x * renderSize.x * 0.5f,
			-result.ndcOffset.y * renderSize.y * 0.5f);
		result.valid = true;
		return result;
	}

	inline glm::mat4 ApplyClipSpaceJitter(
		const glm::mat4& unjitteredProjection,
		const glm::vec2 ndcOffset)
	{
		glm::mat4 jitterTranslation(1.0f);
		jitterTranslation[3][0] = ndcOffset.x;
		jitterTranslation[3][1] = ndcOffset.y;
		return jitterTranslation * unjitteredProjection;
	}

	struct VansDeviceDepthRange
	{
		float nearDistance = 0.0f;
		float farDistance = 0.0f;
		bool finiteFar = true;
		bool valid = false;
	};

	inline bool IsFiniteHomogeneousPoint(const glm::vec4& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) &&
			std::isfinite(value.z) && std::isfinite(value.w);
	}

	inline VansDeviceDepthRange ExtractVulkanDeviceDepthRange(
		const glm::mat4& unjitteredProjection)
	{
		constexpr float kHomogeneousEpsilon = 1.0e-6f;
		const glm::mat4 inverseProjection = glm::inverse(unjitteredProjection);
		const glm::vec4 nearH = inverseProjection * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		const glm::vec4 farH = inverseProjection * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

		VansDeviceDepthRange result{};
		if (!IsFiniteHomogeneousPoint(nearH) || std::abs(nearH.w) <= kHomogeneousEpsilon)
			return result;

		result.nearDistance = std::abs(nearH.z / nearH.w);
		if (!IsFiniteHomogeneousPoint(farH))
			return result;

		if (std::abs(farH.w) <= kHomogeneousEpsilon)
		{
			result.finiteFar = false;
		}
		else
		{
			result.farDistance = std::abs(farH.z / farH.w);
		}

		result.valid = std::isfinite(result.nearDistance) && result.nearDistance > 0.0f &&
			(!result.finiteFar ||
				(std::isfinite(result.farDistance) && result.farDistance > result.nearDistance));
		return result;
	}
}
