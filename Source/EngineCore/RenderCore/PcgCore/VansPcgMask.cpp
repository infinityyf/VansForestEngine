#include "VansPcgMask.h"

#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../Util/VansLog.h"

#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <optional>

namespace VansGraphics
{
	namespace
	{
		std::string ResolveProjectPath(const std::string& projectRoot, const std::string& path)
		{
			if (path.empty())
				return {};
			std::filesystem::path fsPath(path);
			if (fsPath.is_absolute())
				return fsPath.string();
			return (std::filesystem::path(projectRoot) / fsPath).lexically_normal().string();
		}

		std::optional<Vans::VansAssetRecord> ResolveMaskTextureRecord(const std::string& guidText)
		{
			Vans::VansAssetGuid guid;
			if (!Vans::VansAssetGuid::TryParse(guidText, guid))
				return std::nullopt;

			std::optional<Vans::VansAssetRecord> record = Vans::VansProjectManager::Get().FindAssetRecord(guid);
			if (!record)
			{
				VANS_LOG_WARN("[PCG] Placement mask texture guid was not found in project asset index: " << guidText);
				return std::nullopt;
			}
			if (record->type != Vans::VansAssetType::Texture)
			{
				VANS_LOG_WARN("[PCG] Placement mask guid does not reference a texture asset: " << guidText);
				return std::nullopt;
			}
			if (record->state == Vans::VansAssetState::Missing)
			{
				VANS_LOG_WARN("[PCG] Placement mask texture asset is missing: " << guidText);
				return std::nullopt;
			}
			return record;
		}

		int ReadPcgMaskChannel(const std::string& channel)
		{
			std::string lower = channel;
			std::transform(lower.begin(), lower.end(), lower.begin(),
				[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
			if (lower == "g" || lower == "green") return 1;
			if (lower == "b" || lower == "blue") return 2;
			if (lower == "a" || lower == "alpha") return 3;
			if (lower == "luma" || lower == "luminance" || lower == "rgb") return -1;
			return 0;
		}

		PcgPlacementMask LoadPcgPlacementMask(const PcgMaskConfig& maskConfig,
		                                      const std::string& projectRoot,
		                                      const std::string& fallbackName,
		                                      const glm::vec2& fallbackMinXZ,
		                                      const glm::vec2& fallbackMaxXZ)
		{
			PcgPlacementMask mask;

			mask.name = !maskConfig.id.empty() ? maskConfig.id : fallbackName;
			mask.worldMinXZ = maskConfig.boundsMin.value_or(fallbackMinXZ);
			mask.worldMaxXZ = maskConfig.boundsMax.value_or(fallbackMaxXZ);
			mask.worldMinXZ = maskConfig.worldMin.value_or(mask.worldMinXZ);
			mask.worldMaxXZ = maskConfig.worldMax.value_or(mask.worldMaxXZ);
			mask.threshold = glm::clamp(maskConfig.threshold.value_or(0.0f), 0.0f, 1.0f);
			mask.densityScale = std::max(maskConfig.densityScale.value_or(1.0f), 0.0f);
			mask.invert = maskConfig.invert.value_or(false);
			mask.channel = maskConfig.channel.empty() ? std::string("r") : maskConfig.channel;

			std::string resolvedPath;
			std::string textureGuid = maskConfig.assetGuid;
			std::string legacyPath = maskConfig.path;
			if (textureGuid.empty() && !maskConfig.textureValue.empty())
			{
				Vans::VansAssetGuid parsed;
				if (Vans::VansAssetGuid::TryParse(maskConfig.textureValue, parsed))
					textureGuid = maskConfig.textureValue;
				else
					legacyPath = maskConfig.textureValue;
			}

			if (!textureGuid.empty())
			{
				mask.assetGuid = textureGuid;
				const std::optional<Vans::VansAssetRecord> record = ResolveMaskTextureRecord(textureGuid);
				if (!record)
					return mask;
				mask.sourcePath = record->sourcePath.string();
				resolvedPath = record->sourcePath.string();
			}
			else
			{
				mask.sourcePath = legacyPath;
				if (legacyPath.empty())
					return mask;
				resolvedPath = ResolveProjectPath(projectRoot, legacyPath);
				VANS_LOG_WARN("[PCG] Placement mask '" << mask.name
					<< "' uses legacy path reference. Prefer texture guid references.");
			}

			mask.resolvedPath = resolvedPath;
			int width = 0;
			int height = 0;
			int sourceChannels = 0;
			stbi_uc* pixels = stbi_load(resolvedPath.c_str(), &width, &height, &sourceChannels, 4);
			if (!pixels || width <= 0 || height <= 0)
			{
				VANS_LOG_WARN("[PCG] Failed to load placement mask '" << mask.sourcePath << "' for '" << mask.name << "'.");
				if (pixels)
					stbi_image_free(pixels);
				return mask;
			}

			const int channel = ReadPcgMaskChannel(mask.channel);
			mask.width = static_cast<uint32_t>(width);
			mask.height = static_cast<uint32_t>(height);
			mask.values.resize(static_cast<size_t>(width) * height);
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					const size_t src = (static_cast<size_t>(y) * width + x) * 4;
					float value = 0.0f;
					if (channel < 0)
					{
						value = (0.2126f * pixels[src + 0]
						       + 0.7152f * pixels[src + 1]
						       + 0.0722f * pixels[src + 2]) / 255.0f;
					}
					else
					{
						value = static_cast<float>(pixels[src + channel]) / 255.0f;
					}
					mask.values[static_cast<size_t>(y) * width + x] = glm::clamp(value, 0.0f, 1.0f);
				}
			}

			stbi_image_free(pixels);
			mask.enabled = true;
			VANS_LOG("[PCG] Loaded placement mask '" << mask.name << "' " << width << "x" << height
				<< " from " << mask.resolvedPath);
			return mask;
		}
	}

	float VansPcgMaskSampler::Sample(const PcgPlacementMask& mask, const glm::vec2& worldXZ)
	{
		if (!mask.enabled || mask.values.empty() || mask.width == 0 || mask.height == 0)
			return 1.0f;

		const glm::vec2 minXZ = glm::min(mask.worldMinXZ, mask.worldMaxXZ);
		const glm::vec2 maxXZ = glm::max(mask.worldMinXZ, mask.worldMaxXZ);
		const glm::vec2 size = maxXZ - minXZ;
		if (size.x <= 0.0001f || size.y <= 0.0001f)
			return 0.0f;

		const float u = (worldXZ.x - minXZ.x) / size.x;
		const float v = (worldXZ.y - minXZ.y) / size.y;
		if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
			return 0.0f;

		const float px = u * static_cast<float>(mask.width - 1);
		const float py = v * static_cast<float>(mask.height - 1);
		const uint32_t x0 = static_cast<uint32_t>(glm::clamp(floorf(px), 0.0f, static_cast<float>(mask.width - 1)));
		const uint32_t y0 = static_cast<uint32_t>(glm::clamp(floorf(py), 0.0f, static_cast<float>(mask.height - 1)));
		const uint32_t x1 = std::min(x0 + 1, mask.width - 1);
		const uint32_t y1 = std::min(y0 + 1, mask.height - 1);
		const float tx = px - static_cast<float>(x0);
		const float ty = py - static_cast<float>(y0);

		auto read = [&](uint32_t x, uint32_t y) -> float
		{
			const size_t idx = static_cast<size_t>(y) * mask.width + x;
			return idx < mask.values.size() ? mask.values[idx] : 0.0f;
		};

		const float a = glm::mix(read(x0, y0), read(x1, y0), tx);
		const float b = glm::mix(read(x0, y1), read(x1, y1), tx);
		float density = glm::mix(a, b, ty);
		if (mask.invert)
			density = 1.0f - density;
		density = glm::clamp(density * std::max(mask.densityScale, 0.0f), 0.0f, 1.0f);
		return density >= mask.threshold ? density : 0.0f;
	}

	bool VansPcgMaskSampler::Accept(const PcgPlacementMask& mask, const glm::vec2& worldXZ, float randomValue)
	{
		return randomValue <= Sample(mask, worldXZ);
	}

	void VansPcgMaskRegistry::Load(const std::vector<PcgMaskConfig>& masks,
	                               const std::string& projectRoot,
	                               const glm::vec2& fallbackMinXZ,
	                               const glm::vec2& fallbackMaxXZ)
	{
		m_Masks.clear();
		for (const PcgMaskConfig& maskConfig : masks)
			LoadOne(maskConfig, projectRoot, fallbackMinXZ, fallbackMaxXZ);
	}

	PcgPlacementMask VansPcgMaskRegistry::ResolvePlacementMask(
		const PcgMaskReference& maskReference,
		const std::string& projectRoot,
		const std::string& fallbackName,
		const glm::vec2& fallbackMinXZ,
		const glm::vec2& fallbackMaxXZ) const
	{
		if (!maskReference.HasValue())
			return {};

		if (maskReference.inlineMask)
			return LoadPcgPlacementMask(*maskReference.inlineMask, projectRoot, fallbackName, fallbackMinXZ, fallbackMaxXZ);

		const PcgPlacementMask* found = Find(maskReference.ref);
		if (found)
			return *found;
		VANS_LOG_WARN("[PCG] Placement mask reference '" << maskReference.ref << "' was not found.");
		return {};
	}

	const PcgPlacementMask* VansPcgMaskRegistry::Find(const std::string& name) const
	{
		const auto found = m_Masks.find(name);
		return found != m_Masks.end() ? &found->second : nullptr;
	}

	void VansPcgMaskRegistry::LoadOne(const PcgMaskConfig& maskConfig,
	                                  const std::string& projectRoot,
	                                  const glm::vec2& fallbackMinXZ,
	                                  const glm::vec2& fallbackMaxXZ)
	{
		PcgPlacementMask mask = LoadPcgPlacementMask(maskConfig, projectRoot, maskConfig.id, fallbackMinXZ, fallbackMaxXZ);
		if (mask.enabled && !mask.name.empty())
			m_Masks[mask.name] = std::move(mask);
	}

	void VansPcgSystem::Configure(const std::vector<PcgMaskConfig>& masks,
	                              const std::string& projectRoot,
	                              const glm::vec2& fallbackMinXZ,
	                              const glm::vec2& fallbackMaxXZ)
	{
		m_ProjectRoot = projectRoot;
		m_MaskRegistry.Load(masks, projectRoot, fallbackMinXZ, fallbackMaxXZ);
	}

	PcgPlacementMask VansPcgSystem::ResolvePlacementMask(const PcgMaskReference& maskReference,
	                                                     const std::string& fallbackName,
	                                                     const glm::vec2& fallbackMinXZ,
	                                                     const glm::vec2& fallbackMaxXZ) const
	{
		return m_MaskRegistry.ResolvePlacementMask(maskReference, m_ProjectRoot, fallbackName,
			fallbackMinXZ, fallbackMaxXZ);
	}

	bool VansPcgSystem::AcceptMask(const PcgPlacementMask& mask,
	                               const glm::vec2& worldXZ,
	                               float randomValue) const
	{
		return VansPcgMaskSampler::Accept(mask, worldXZ, randomValue);
	}
}

