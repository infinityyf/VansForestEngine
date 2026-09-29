#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"

namespace Vans
{
// Scene 配置、运行时设置和 Editor DTO 共用同一组作者字段写出规则，
// 确保天气面板不会形成第二套雨天 schema。
template <typename RainSettings>
VansSerializedValue WriteSceneRainSettings(const RainSettings& settings)
{
	return VansSerializedValue::Object({
		{ "enabled", VansSerializedValue::Bool(settings.enabled) },
		{ "rainRateMmPerHour", VansSerializedValue::Float(settings.rainRateMmPerHour) },
		{ "fullIntensityRateMmPerHour", VansSerializedValue::Float(settings.fullIntensityRateMmPerHour) },
		{ "wettingHalfLifeSeconds", VansSerializedValue::Float(settings.wettingHalfLifeSeconds) },
		{ "dryingHalfLifeSeconds", VansSerializedValue::Float(settings.dryingHalfLifeSeconds) },
		{ "puddleFillHalfLifeSeconds", VansSerializedValue::Float(settings.puddleFillHalfLifeSeconds) },
		{ "puddleDrainHalfLifeSeconds", VansSerializedValue::Float(settings.puddleDrainHalfLifeSeconds) },
		{ "windDirectionX", VansSerializedValue::Float(settings.windDirectionX) },
		{ "windDirectionZ", VansSerializedValue::Float(settings.windDirectionZ) },
		{ "windSpeedMetersPerSecond", VansSerializedValue::Float(settings.windSpeedMetersPerSecond) },
		{ "fallSpeedMetersPerSecond", VansSerializedValue::Float(settings.fallSpeedMetersPerSecond) },
		{ "maximumVisibleDistanceMeters", VansSerializedValue::Float(settings.maximumVisibleDistanceMeters) },
		{ "splashLifetimeSeconds", VansSerializedValue::Float(settings.splashLifetimeSeconds) },
		{ "splashRadiusMeters", VansSerializedValue::Float(settings.splashRadiusMeters) },
		{ "rippleScaleMeters", VansSerializedValue::Float(settings.rippleScaleMeters) },
		{ "rippleStrength", VansSerializedValue::Float(settings.rippleStrength) }
	});
}
}
