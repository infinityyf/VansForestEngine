#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace VansGraphics
{
	// 地表天气是渲染帧契约，不拥有天气演化或地形资产。WeatherCore 和
	// Terrain 分别发布动态状态与持久形态，RenderCore 只在帧边界合成。
	struct VansSurfaceWeatherFrameData final
	{
		bool enabled = false;
		float normalizedIntensity = 0.0f;
		float filmWetness = 0.0f;
		float puddleFill = 0.0f;
		glm::vec2 windDirection{ 0.35f, 0.94f };
		float windSpeedMetersPerSecond = 2.0f;
		float fallSpeedMetersPerSecond = 8.0f;
		float maximumVisibleDistanceMeters = 45.0f;
		float splashLifetimeSeconds = 0.12f;
		float splashRadiusMeters = 0.09f;
		float rippleScaleMeters = 20.0f;
		float rippleStrength = 1.0f;
		float visualTimeSeconds = 0.0f;

		// Terrain 资产是以下持久参数的唯一来源；道路等接收者只读帧副本。
		float puddleScaleMeters = 18.0f;
		float puddleDetailScale = 3.0f;
		float puddleThreshold = 0.58f;
		float puddleSoftness = 0.12f;
		// A ground surface has no authored puddle distribution until the active
		// terrain provider supplies one. Water ripple does not depend on this value.
		float puddleStrength = 0.0f;
		float puddleSeed = 0.0f;
		float wetAlbedoScale = 0.72f;
		float wetRoughness = 0.18f;
		float puddleRoughness = 0.045f;
		float puddleFresnel0 = 0.02f;
	};

	enum VansGroundWeatherEffect : std::uint32_t
	{
		VANS_GROUND_WEATHER_NONE = 0,
		VANS_GROUND_WEATHER_WET_FILM = 1u << 0u,
		VANS_GROUND_WEATHER_PUDDLE = 1u << 1u,
		VANS_GROUND_WEATHER_RIPPLE = 1u << 2u,
		VANS_GROUND_WEATHER_ALL = VANS_GROUND_WEATHER_WET_FILM |
			VANS_GROUND_WEATHER_PUDDLE | VANS_GROUND_WEATHER_RIPPLE
	};
}
