#pragma once

#include "../ShadowCore/VansPunctualShadowTypes.h"

#include <cstdint>
#include <glm/glm.hpp>

namespace VansGraphics
{
	inline constexpr uint32_t VANS_MAX_DIRECTION_LIGHTS = 1;
	inline constexpr uint32_t VANS_MAX_POINT_LIGHTS = 64;
	inline constexpr uint32_t VANS_MAX_SPOT_LIGHTS = 64;
	inline constexpr uint32_t VANS_MAX_RECT_LIGHTS = 32;

	enum class VansLightType
	{
		DIRECTIONAL = 0,
		POINT = 1,
		SPOT = 2,
		RECT = 3
	};

	// CPU/GPU 共享的灯光 ABI；只包含逐帧值，不拥有任何 backend 资源。
	struct alignas(16) VansDirectionalLight
	{
		glm::vec3 m_Direction;
		alignas(16) glm::vec3 m_Color;
		alignas(16) float m_Intensity;
		float padding[3];
		glm::mat4x4 m_ShadowMatrix[4];
		glm::vec4 m_CascadeSplits;
		glm::vec4 m_CascadeTexelSize;
		glm::vec4 m_CascadeDepthScale;
		glm::vec4 m_CascadeNormalBias;
		glm::vec4 m_CascadeFilterRadius;
	};

	struct VansCascadeCameraData
	{
		glm::vec3 position;
		glm::vec3 forward;
		glm::vec3 up;
		float verticalFovRadians;
		float aspectRatio;
		float nearPlane;
		float farPlane;
		uint32_t viewportWidth = 1920;
		uint32_t viewportHeight = 1080;
	};

	struct VansCelestialLightingState
	{
		glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 moonDirection = glm::vec3(0.0f, -1.0f, 0.0f);
		glm::vec3 direction = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 color = glm::vec3(1.0f);
		float intensity = 1.0f;
		float skyDiffuseScale = 1.0f;
		float skySpecularScale = 1.0f;
		float moonBlend = 0.0f;
	};

	struct alignas(16) VansPointLight
	{
		glm::vec3 m_Position;
		alignas(16) glm::vec3 m_Color;
		alignas(16) float m_Intensity;
		float m_Radius;
		uint32_t m_ShadowMetaIndex;
		float m_IESProfileIndex;
	};

	struct alignas(16) VansSpotLight
	{
		glm::vec3 m_Position;
		alignas(16) glm::vec3 m_Direction;
		alignas(16) glm::vec3 m_Color;
		alignas(16) float m_Intensity;
		float m_Radius;
		float m_InnerCutOff;
		float m_OuterCutOff;
		uint32_t m_ShadowMetaIndex;
		float m_IESProfileIndex;
		float m_IESIntensityScale;
		float m_pad0;
	};

	struct alignas(16) VansRectLight
	{
		glm::vec3 m_Position;
		float m_HalfWidth;
		glm::vec3 m_Normal;
		float m_HalfHeight;
		glm::vec3 m_Right;
		float m_Range;
		glm::vec3 m_Up;
		float m_Intensity;
		glm::vec3 m_Color;
		float m_TwoSided;
		uint32_t m_ShadowMetaIndex;
		float m_AttenuationExp;
		float m_TextureSlot;
		float m_TexLodBias;
	};

	static_assert(sizeof(VansPointLight) == 48, "VansPointLight must match GLSL std430 layout");
	static_assert(sizeof(VansSpotLight) == 80, "VansSpotLight must match GLSL std430 layout");
	static_assert(sizeof(VansRectLight) == 96, "VansRectLight must match GLSL std430 layout");

	struct VansPunctualShadowRegistration
	{
		uint32_t stableLightId = 0;
		VansPunctualShadowSettings settings;
	};
}
