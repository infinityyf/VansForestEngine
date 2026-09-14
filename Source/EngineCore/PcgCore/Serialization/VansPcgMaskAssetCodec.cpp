#include "VansPcgMaskAssetCodec.h"
#include "VansPcgValueCodec.h"
#include "../../AssetCore/Serialization/VansDataPngEncoder.h"

#include <stb_image.h>
#include <limits>

namespace Vans
{
bool VansPcgMaskAssetCodec::DecodeDefinition(const VansSerializedValue& root, VansPcgMaskAsset& asset, std::string& error)
{
	error.clear();
	VansPcgMaskAsset decoded;
	PcgValue::Reader reader(&root, "mask", error);
	reader.String("name", decoded.name);
	reader.Reference("pixels", "texture", decoded.pixelAsset);
	auto owner = reader.Object("owner");
	owner.String("region", decoded.mask.target.regionId);
	owner.String("layer", decoded.mask.target.layerId);
	owner.String("mask", decoded.mask.target.maskId);
	owner.Finish();
	auto bounds = reader.Object("bounds");
	bounds.Vector("min", decoded.mask.bounds.min);
	bounds.Vector("max", decoded.mask.bounds.max);
	bounds.Finish();
	reader.IntegerField("width", decoded.mask.width);
	reader.IntegerField("height", decoded.mask.height);
	auto brush = reader.Object("brush");
	brush.EnumField("operation", decoded.brush.operation, { { "add", VansPcgBrushOperation::Add },
		{ "subtract", VansPcgBrushOperation::Subtract }, { "set", VansPcgBrushOperation::Set },
		{ "smooth", VansPcgBrushOperation::Smooth }, { "erase", VansPcgBrushOperation::Erase } });
	brush.Float("radius", decoded.brush.radius);
	brush.Float("strength", decoded.brush.strength);
	brush.Float("hardness", decoded.brush.hardness);
	brush.Float("targetValue", decoded.brush.targetValue);
	brush.Float("spacingFraction", decoded.brush.spacingFraction);
	brush.Finish();
	if (!reader.Finish()) return false;
	const auto diagnostics = ValidatePcgMaskAsset(decoded, false);
	if (!diagnostics.empty()) { error = diagnostics.front(); return false; }
	asset = std::move(decoded);
	return true;
}

bool VansPcgMaskAssetCodec::EncodeDefinition(const VansPcgMaskAsset& asset, VansSerializedValue& root, std::string& error)
{
	error.clear();
	const auto diagnostics = ValidatePcgMaskAsset(asset, false);
	if (!diagnostics.empty()) { error = diagnostics.front(); return false; }
	using Value = VansSerializedValue;
	root = Value::Object({
		{ "name", Value::String(asset.name) }, { "pixels", PcgValue::Reference(asset.pixelAsset, "texture") },
		{ "owner", Value::Object({ { "region", Value::String(asset.mask.target.regionId) },
			{ "layer", Value::String(asset.mask.target.layerId) }, { "mask", Value::String(asset.mask.target.maskId) } }) },
		{ "bounds", Value::Object({ { "min", PcgValue::Vector(asset.mask.bounds.min) }, { "max", PcgValue::Vector(asset.mask.bounds.max) } }) },
		{ "width", Value::Int(asset.mask.width) }, { "height", Value::Int(asset.mask.height) },
		{ "brush", Value::Object({ { "operation", Value::String(asset.brush.operation == VansPcgBrushOperation::Add ? "add" :
			asset.brush.operation == VansPcgBrushOperation::Subtract ? "subtract" : asset.brush.operation == VansPcgBrushOperation::Set ? "set" :
			asset.brush.operation == VansPcgBrushOperation::Smooth ? "smooth" : "erase") },
			{ "radius", Value::Float(asset.brush.radius) }, { "strength", Value::Float(asset.brush.strength) },
			{ "hardness", Value::Float(asset.brush.hardness) }, { "targetValue", Value::Float(asset.brush.targetValue) },
			{ "spacingFraction", Value::Float(asset.brush.spacingFraction) } }) }
	});
	return true;
}

namespace
{
bool DecodeImage(const std::string& bytes, std::uint32_t channel, bool canonical,
	VansPcgMaskAsset& asset, std::string& error)
{
	error.clear();
	const auto diagnostics = ValidatePcgMaskAsset(asset, false);
	if (!diagnostics.empty()) { error = diagnostics.front(); return false; }
	if (bytes.empty() || bytes.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
	{ error = "PCG Mask image bytes are empty or exceed decoder limits"; return false; }
	const auto* source = reinterpret_cast<const stbi_uc*>(bytes.data());
	const auto size = static_cast<int>(bytes.size());
	int width = 0, height = 0, channels = 0;
	if (!stbi_info_from_memory(source, size, &width, &height, &channels) || width <= 0 || height <= 0 ||
		static_cast<std::uint32_t>(width) != asset.mask.width || static_cast<std::uint32_t>(height) != asset.mask.height ||
		channel >= static_cast<std::uint32_t>(channels))
	{ error = "PCG Mask image dimensions/channel do not match the selected layer"; return false; }
	const bool highPrecision = stbi_is_16_bit_from_memory(source, size) != 0;
	if (canonical && (!highPrecision || channels != 1))
	{ error = "PCG Mask pixel asset must be a single-channel 16-bit image"; return false; }
	const std::size_t count = static_cast<std::size_t>(width) * height;
	std::vector<std::uint16_t> decoded(count);
	if (highPrecision)
	{
		auto* pixels = stbi_load_16_from_memory(source, size, &width, &height, &channels, 0);
		if (!pixels) { error = "Cannot decode PCG Mask image"; return false; }
		for (std::size_t i = 0; i < count; ++i) decoded[i] = pixels[i * channels + channel];
		stbi_image_free(pixels);
	}
	else
	{
		auto* pixels = stbi_load_from_memory(source, size, &width, &height, &channels, 0);
		if (!pixels) { error = "Cannot decode PCG Mask image"; return false; }
		// 数据通道直接扩展精度，不做 sRGB 转换，也不归一化其他草种的值。
		for (std::size_t i = 0; i < count; ++i) decoded[i] = static_cast<std::uint16_t>(pixels[i * channels + channel] * 257u);
		stbi_image_free(pixels);
	}
	asset.mask.pixels = std::move(decoded);
	return true;
}
}

bool VansPcgMaskAssetCodec::DecodePixels(const std::string& bytes, VansPcgMaskAsset& asset, std::string& error)
{
	return DecodeImage(bytes, 0, true, asset, error);
}

bool VansPcgMaskAssetCodec::EncodePixels(const VansPcgMaskAsset& asset, std::string& bytes, std::string& error)
{
	error.clear();
	const auto diagnostics = ValidatePcgMaskAsset(asset, true);
	if (!diagnostics.empty()) { error = diagnostics.front(); return false; }
	return VansDataPngEncoder::EncodeGray16(asset.mask.width, asset.mask.height, asset.mask.pixels, bytes, error);
}

bool VansPcgMaskAssetCodec::ImportPixels(const std::string& bytes, std::uint32_t channel, VansPcgMaskAsset& asset, std::string& error)
{
	return DecodeImage(bytes, channel, false, asset, error);
}
}
