#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Vans
{
enum class VansLocalFogTextureProjection
{
	LocalXZ
};

enum class VansLocalFogTextureAddressMode
{
	ClampToBorderZero,
	ClampToEdge,
	Repeat
};

enum class VansLocalFogTextureChannel
{
	R,
	G,
	B,
	A
};

struct VansLocalFogFieldTextureImportSettings
{
	bool linear = false;
	bool compressed = true;
	bool mipmapped = false;
	int channelCount = 0;
	std::string precision = "low8";
};

inline bool IsSupportedLocalFogFieldChannelCount(int channelCount)
{
	return channelCount == 1 || channelCount == 2 || channelCount == 4;
}

inline bool IsSupportedLocalFogFieldPrecision(const std::string& precision)
{
	return precision.empty() || precision == "low8" ||
		precision == "8" || precision == "rgba8";
}

inline bool ValidateLocalFogFieldTextureImportSettings(
	const VansLocalFogFieldTextureImportSettings& settings,
	int requiredChannelCount)
{
	return settings.linear && !settings.compressed && settings.mipmapped &&
		IsSupportedLocalFogFieldChannelCount(settings.channelCount) &&
		settings.channelCount >= std::clamp(requiredChannelCount, 1, 4) &&
		IsSupportedLocalFogFieldPrecision(settings.precision);
}

struct VansLocalFogScalarTextureSourceConfig
{
	std::string assetGuid;
	VansLocalFogTextureChannel channel = VansLocalFogTextureChannel::R;
};

struct VansLocalFogVector2TextureSourceConfig
{
	std::string assetGuid;
	VansLocalFogTextureChannel xChannel = VansLocalFogTextureChannel::R;
	VansLocalFogTextureChannel zChannel = VansLocalFogTextureChannel::G;
};

struct VansLocalFogTextureMapping2DConfig
{
	VansLocalFogTextureProjection projection = VansLocalFogTextureProjection::LocalXZ;
	std::array<float, 2> tiling{ 1.0f, 1.0f };
	std::array<float, 2> offset{ 0.0f, 0.0f };
	VansLocalFogTextureAddressMode addressMode = VansLocalFogTextureAddressMode::Repeat;
};

struct VansLocalFogScalarDensityLayerConfig
{
	bool enabled = false;
	VansLocalFogScalarTextureSourceConfig source{};
	VansLocalFogTextureMapping2DConfig mapping{};
	float inputMinimum = 0.0f;
	float inputMaximum = 1.0f;
	float influence = 1.0f;
	float lodBias = 0.0f;
	bool invert = false;
};

inline VansLocalFogScalarDensityLayerConfig MakeLocalFogScalarLayerDefaults(
	VansLocalFogTextureAddressMode addressMode)
{
	VansLocalFogScalarDensityLayerConfig result;
	result.mapping.addressMode = addressMode;
	return result;
}

struct VansLocalFogFlowConfig
{
	bool enabled = false;
	VansLocalFogVector2TextureSourceConfig source{};
	VansLocalFogTextureMapping2DConfig mapping{
		VansLocalFogTextureProjection::LocalXZ,
		{ 1.0f, 1.0f },
		{ 0.0f, 0.0f },
		VansLocalFogTextureAddressMode::ClampToEdge };
	std::array<float, 2> fallbackDirectionLocalXZ{ 1.0f, 0.0f };
	float speedMetersPerSecond = 0.0f;
	float loopDistanceMeters = 1.0f;
	float phaseOffset01 = 0.0f;
	float lodBias = 0.0f;
};

// LocalVolumetricFog 是实体组件；位置、旋转与尺寸来自实体 Transform，
// 其中 Transform.scale 表示盒体的完整世界尺寸（米）。
struct VansSceneLocalVolumetricFogComponentConfig
{
	bool enabled = true;
	float visibilityDistanceMeters = 80.0f;
	std::array<float, 3> singleScatteringAlbedo{ 0.95f, 0.97f, 1.0f };
	float anisotropy = 0.2f;
	std::array<float, 3> emissivePerMeter{ 0.0f, 0.0f, 0.0f };
	float edgeFadeDistanceMeters = 2.0f;
	float distanceFadeStartMeters = 0.0f;
	float distanceFadeEndMeters = 2000.0f;
	float directLightingScale = 1.0f;
	float skyLightingScale = 1.0f;
	bool receiveCloudShadows = true;
	VansLocalFogScalarDensityLayerConfig shapeMask = MakeLocalFogScalarLayerDefaults(
		VansLocalFogTextureAddressMode::ClampToBorderZero);
	VansLocalFogScalarDensityLayerConfig detailNoise = MakeLocalFogScalarLayerDefaults(
		VansLocalFogTextureAddressMode::Repeat);
	VansLocalFogFlowConfig flow{};
};

inline const char* ToString(VansLocalFogTextureProjection projection)
{
	switch (projection)
	{
	case VansLocalFogTextureProjection::LocalXZ: return "localXZ";
	}
	return "localXZ";
}

inline const char* ToString(VansLocalFogTextureAddressMode addressMode)
{
	switch (addressMode)
	{
	case VansLocalFogTextureAddressMode::ClampToBorderZero: return "clampToBorderZero";
	case VansLocalFogTextureAddressMode::ClampToEdge: return "clampToEdge";
	case VansLocalFogTextureAddressMode::Repeat: return "repeat";
	}
	return "repeat";
}

inline const char* ToString(VansLocalFogTextureChannel channel)
{
	switch (channel)
	{
	case VansLocalFogTextureChannel::R: return "r";
	case VansLocalFogTextureChannel::G: return "g";
	case VansLocalFogTextureChannel::B: return "b";
	case VansLocalFogTextureChannel::A: return "a";
	}
	return "r";
}

inline bool TryParseLocalFogTextureProjection(
	const std::string& value, VansLocalFogTextureProjection& outProjection)
{
	if (value != "localXZ")
		return false;
	outProjection = VansLocalFogTextureProjection::LocalXZ;
	return true;
}

inline bool TryParseLocalFogTextureAddressMode(
	const std::string& value, VansLocalFogTextureAddressMode& outAddressMode)
{
	if (value == "clampToBorderZero")
		outAddressMode = VansLocalFogTextureAddressMode::ClampToBorderZero;
	else if (value == "clampToEdge")
		outAddressMode = VansLocalFogTextureAddressMode::ClampToEdge;
	else if (value == "repeat")
		outAddressMode = VansLocalFogTextureAddressMode::Repeat;
	else
		return false;
	return true;
}

inline bool TryParseLocalFogTextureChannel(
	char value, VansLocalFogTextureChannel& outChannel)
{
	switch (value)
	{
	case 'r': outChannel = VansLocalFogTextureChannel::R; return true;
	case 'g': outChannel = VansLocalFogTextureChannel::G; return true;
	case 'b': outChannel = VansLocalFogTextureChannel::B; return true;
	case 'a': outChannel = VansLocalFogTextureChannel::A; return true;
	default: return false;
	}
}

inline int RequiredLocalFogFieldChannelCount(VansLocalFogTextureChannel channel)
{
	switch (channel)
	{
	case VansLocalFogTextureChannel::R: return 1;
	case VansLocalFogTextureChannel::G: return 2;
	// 当前导入管线没有 RGB artifact；访问 B/A 时必须保留 RGBA。
	case VansLocalFogTextureChannel::B:
	case VansLocalFogTextureChannel::A: return 4;
	}
	return 4;
}

inline int LocalFogTextureChannelIndex(VansLocalFogTextureChannel channel)
{
	switch (channel)
	{
	case VansLocalFogTextureChannel::R: return 0;
	case VansLocalFogTextureChannel::G: return 1;
	case VansLocalFogTextureChannel::B: return 2;
	case VansLocalFogTextureChannel::A: return 3;
	}
	return 0;
}

inline std::array<float, 2> DecodeAndClampLocalFogFlowVector(
	float encodedX,
	float encodedZ,
	float zeroThreshold = 1.5f / 255.0f)
{
	float x = encodedX * 2.0f - 1.0f;
	float z = encodedZ * 2.0f - 1.0f;
	x = std::abs(x) <= zeroThreshold ? 0.0f : x;
	z = std::abs(z) <= zeroThreshold ? 0.0f : z;
	const float length = std::sqrt(x * x + z * z);
	if (length > 1.0f)
	{
		x /= length;
		z /= length;
	}
	return { x, z };
}

inline bool TryParseLocalFogScalarTextureChannels(
	const std::string& value,
	VansLocalFogTextureChannel& outChannel)
{
	return value.size() == 1 &&
		TryParseLocalFogTextureChannel(value[0], outChannel);
}

inline bool TryParseLocalFogVector2TextureChannels(
	const std::string& value,
	VansLocalFogTextureChannel& outXChannel,
	VansLocalFogTextureChannel& outZChannel)
{
	return value.size() == 2 &&
		TryParseLocalFogTextureChannel(value[0], outXChannel) &&
		TryParseLocalFogTextureChannel(value[1], outZChannel) &&
		outXChannel != outZChannel;
}

inline int RequiredLocalFogFieldChannelCount(const std::string& channels)
{
	VansLocalFogTextureChannel channel0{};
	if (TryParseLocalFogScalarTextureChannels(channels, channel0))
		return RequiredLocalFogFieldChannelCount(channel0);
	VansLocalFogTextureChannel channel1{};
	if (!TryParseLocalFogVector2TextureChannels(channels, channel0, channel1))
		return 0;
	return (std::max)(RequiredLocalFogFieldChannelCount(channel0),
		RequiredLocalFogFieldChannelCount(channel1));
}

inline void NormalizeLocalFogTextureMapping(VansLocalFogTextureMapping2DConfig& mapping)
{
	mapping.projection = VansLocalFogTextureProjection::LocalXZ;
	for (float& value : mapping.tiling)
		value = (std::max)(std::isfinite(value) ? value : 1.0f, 0.001f);
	for (float& value : mapping.offset)
		value = std::isfinite(value) ? value : 0.0f;
}

inline void NormalizeLocalFogScalarDensityLayer(VansLocalFogScalarDensityLayerConfig& layer)
{
	NormalizeLocalFogTextureMapping(layer.mapping);
	layer.inputMinimum = std::isfinite(layer.inputMinimum) ? layer.inputMinimum : 0.0f;
	layer.inputMaximum = std::isfinite(layer.inputMaximum) ? layer.inputMaximum : 1.0f;
	layer.inputMaximum = (std::max)(layer.inputMaximum, layer.inputMinimum + 1.0e-5f);
	layer.influence = std::clamp(
		std::isfinite(layer.influence) ? layer.influence : 1.0f, 0.0f, 1.0f);
	layer.lodBias = std::clamp(
		std::isfinite(layer.lodBias) ? layer.lodBias : 0.0f, -16.0f, 16.0f);
}

inline void NormalizeLocalFogFlow(VansLocalFogFlowConfig& flow)
{
	NormalizeLocalFogTextureMapping(flow.mapping);
	flow.speedMetersPerSecond = (std::max)(
		std::isfinite(flow.speedMetersPerSecond) ? flow.speedMetersPerSecond : 0.0f,
		0.0f);
	flow.loopDistanceMeters = (std::max)(
		std::isfinite(flow.loopDistanceMeters) ? flow.loopDistanceMeters : 1.0f,
		0.01f);
	flow.phaseOffset01 = std::isfinite(flow.phaseOffset01)
		? flow.phaseOffset01 - std::floor(flow.phaseOffset01) : 0.0f;
	flow.lodBias = std::clamp(
		std::isfinite(flow.lodBias) ? flow.lodBias : 0.0f, -16.0f, 16.0f);
	float x = std::isfinite(flow.fallbackDirectionLocalXZ[0])
		? flow.fallbackDirectionLocalXZ[0] : 0.0f;
	float z = std::isfinite(flow.fallbackDirectionLocalXZ[1])
		? flow.fallbackDirectionLocalXZ[1] : 0.0f;
	const float length = std::sqrt(x * x + z * z);
	if (length > 1.0e-6f)
	{
		x /= length;
		z /= length;
	}
	else
	{
		x = 0.0f;
		z = 0.0f;
	}
	flow.fallbackDirectionLocalXZ = { x, z };
}

inline void NormalizeLocalVolumetricFogConfig(
	VansSceneLocalVolumetricFogComponentConfig& config)
{
	config.visibilityDistanceMeters = (std::max)(config.visibilityDistanceMeters, 0.1f);
	config.anisotropy = std::clamp(config.anisotropy, -0.95f, 0.95f);
	config.edgeFadeDistanceMeters = (std::max)(config.edgeFadeDistanceMeters, 0.0f);
	config.distanceFadeStartMeters = (std::max)(config.distanceFadeStartMeters, 0.0f);
	config.distanceFadeEndMeters = (std::max)(
		config.distanceFadeEndMeters, config.distanceFadeStartMeters + 0.01f);
	config.directLightingScale = (std::max)(config.directLightingScale, 0.0f);
	config.skyLightingScale = (std::max)(config.skyLightingScale, 0.0f);
	for (float& value : config.singleScatteringAlbedo)
		value = std::clamp(value, 0.0f, 1.0f);
	for (float& value : config.emissivePerMeter)
		value = (std::max)(value, 0.0f);
	NormalizeLocalFogScalarDensityLayer(config.shapeMask);
	NormalizeLocalFogScalarDensityLayer(config.detailNoise);
	NormalizeLocalFogFlow(config.flow);
}
}
