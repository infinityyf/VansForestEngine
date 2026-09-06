#include "Public/VansUIResourceRegistry.h"

#include "Serialization/VansUILocalizationReader.h"
#include "Serialization/VansUIThemeTokensReader.h"
#include "VansUIAssetResolver.h"

#include <utility>
#include <vector>

namespace VansRuntime
{
	namespace
	{
		std::optional<VansUIVariant> FindTokenInGroup(
			const VansUIVariantMap& group,
			const std::string& name)
		{
			const auto it = group.find(name);
			return it == group.end() ? std::nullopt : std::optional<VansUIVariant>(it->second);
		}
	}

	VansUIResourceRegistry& VansUIResourceRegistry::Get()
	{
		static VansUIResourceRegistry registry;
		return registry;
	}

	bool VansUIResourceRegistry::LoadThemeTokens(const std::string& assetGuid, std::string& error)
	{
		if (assetGuid.empty())
		{
			error = "Theme token asset GUID is empty.";
			return false;
		}
		if (m_ThemeTokensByGuid.find(assetGuid) != m_ThemeTokensByGuid.end())
			return true;

		std::shared_ptr<const VansUIAssetDocument> document;
		if (!VansUIAssetResolver::ResolveDocument(
			assetGuid, Vans::VansAssetType::UIThemeTokens, document, error))
			return false;

		VansUIThemeTokensConfig config;
		config.sourceAssetGuid = assetGuid;
		std::vector<std::string> diagnostics;
		if (!VansUIThemeTokensReader::Read(document->root, config, diagnostics))
		{
			error = diagnostics.empty() ? "Theme token parsing failed." : diagnostics.front();
			return false;
		}

		m_ThemeTokensByGuid.emplace(assetGuid, std::move(config));
		return true;
	}

	bool VansUIResourceRegistry::LoadLocalization(const std::string& assetGuid, std::string& error)
	{
		if (assetGuid.empty())
		{
			error = "Localization asset GUID is empty.";
			return false;
		}

		std::shared_ptr<const VansUIAssetDocument> document;
		if (!VansUIAssetResolver::ResolveDocument(
			assetGuid, Vans::VansAssetType::UILocalization, document, error))
			return false;

		VansUILocalizationConfig config;
		config.sourceAssetGuid = assetGuid;
		std::vector<std::string> diagnostics;
		if (!VansUILocalizationReader::Read(document->root, config, diagnostics))
		{
			error = diagnostics.empty() ? "Localization parsing failed." : diagnostics.front();
			return false;
		}

		m_LocalizationByLocale[config.locale] = std::move(config);
		return true;
	}

	void VansUIResourceRegistry::Clear()
	{
		m_ThemeTokensByGuid.clear();
		m_LocalizationByLocale.clear();
	}

	const VansUIThemeTokensConfig* VansUIResourceRegistry::GetThemeTokens(const std::string& assetGuid) const
	{
		const auto it = m_ThemeTokensByGuid.find(assetGuid);
		return it != m_ThemeTokensByGuid.end() ? &it->second : nullptr;
	}

	const VansUILocalizationConfig* VansUIResourceRegistry::GetLocalization(const std::string& locale) const
	{
		const auto it = m_LocalizationByLocale.find(locale);
		return it != m_LocalizationByLocale.end() ? &it->second : nullptr;
	}

	std::optional<VansUIVariant> VansUIResourceRegistry::GetToken(
		const std::string& group,
		const std::string& name) const
	{
		for (const auto& [guid, tokens] : m_ThemeTokensByGuid)
		{
			(void)guid;
			if (group == "colors")
				if (auto value = FindTokenInGroup(tokens.colors, name)) return value;
			if (group == "font")
				if (auto value = FindTokenInGroup(tokens.font, name)) return value;
			if (group == "spacing")
				if (auto value = FindTokenInGroup(tokens.spacing, name)) return value;
			if (group == "motion")
				if (auto value = FindTokenInGroup(tokens.motion, name)) return value;
		}
		return std::nullopt;
	}

	std::string VansUIResourceRegistry::Localize(
		const std::string& key,
		const std::string& locale,
		const std::string& fallback) const
	{
		if (const VansUILocalizationConfig* localization = GetLocalization(locale))
		{
			const auto it = localization->strings.find(key);
			if (it != localization->strings.end()) return it->second;
		}
		if (const VansUILocalizationConfig* localization = GetLocalization("default"))
		{
			const auto it = localization->strings.find(key);
			if (it != localization->strings.end()) return it->second;
		}
		return fallback.empty() ? key : fallback;
	}
}
