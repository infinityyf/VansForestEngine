#include "VansLocalFogFieldPreviewService.h"

#include "../../SceneCore/VansSceneLocalVolumetricFogComponentConfig.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <tuple>

namespace Vans::EditorAPI
{
	namespace
	{
		constexpr std::uint32_t MinimumPreviewDimension = 4;
		constexpr std::uint32_t MaximumPreviewDimension = 32;
		constexpr std::size_t MaximumCachedPreviews = 64;

		float SampleBilinearChannel(
			const std::uint8_t* rgbaPixels,
			std::uint32_t width,
			std::uint32_t height,
			int channel,
			float u,
			float v)
		{
			const float sourceX = std::clamp(
				u * static_cast<float>(width) - 0.5f,
				0.0f,
				static_cast<float>(width - 1));
			const float sourceY = std::clamp(
				v * static_cast<float>(height) - 0.5f,
				0.0f,
				static_cast<float>(height - 1));
			const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(sourceX));
			const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(sourceY));
			const std::uint32_t x1 = (std::min)(x0 + 1u, width - 1u);
			const std::uint32_t y1 = (std::min)(y0 + 1u, height - 1u);
			const float tx = sourceX - static_cast<float>(x0);
			const float ty = sourceY - static_cast<float>(y0);
			const auto read = [&](std::uint32_t x, std::uint32_t y)
			{
				const std::size_t index =
					(static_cast<std::size_t>(y) * width + x) * 4u +
					static_cast<std::size_t>(channel);
				return static_cast<float>(rgbaPixels[index]) / 255.0f;
			};
			const float top = read(x0, y0) + (read(x1, y0) - read(x0, y0)) * tx;
			const float bottom = read(x0, y1) + (read(x1, y1) - read(x0, y1)) * tx;
			return top + (bottom - top) * ty;
		}
	}

	LocalFogFieldPreviewSnapshot BuildLocalFogFieldPreviewFromRgba8(
		const std::uint8_t* rgbaPixels,
		std::size_t rgbaByteCount,
		std::uint32_t sourceWidth,
		std::uint32_t sourceHeight,
		const LocalFogFieldPreviewRequest& request)
	{
		LocalFogFieldPreviewSnapshot snapshot;
		if (!rgbaPixels || sourceWidth == 0 || sourceHeight == 0)
		{
			snapshot.message = "Local Fog preview source is empty";
			return snapshot;
		}
		const std::size_t pixelCount =
			static_cast<std::size_t>(sourceWidth) * sourceHeight;
		if (pixelCount > (std::numeric_limits<std::size_t>::max)() / 4u ||
			rgbaByteCount < pixelCount * 4u)
		{
			snapshot.message = "Local Fog preview source byte count is invalid";
			return snapshot;
		}

		VansLocalFogTextureChannel channel0{};
		VansLocalFogTextureChannel channel1{};
		const bool isFlow = request.kind == LocalFogFieldPreviewKind::FlowVector;
		const bool validChannels = isFlow
			? TryParseLocalFogVector2TextureChannels(
				request.channels, channel0, channel1)
			: TryParseLocalFogScalarTextureChannels(request.channels, channel0);
		if (!validChannels)
		{
			snapshot.message = isFlow
				? "Flow preview requires two distinct RGBA channels"
				: "Scalar preview requires one RGBA channel";
			return snapshot;
		}

		snapshot.sourceWidth = sourceWidth;
		snapshot.sourceHeight = sourceHeight;
		snapshot.sampleColumns = std::clamp(
			request.sampleColumns, MinimumPreviewDimension, MaximumPreviewDimension);
		snapshot.sampleRows = std::clamp(
			request.sampleRows, MinimumPreviewDimension, MaximumPreviewDimension);
		const std::size_t sampleCount =
			static_cast<std::size_t>(snapshot.sampleColumns) * snapshot.sampleRows;
		if (isFlow)
			snapshot.flowSamples.reserve(sampleCount);
		else
			snapshot.scalarSamples.reserve(sampleCount);

		const int channelIndex0 = LocalFogTextureChannelIndex(channel0);
		const int channelIndex1 = LocalFogTextureChannelIndex(channel1);
		for (std::uint32_t y = 0; y < snapshot.sampleRows; ++y)
		{
			const float v = (static_cast<float>(y) + 0.5f) /
				static_cast<float>(snapshot.sampleRows);
			for (std::uint32_t x = 0; x < snapshot.sampleColumns; ++x)
			{
				const float u = (static_cast<float>(x) + 0.5f) /
					static_cast<float>(snapshot.sampleColumns);
				const float value0 = SampleBilinearChannel(
					rgbaPixels, sourceWidth, sourceHeight, channelIndex0, u, v);
				if (!isFlow)
				{
					snapshot.scalarSamples.push_back(value0);
					continue;
				}
				const float value1 = SampleBilinearChannel(
					rgbaPixels, sourceWidth, sourceHeight, channelIndex1, u, v);
				const std::array<float, 2> flow =
					DecodeAndClampLocalFogFlowVector(value0, value1);
				snapshot.flowSamples.push_back({ flow[0], flow[1] });
			}
		}

		snapshot.available = true;
		return snapshot;
	}

	bool VansLocalFogFieldPreviewService::CacheKey::operator<(
		const CacheKey& other) const
	{
		return std::tie(assetGuid, channels, sourcePath, sourceHash, metaHash,
			generation, kind, sampleColumns, sampleRows) <
			std::tie(other.assetGuid, other.channels, other.sourcePath,
				other.sourceHash, other.metaHash, other.generation, other.kind,
				other.sampleColumns, other.sampleRows);
	}

	LocalFogFieldPreviewSnapshot VansLocalFogFieldPreviewService::Build(
		const LocalFogFieldPreviewRequest& request,
		const std::string& sourcePath,
		std::uint64_t sourceHash,
		std::uint64_t metaHash,
		std::uint64_t generation) const
	{
		CacheKey key;
		key.assetGuid = request.assetGuid;
		key.channels = request.channels;
		key.sourcePath = sourcePath;
		key.sourceHash = sourceHash;
		key.metaHash = metaHash;
		key.generation = generation;
		key.kind = request.kind;
		key.sampleColumns = std::clamp(
			request.sampleColumns, MinimumPreviewDimension, MaximumPreviewDimension);
		key.sampleRows = std::clamp(
			request.sampleRows, MinimumPreviewDimension, MaximumPreviewDimension);
		{
			const std::scoped_lock lock(m_CacheMutex);
			const auto found = m_Cache.find(key);
			if (found != m_Cache.end())
				return found->second;
		}

		LocalFogFieldPreviewSnapshot snapshot;
		int width = 0;
		int height = 0;
		int sourceChannels = 0;
		std::unique_ptr<stbi_uc, void(*)(void*)> pixels(
			stbi_load(sourcePath.c_str(), &width, &height, &sourceChannels, 4),
			stbi_image_free);
		if (!pixels || width <= 0 || height <= 0 || sourceChannels <= 0)
		{
			snapshot.message = "Could not decode the Local Fog field source texture";
		}
		else
		{
			const std::size_t byteCount =
				static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
			LocalFogFieldPreviewRequest normalizedRequest = request;
			normalizedRequest.sampleColumns = key.sampleColumns;
			normalizedRequest.sampleRows = key.sampleRows;
			snapshot = BuildLocalFogFieldPreviewFromRgba8(
				pixels.get(), byteCount,
				static_cast<std::uint32_t>(width),
				static_cast<std::uint32_t>(height), normalizedRequest);
		}

		{
			const std::scoped_lock lock(m_CacheMutex);
			for (auto item = m_Cache.begin(); item != m_Cache.end();)
			{
				const bool sameAsset = item->first.assetGuid == key.assetGuid;
				const bool staleFingerprint = item->first.sourceHash != key.sourceHash ||
					item->first.metaHash != key.metaHash ||
					item->first.generation != key.generation ||
					item->first.sourcePath != key.sourcePath;
				item = sameAsset && staleFingerprint ? m_Cache.erase(item) : ++item;
			}
			if (m_Cache.size() >= MaximumCachedPreviews)
				m_Cache.erase(m_Cache.begin());
			m_Cache[key] = snapshot;
		}
		return snapshot;
	}

	void VansLocalFogFieldPreviewService::Clear()
	{
		const std::scoped_lock lock(m_CacheMutex);
		m_Cache.clear();
	}
}
