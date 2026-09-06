#include "Public/VansUIComponentRegistry.h"

#include "Public/VansUIComponent.h"
#include "Public/VansUISystem.h"
#include "Serialization/VansUIComponentConfigReader.h"
#include "Serialization/VansUIDocumentValidator.h"
#include "VansUIAssetResolver.h"
#include "../Util/VansLog.h"

#include <utility>
#include <vector>

namespace VansRuntime
{
    VansUIComponentRegistry& VansUIComponentRegistry::Get()
    {
        static VansUIComponentRegistry registry;
        return registry;
    }

    std::shared_ptr<VansUIComponentInstance> VansUIComponentRegistry::LoadComponent(
        const std::string& configAssetGuid)
    {
        std::shared_ptr<const VansUIAssetDocument> document;
        std::string error;
        if (!VansUIAssetResolver::ResolveDocument(
            configAssetGuid, Vans::VansAssetType::UIComponent, document, error))
        {
            VANS_LOG_ERROR("[RuntimeUI] Failed to resolve UI component asset '" << configAssetGuid << "': " << error);
            return nullptr;
        }

        VansUIComponentConfig config;
        config.sourceAssetGuid = configAssetGuid;
        std::vector<std::string> diagnostics;
        if (!VansUIComponentConfigReader::Read(document->root, config, diagnostics) ||
            !VansUIDocumentValidator::ValidateComponentConfig(config, diagnostics))
        {
            for (const std::string& diagnostic : diagnostics)
                VANS_LOG_ERROR("[RuntimeUI] " << configAssetGuid << ": " << diagnostic);
            return nullptr;
        }

        auto uiDocument = VansUISystem::Get().LoadDocument(config.xamlAssetGuid);
        if (!uiDocument)
        {
            VANS_LOG_ERROR("[RuntimeUI] Failed to resolve UI component XAML '" << config.xamlAssetGuid << "'");
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
