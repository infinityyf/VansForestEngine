#pragma once

#include "VansTemporalProjection.h"

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace VansGraphics
{
	struct VansTemporalCameraSnapshot final
	{
		glm::mat4 view{ 1.0f };
		glm::mat4 projection{ 1.0f };
		glm::mat4 previousViewProjection{ 1.0f };
		glm::vec3 position{ 0.0f };
		glm::vec3 up{ 0.0f, 1.0f, 0.0f };
		glm::vec3 right{ 1.0f, 0.0f, 0.0f };
		glm::vec3 forward{ 0.0f, 0.0f, -1.0f };
		VansTemporalJitter jitter{};
		std::uint32_t frameIndex = 0;
		float nearClip = 0.1f;
		float farClip = 1000.0f;
		float fovRadians = 1.0f;
	};

	// Backend-ready data. Ownership belongs to the renderer, never to the
	// logical camera or Scene/Timeline component model.
	struct alignas(16) VansCameraDataGPU final
	{
		glm::vec4 cameraPosition;
		glm::vec4 cameraDirection;
		glm::mat4 viewMatrix;
		glm::mat4 projectionMatrix;
		glm::mat4 viewProjectionMatrix;
		glm::mat4 lastViewMatrix;
		glm::mat4 lastProjectionMatrix;
		glm::mat4 lastViewProjectionMatrix;
		glm::mat4 lastPreviousViewMatrix;
		glm::mat4 lastPreviousProjectionMatrix;
		glm::mat4 lastPreviousViewProjectionMatrix;
		glm::mat4 inverseViewMatrix;
		glm::mat4 inverseProjectionMatrix;
		glm::mat4 unjitteredViewProjectionMatrix;
		glm::mat4 lastUnjitteredViewProjectionMatrix;
		glm::vec4 screenParams;
		glm::vec4 frameParams;
		glm::vec4 cameraParams;
	};

	static_assert(alignof(VansCameraDataGPU) == 16);
	static_assert(sizeof(VansCameraDataGPU) == 912);
	static_assert(offsetof(VansCameraDataGPU, cameraPosition) == 0);
	static_assert(offsetof(VansCameraDataGPU, cameraDirection) == 16);
	static_assert(offsetof(VansCameraDataGPU, viewMatrix) == 32);
	static_assert(offsetof(VansCameraDataGPU, projectionMatrix) == 96);
	static_assert(offsetof(VansCameraDataGPU, viewProjectionMatrix) == 160);
	static_assert(offsetof(VansCameraDataGPU, lastViewMatrix) == 224);
	static_assert(offsetof(VansCameraDataGPU, lastPreviousViewMatrix) == 416);
	static_assert(offsetof(VansCameraDataGPU, inverseViewMatrix) == 608);
	static_assert(offsetof(VansCameraDataGPU, unjitteredViewProjectionMatrix) == 736);
	static_assert(offsetof(VansCameraDataGPU, screenParams) == 864);
	static_assert(offsetof(VansCameraDataGPU, frameParams) == 880);
	static_assert(offsetof(VansCameraDataGPU, cameraParams) == 896);
}
