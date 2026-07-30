#pragma once

#include "VansUILocalization.h"
#include "VansUIThemeTokens.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace VansRuntime
{
	class VansUIResourceRegistry
	{
	public:
		static VansUIResourceRegistry& Get();

		bool LoadThemeTokens(const std::string& configPath, std::string& error);
		bool LoadLocalization(const std::string& configPath, std::string& error);
		void Clear();

		const VansUIThemeTokensConfig* GetThemeTokens(const std::string& configPath) const;
		const VansUILocalizationConfig* GetLocalization(const std::string& locale) const;
		std::optional<VansUIVariant> GetToken(const std::string& group, const std::string& name) const;
		std::string Localize(const std::string& key, const std::string& locale, const std::string& fallback = {}) const;

	private:
		VansUIResourceRegistry() = default;

		std::unordered_map<std::string, VansUIThemeTokensConfig> m_ThemeTokensByPath;
		std::unordered_map<std::string, VansUILocalizationConfig> m_LocalizationByLocale;
	};
}
