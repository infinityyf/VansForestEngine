#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	struct PcgMaskConfig
	{
		std::string id;
		std::string path;
		std::string assetGuid;
		std::string textureValue;
		std::string channel;
		std::optional<glm::vec2> boundsMin;
		std::optional<glm::vec2> boundsMax;
		std::optional<glm::vec2> worldMin;
		std::optional<glm::vec2> worldMax;
		std::optional<float> threshold;
		std::optional<float> densityScale;
		std::optional<bool> invert;
	};

	struct PcgMaskReference
	{
		std::string ref;
		std::optional<PcgMaskConfig> inlineMask;

		bool HasValue() const { return !ref.empty() || inlineMask.has_value(); }
	};

	struct PcgPlacementMask
	{
		bool enabled = false;
		std::string name;
		std::string assetGuid;
		std::string sourcePath;
		std::string resolvedPath;
		std::string channel = "r";
		uint32_t width = 0;
		uint32_t height = 0;
		glm::vec2 worldMinXZ = glm::vec2(-100.0f);
		glm::vec2 worldMaxXZ = glm::vec2(100.0f);
		float threshold = 0.0f;
		float densityScale = 1.0f;
		bool invert = false;
		std::vector<float> values;
	};

	class VansPcgMaskSampler
	{
	public:
		static float Sample(const PcgPlacementMask& mask, const glm::vec2& worldXZ);
		static bool Accept(const PcgPlacementMask& mask, const glm::vec2& worldXZ, float randomValue);
	};

	class VansPcgMaskRegistry
	{
	public:
		void Load(const std::vector<PcgMaskConfig>& masks,
		          const std::string& projectRoot,
		          const glm::vec2& fallbackMinXZ,
		          const glm::vec2& fallbackMaxXZ);

		PcgPlacementMask ResolvePlacementMask(const PcgMaskReference& maskReference,
		                                      const std::string& projectRoot,
		                                      const std::string& fallbackName,
		                                      const glm::vec2& fallbackMinXZ,
		                                      const glm::vec2& fallbackMaxXZ) const;

		const PcgPlacementMask* Find(const std::string& name) const;
		const std::unordered_map<std::string, PcgPlacementMask>& GetMasks() const { return m_Masks; }
		bool Empty() const { return m_Masks.empty(); }

	private:
		void LoadOne(const PcgMaskConfig& maskConfig,
		             const std::string& projectRoot,
		             const glm::vec2& fallbackMinXZ,
		             const glm::vec2& fallbackMaxXZ);

		std::unordered_map<std::string, PcgPlacementMask> m_Masks;
	};

	class VansPcgSystem
	{
	public:
		void Configure(const std::vector<PcgMaskConfig>& masks,
		               const std::string& projectRoot,
		               const glm::vec2& fallbackMinXZ,
		               const glm::vec2& fallbackMaxXZ);

		PcgPlacementMask ResolvePlacementMask(const PcgMaskReference& maskReference,
		                                      const std::string& fallbackName,
		                                      const glm::vec2& fallbackMinXZ,
		                                      const glm::vec2& fallbackMaxXZ) const;

		bool AcceptMask(const PcgPlacementMask& mask, const glm::vec2& worldXZ, float randomValue) const;
		const VansPcgMaskRegistry& GetMaskRegistry() const { return m_MaskRegistry; }

	private:
		std::string m_ProjectRoot;
		VansPcgMaskRegistry m_MaskRegistry;
	};
}
