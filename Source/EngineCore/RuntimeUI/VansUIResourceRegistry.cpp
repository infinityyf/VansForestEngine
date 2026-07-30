#include "Public/VansUIResourceRegistry.h"

#include "Serialization/VansUIDocumentLoader.h"
#include "Serialization/VansUIDocumentMigrator.h"
#include "Serialization/VansUILocalizationReader.h"
#include "Serialization/VansUIThemeTokensReader.h"
#include "../Configration/VansConfigration.h"
#include "../ProjectSystem/VansProjectManager.h"

#include <filesystem>
#include <algorithm>
#include <utility>

namespace VansRuntime
{
	namespace
	{
		std::string ResolveUIProjectRelativePath(const std::string& path)
		{
			std::string normalized = path;
			std::replace(normalized.begin(), normalized.end(), '\\', '/');
			if (normalized.rfind("Assets/", 0) == 0)
				return normalized;

			auto& projectManager = Vans::VansProjectManager::Get();
			const auto& dirs = projectManager.GetConfig().assetDirectories;
			const auto uiDir = dirs.find("ui");
			if (uiDir == dirs.end())
				return normalized;

			std::string root = uiDir->second;
			std::replace(root.begin(), root.end(), '\\', '/');
			while (!root.empty() && root.back() == '/')
				root.pop_back();

			if (normalized.rfind("UI/", 0) == 0)
				return root + "/" + normalized.substr(3);
			return root + "/" + normalized;
		}

		std::filesystem::path ResolveUIResourcePath(const std::string& configPath)
		{
			std::filesystem::path path(configPath);
			if (path.is_absolute())
				return path;

			auto& projectManager = Vans::VansProjectManager::Get();
			if (projectManager.IsProjectLoaded())
				return std::filesystem::path(projectManager.ResolveAssetPath(ResolveUIProjectRelativePath(configPath)));

			if (auto* configuration = VansConfigration::GetInstance())
				return std::filesystem::path(configuration->GetProjectRootPath()) / configPath;

			return path;
		}

		std::optional<VansUIVariant> FindTokenInGroup(
			const VansUIVariantMap& group,
			const std::string& name)
		{
			const auto it = group.find(name);
			if (it == group.end())
				return std::nullopt;
			return it->second;
		}
	}

	VansUIResourceRegistry& VansUIResourceRegistry::Get()
	{
		static VansUIResourceRegistry registry;
		return registry;
	}

	bool VansUIResourceRegistry::LoadThemeTokens(const std::string& configPath, std::string& error)
	{
		if (configPath.empty())
		{
			error = "Theme token path is empty.";
			return false;
		}
		if (m_ThemeTokensByPath.find(configPath) != m_ThemeTokensByPath.end())
			return true;

		VansUIAssetDocument document;
		if (!VansUIDocumentLoader::Load(ResolveUIResourcePath(configPath), document, error))
			return false;

		std::vector<std::string> diagnostics;
		if (!VansUIDocumentMigrator::MigrateToCurrent(document, VansUIDocumentKind::ThemeTokens, diagnostics))
		{
			error = diagnostics.empty() ? "Theme token migration failed." : diagnostics.front();
			return false;
		}

		VansUIThemeTokensConfig config;
		config.sourceConfigPath = configPath;
		diagnostics.clear();
		if (!VansUIThemeTokensReader::Read(document.root, config, diagnostics))
		{
			error = diagnostics.empty() ? "Theme token parsing failed." : diagnostics.front();
			return false;
		}

		m_ThemeTokensByPath.emplace(configPath, std::move(config));
		return true;
	}

	bool VansUIResourceRegistry::LoadLocalization(const std::string& configPath, std::string& error)
	{
		if (configPath.empty())
		{
			error = "Localization path is empty.";
			return false;
		}

		VansUIAssetDocument document;
		if (!VansUIDocumentLoader::Load(ResolveUIResourcePath(configPath), document, error))
			return false;

		std::vector<std::string> diagnostics;
		if (!VansUIDocumentMigrator::MigrateToCurrent(document, VansUIDocumentKind::Localization, diagnostics))
		{
			error = diagnostics.empty() ? "Localization migration failed." : diagnostics.front();
			return false;
		}

		VansUILocalizationConfig config;
		config.sourceConfigPath = configPath;
		diagnostics.clear();
		if (!VansUILocalizationReader::Read(document.root, config, diagnostics))
		{
			error = diagnostics.empty() ? "Localization parsing failed." : diagnostics.front();
			return false;
		}

		m_LocalizationByLocale[config.locale] = std::move(config);
		return true;
	}

	void VansUIResourceRegistry::Clear()
	{
		m_ThemeTokensByPath.clear();
		m_LocalizationByLocale.clear();
	}

	const VansUIThemeTokensConfig* VansUIResourceRegistry::GetThemeTokens(const std::string& configPath) const
	{
		const auto it = m_ThemeTokensByPath.find(configPath);
		return it != m_ThemeTokensByPath.end() ? &it->second : nullptr;
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
		for (const auto& [path, tokens] : m_ThemeTokensByPath)
		{
			(void)path;
			if (group == "colors")
				if (auto value = FindTokenInGroup(tokens.colors, name))
					return value;
			if (group == "font")
				if (auto value = FindTokenInGroup(tokens.font, name))
					return value;
			if (group == "spacing")
				if (auto value = FindTokenInGroup(tokens.spacing, name))
					return value;
			if (group == "motion")
				if (auto value = FindTokenInGroup(tokens.motion, name))
					return value;
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
			if (it != localization->strings.end())
				return it->second;
		}
		if (const VansUILocalizationConfig* localization = GetLocalization("default"))
		{
			const auto it = localization->strings.find(key);
			if (it != localization->strings.end())
				return it->second;
		}
		return fallback.empty() ? key : fallback;
	}
}
