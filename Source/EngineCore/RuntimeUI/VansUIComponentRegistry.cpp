#include "Public/VansUIComponentRegistry.h"

#include "Public/VansUIComponent.h"
#include "Public/VansUISystem.h"
#include "Serialization/VansUIComponentConfigReader.h"
#include "Serialization/VansUIDocumentLoader.h"
#include "Serialization/VansUIDocumentMigrator.h"
#include "Serialization/VansUIDocumentValidator.h"
#include "../Configration/VansConfigration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../Util/VansLog.h"

#include <filesystem>
#include <utility>
#include <vector>

namespace VansRuntime
{
    namespace
    {
        std::filesystem::path ResolveUIConfigPath(const std::string& configPath)
        {
            std::filesystem::path path(configPath);
            if (path.is_absolute())
                return path;

            auto& projectManager = Vans::VansProjectManager::Get();
            if (projectManager.IsProjectLoaded())
                return std::filesystem::path(projectManager.ResolveAssetPath(configPath));

            if (auto* configuration = VansConfigration::GetInstance())
                return std::filesystem::path(configuration->GetProjectRootPath()) / configPath;

            return path;
        }
    }

    VansUIComponentRegistry& VansUIComponentRegistry::Get()
    {
        static VansUIComponentRegistry registry;
        return registry;
    }

    std::shared_ptr<VansUIComponentInstance> VansUIComponentRegistry::LoadComponent(
        const std::string& configPath)
    {
        VansUIAssetDocument document;
        std::string error;
        if (!VansUIDocumentLoader::Load(ResolveUIConfigPath(configPath), document, error))
        {
            VANS_LOG_ERROR("[RuntimeUI] Failed to load UI component config '" << configPath << "': " << error);
            return nullptr;
        }

        VansUIComponentConfig config;
        config.sourceConfigPath = configPath;
        std::vector<std::string> diagnostics;
        if (!VansUIDocumentMigrator::MigrateToCurrent(document, VansUIDocumentKind::Component, diagnostics))
        {
            for (const std::string& diagnostic : diagnostics)
                VANS_LOG_ERROR("[RuntimeUI] " << configPath << ": " << diagnostic);
            return nullptr;
        }
        for (const std::string& diagnostic : diagnostics)
            VANS_LOG_WARN("[RuntimeUI] " << configPath << ": " << diagnostic);
        diagnostics.clear();
        if (!VansUIComponentConfigReader::Read(document.root, config, diagnostics) ||
            !VansUIDocumentValidator::ValidateComponentConfig(config, diagnostics))
        {
            for (const std::string& diagnostic : diagnostics)
                VANS_LOG_ERROR("[RuntimeUI] " << configPath << ": " << diagnostic);
            return nullptr;
        }

        auto uiDocument = VansUISystem::Get().LoadDocument(config.xamlPath);
        if (!uiDocument)
        {
            VANS_LOG_ERROR("[RuntimeUI] Failed to load UI component XAML '" << config.xamlPath << "'");
            return nullptr;
        }

        auto component = std::make_shared<VansUIComponentInstance>(
            m_NextHandle++,
            std::move(config),
            std::move(uiDocument));
        m_Components[component->GetHandleId()] = component;
        return component;
    }

    std::shared_ptr<VansUIComponentInstance> VansUIComponentRegistry::GetComponent(
        VansUIHandleId handle) const
    {
        const auto it = m_Components.find(handle);
        return it != m_Components.end() ? it->second : nullptr;
    }

    void VansUIComponentRegistry::CloseComponent(VansUIHandleId handle)
    {
        const auto it = m_Components.find(handle);
        if (it == m_Components.end())
            return;

        if (it->second)
        {
            if (auto document = it->second->GetDocument())
                VansUISystem::Get().UnloadDocument(document);
            it->second->Close();
        }
        m_Components.erase(it);
    }

    void VansUIComponentRegistry::CloseAll()
    {
        std::vector<VansUIHandleId> handles;
        handles.reserve(m_Components.size());
        for (const auto& [handle, component] : m_Components)
        {
            (void)component;
            handles.push_back(handle);
        }
        for (VansUIHandleId handle : handles)
            CloseComponent(handle);
    }
}
