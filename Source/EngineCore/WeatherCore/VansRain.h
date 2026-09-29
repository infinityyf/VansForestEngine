#pragma once

#include <string>

namespace Vans
{
struct VansRainSettings final
{
	bool enabled = false;
	float rainRateMmPerHour = 0.0f;
	float fullIntensityRateMmPerHour = 25.0f;
	float wettingHalfLifeSeconds = 4.0f;
	float dryingHalfLifeSeconds = 120.0f;
	float puddleFillHalfLifeSeconds = 45.0f;
	float puddleDrainHalfLifeSeconds = 360.0f;
	float windDirectionX = 0.35f;
	float windDirectionZ = 0.94f;
	float windSpeedMetersPerSecond = 2.0f;
	float fallSpeedMetersPerSecond = 8.0f;
	float maximumVisibleDistanceMeters = 45.0f;
	float splashLifetimeSeconds = 0.12f;
	float splashRadiusMeters = 0.09f;
	float rippleScaleMeters = 20.0f;
	float rippleStrength = 1.0f;
};

struct VansRainState final
{
	bool enabled = false;
	float normalizedIntensity = 0.0f;
	float filmWetness = 0.0f;
	float puddleFill = 0.0f;
	double visualTimeSeconds = 0.0;
};

bool ValidateRainSettings(const VansRainSettings& settings, std::string& error);

// Main-thread weather-domain state. It owns only deterministic scalar evolution;
// render resources and shader buffers remain in RenderCore.
class VansRainRuntime final
{
public:
	void Configure(const VansRainSettings& settings);
	// 作者预览只替换配置并保留已经积累的雨膜/积水状态；场景装载和卸载
	// 仍通过 Configure 建立全新的生命周期状态。
	void ApplySettings(const VansRainSettings& settings);
	// 为编辑器和确定性恢复提供一次运行时状态写入；不会改变雨量配置或累计规则。
	bool SetPuddleFill(float puddleFill);
	void Update(float deltaSeconds);
	const VansRainSettings& Settings() const { return m_Settings; }
	const VansRainState& State() const { return m_State; }

private:
	VansRainSettings m_Settings;
	VansRainState m_State;
};
}
