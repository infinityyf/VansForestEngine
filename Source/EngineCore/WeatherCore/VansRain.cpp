#include "VansRain.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
float ApproachHalfLife(float current, float target, float halfLifeSeconds, float deltaSeconds)
{
	if (!(deltaSeconds > 0.0f))
		return current;
	const float retain = std::exp2(-deltaSeconds / halfLifeSeconds);
	return std::clamp(target + (current - target) * retain, 0.0f, 1.0f);
}

float SmoothStep(float edge0, float edge1, float value)
{
	const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
}

bool ValidateRainSettings(const VansRainSettings& settings, std::string& error)
{
	error.clear();
	if (!std::isfinite(settings.rainRateMmPerHour) || settings.rainRateMmPerHour < 0.0f)
	{
		error = "Rain rate must be finite and nonnegative";
		return false;
	}
	if (!std::isfinite(settings.fullIntensityRateMmPerHour) ||
		settings.fullIntensityRateMmPerHour <= 0.0f)
	{
		error = "Rain full-intensity rate must be finite and positive";
		return false;
	}
	const float halfLives[] = {
		settings.wettingHalfLifeSeconds,
		settings.dryingHalfLifeSeconds,
		settings.puddleFillHalfLifeSeconds,
		settings.puddleDrainHalfLifeSeconds
	};
	for (const float halfLife : halfLives)
	{
		if (!std::isfinite(halfLife) || halfLife <= 0.0f)
		{
			error = "Rain wetting, drying, puddle-fill and puddle-drain half-lives must be finite and positive";
			return false;
		}
	}
	const float effectValues[] = {
		settings.windDirectionX,
		settings.windDirectionZ,
		settings.windSpeedMetersPerSecond,
		settings.fallSpeedMetersPerSecond,
		settings.maximumVisibleDistanceMeters,
		settings.splashLifetimeSeconds,
		settings.splashRadiusMeters,
		settings.rippleScaleMeters,
		settings.rippleStrength
	};
	for (const float value : effectValues)
	{
		if (!std::isfinite(value))
		{
			error = "Rain effect parameters must be finite";
			return false;
		}
	}
	const float windLengthSquared = settings.windDirectionX * settings.windDirectionX +
		settings.windDirectionZ * settings.windDirectionZ;
	if (windLengthSquared <= 1.0e-6f ||
		settings.windSpeedMetersPerSecond < 0.0f ||
		settings.fallSpeedMetersPerSecond <= 0.0f ||
		settings.maximumVisibleDistanceMeters <= 0.0f ||
		settings.splashLifetimeSeconds <= 0.0f ||
		settings.splashRadiusMeters <= 0.0f ||
		settings.rippleScaleMeters <= 0.0f ||
		settings.rippleStrength < 0.0f || settings.rippleStrength > 1.0f)
	{
		error = "Rain direction and visual-effect ranges are invalid";
		return false;
	}
	return true;
}

void VansRainRuntime::Configure(const VansRainSettings& settings)
{
	m_Settings = settings;
	m_State = {};
	m_State.enabled = settings.enabled;
}

void VansRainRuntime::ApplySettings(const VansRainSettings& settings)
{
	m_Settings = settings;
	m_State.enabled = settings.enabled;
	m_State.normalizedIntensity = settings.enabled
		? std::clamp(settings.rainRateMmPerHour /
			settings.fullIntensityRateMmPerHour, 0.0f, 1.0f)
		: 0.0f;
}

bool VansRainRuntime::SetPuddleFill(float puddleFill)
{
	if (!std::isfinite(puddleFill) || puddleFill < 0.0f || puddleFill > 1.0f)
		return false;
	m_State.puddleFill = puddleFill;
	return true;
}

void VansRainRuntime::Update(float deltaSeconds)
{
	if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
		return;
	m_State.visualTimeSeconds += static_cast<double>(deltaSeconds);
	const float intensity = m_Settings.enabled
		? std::clamp(m_Settings.rainRateMmPerHour /
			m_Settings.fullIntensityRateMmPerHour, 0.0f, 1.0f)
		: 0.0f;
	const float puddleTarget = SmoothStep(0.15f, 0.75f, intensity);

	m_State.enabled = m_Settings.enabled;
	m_State.normalizedIntensity = intensity;
	m_State.filmWetness = ApproachHalfLife(
		m_State.filmWetness,
		intensity,
		intensity > m_State.filmWetness
			? m_Settings.wettingHalfLifeSeconds
			: m_Settings.dryingHalfLifeSeconds,
		deltaSeconds);
	m_State.puddleFill = ApproachHalfLife(
		m_State.puddleFill,
		puddleTarget,
		puddleTarget > m_State.puddleFill
			? m_Settings.puddleFillHalfLifeSeconds
			: m_Settings.puddleDrainHalfLifeSeconds,
		deltaSeconds);
}
}
