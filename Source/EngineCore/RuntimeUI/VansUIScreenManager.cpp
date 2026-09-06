#include "Public/VansUIScreenManager.h"
#include "Public/VansUISystem.h"
#include "Public/VansUIDocument.h"
#include "Public/VansUIActionBus.h"
#include "Public/VansUIElementHandle.h"
#include "Public/VansUIScreen.h"
#include "Public/VansUIResourceRegistry.h"
#include "Public/VansUIViewModel.h"
#include "Serialization/VansUIDocumentValidator.h"
#include "Serialization/VansUIScreenConfigReader.h"
#include "VansUIAssetResolver.h"
#include "../Util/VansLog.h"

#include <cassert>
#include <algorithm>
#include <utility>

namespace VansRuntime
{
VansUIScreenManager::VansUIScreenManager(VansUISystem& uiSystem)
    : m_UISystem(uiSystem)
{}

VansUIScreenManager::~VansUIScreenManager()
{
    CloseAll();
}

std::shared_ptr<VansUIScreen> VansUIScreenManager::CreateScreenFromConfig(
    const std::string& configAssetGuid,
    std::shared_ptr<VansUIViewModel> vm)
{
    std::shared_ptr<const VansUIAssetDocument> document;
    std::string error;
    if (!VansUIAssetResolver::ResolveDocument(
        configAssetGuid, Vans::VansAssetType::UIScreen, document, error))
    {
        VANS_LOG_ERROR("[RuntimeUI] Failed to resolve UI screen asset '" << configAssetGuid << "': " << error);
        return nullptr;
    }

    VansUIScreenConfig config;
    config.sourceAssetGuid = configAssetGuid;
    std::vector<std::string> diagnostics;
    if (!VansUIScreenConfigReader::Read(document->root, config, diagnostics) ||
        !VansUIDocumentValidator::ValidateScreenConfig(config, diagnostics))
    {
        for (const std::string& diagnostic : diagnostics)
            VANS_LOG_ERROR("[RuntimeUI] " << configAssetGuid << ": " << diagnostic);
        return nullptr;
    }

    std::string resourceError;
    for (const std::string& tokenAssetGuid : config.tokenAssetGuids)
    {
        resourceError.clear();
        if (!VansUIResourceRegistry::Get().LoadThemeTokens(tokenAssetGuid, resourceError))
        {
            VANS_LOG_ERROR("[RuntimeUI] Failed to resolve UI tokens '" << tokenAssetGuid
                << "' for screen '" << config.name << "': " << resourceError);
            return nullptr;
        }
    }
    for (const std::string& localizationAssetGuid : config.localizationAssetGuids)
    {
        resourceError.clear();
        if (!VansUIResourceRegistry::Get().LoadLocalization(localizationAssetGuid, resourceError))
        {
            VANS_LOG_ERROR("[RuntimeUI] Failed to resolve UI localization '" << localizationAssetGuid
                << "' for screen '" << config.name << "': " << resourceError);
            return nullptr;
        }
    }

    auto uiDocument = m_UISystem.LoadDocument(config.xamlAssetGuid);
    if (!uiDocument)
    {
        VANS_LOG_ERROR("[RuntimeUI] Failed to resolve UI screen XAML '" << config.xamlAssetGuid
            << "' from asset '" << configAssetGuid << "'");
        return nullptr;
    }

    uiDocument->Show();

    auto screen = std::make_shared<VansUIScreen>(
        m_NextHandle++,
        std::move(config),
        std::move(uiDocument),
        std::move(vm));
    BindConfiguredEvents(screen);
    RegisterScreen(screen);
    return screen;
}

void VansUIScreenManager::RegisterScreen(const std::shared_ptr<VansUIScreen>& screen)
{
    if (!screen)
        return;

    const auto existing = std::find_if(m_Screens.begin(), m_Screens.end(),
        [handle = screen->GetHandleId()](const std::shared_ptr<VansUIScreen>& item)
        {
            return item && item->GetHandleId() == handle;
        });
    if (existing == m_Screens.end())
        m_Screens.push_back(screen);
}

std::shared_ptr<VansUIScreen> VansUIScreenManager::TakePreloadedScreen(
    const std::string& configPath,
    std::shared_ptr<VansUIViewModel> vm)
{
    const auto it = m_PreloadedScreens.find(configPath);
    if (it == m_PreloadedScreens.end() || !it->second)
        return nullptr;

    auto screen = it->second;
    m_PreloadedScreens.erase(it);
    if (vm)
        screen->SetViewModel(std::move(vm));
    screen->Show();
    RegisterScreen(screen);
    return screen;
}

void VansUIScreenManager::BindConfiguredEvents(const std::shared_ptr<VansUIScreen>& screen)
{
    if (!screen)
        return;

    const VansUIHandleId screenId = screen->GetHandleId();
    for (const VansUIScreenEventBindingConfig& binding : screen->GetConfig().events)
    {
        if (binding.eventName != "Click")
            continue;

        VansUIElementHandle element = screen->FindElement(binding.source);
        if (!element.IsValid())
        {
            VANS_LOG_WARN("[RuntimeUI] Event source not found: " << binding.source
                << " in " << screen->GetConfig().sourceAssetGuid);
            continue;
        }

        VANS_LOG("[RuntimeUI] Bound " << binding.eventName << " event for "
            << binding.source << " -> " << binding.action);
        element.BindClick([screenId,
            source = binding.source,
            actionName = binding.action,
            params = binding.params]()
        {
            VANS_LOG("[RuntimeUI] Click event dispatched: " << source
                << " -> " << actionName);
            VansUIActionBus::Get().Dispatch(VansUIAction{
                actionName,
                params,
                screenId,
                source
            });
        });
    }
}

void VansUIScreenManager::UnloadScreen(const std::shared_ptr<VansUIScreen>& screen)
{
    if (!screen)
        return;

    screen->Close();
    if (auto document = screen->GetDocument())
    {
        m_UISystem.UnloadDocument(document);
        if (document == m_HUDDocument)
            m_HUDDocument.reset();
        if (document == m_ModalDocument)
            m_ModalDocument.reset();
        for (auto it = m_Overlays.begin(); it != m_Overlays.end();)
        {
            if (it->second == document)
                it = m_Overlays.erase(it);
            else
                ++it;
        }
    }

    for (auto it = m_PreloadedScreens.begin(); it != m_PreloadedScreens.end();)
    {
        if (it->second == screen)
            it = m_PreloadedScreens.erase(it);
        else
            ++it;
    }

    m_ConfigScreenStack.erase(
        std::remove_if(m_ConfigScreenStack.begin(), m_ConfigScreenStack.end(),
            [handle = screen->GetHandleId()](const std::shared_ptr<VansUIScreen>& existing)
            {
                return !existing || existing->GetHandleId() == handle;
            }),
        m_ConfigScreenStack.end());

    m_Screens.erase(
        std::remove_if(m_Screens.begin(), m_Screens.end(),
            [handle = screen->GetHandleId()](const std::shared_ptr<VansUIScreen>& existing)
            {
                return !existing || existing->GetHandleId() == handle;
            }),
        m_Screens.end());
}

std::shared_ptr<VansUIScreen> VansUIScreenManager::LoadScreen(
    const std::string& configPath,
    std::shared_ptr<VansUIViewModel> vm)
{
    if (auto preloaded = TakePreloadedScreen(configPath, std::move(vm)))
        return preloaded;
    return CreateScreenFromConfig(configPath, std::move(vm));
}

std::shared_ptr<VansUIScreen> VansUIScreenManager::PushScreenConfig(
    const std::string& configPath,
    std::shared_ptr<VansUIViewModel> vm)
{
    if (!m_ConfigScreenStack.empty() && m_ConfigScreenStack.back())
        m_ConfigScreenStack.back()->Hide();

    if (auto preloaded = TakePreloadedScreen(configPath, std::move(vm)))
    {
        m_ConfigScreenStack.push_back(preloaded);
        return preloaded;
    }

    auto screen = CreateScreenFromConfig(configPath, std::move(vm));
    if (screen)
        m_ConfigScreenStack.push_back(screen);
    return screen;
}

std::shared_ptr<VansUIScreen> VansUIScreenManager::ReplaceScreenConfig(
    const std::string& configPath,
    std::shared_ptr<VansUIViewModel> vm)
{
    if (!m_ConfigScreenStack.empty())
    {
        auto top = m_ConfigScreenStack.back();
        m_ConfigScreenStack.pop_back();
        UnloadScreen(top);
    }

    auto screen = LoadScreen(configPath, std::move(vm));
    if (screen)
        m_ConfigScreenStack.push_back(screen);
    return screen;
}

bool VansUIScreenManager::PreloadScreen(const std::string& configPath)
{
    if (configPath.empty())
        return false;
    if (m_PreloadedScreens.find(configPath) != m_PreloadedScreens.end())
        return true;

    auto screen = CreateScreenFromConfig(configPath, nullptr);
    if (!screen)
        return false;

    screen->Hide();
    m_PreloadedScreens[configPath] = screen;
    return true;
}

void VansUIScreenManager::ReleaseScreen(const std::string& configPath)
{
    const auto it = m_PreloadedScreens.find(configPath);
    if (it == m_PreloadedScreens.end())
        return;

    auto screen = it->second;
    m_PreloadedScreens.erase(it);
    UnloadScreen(screen);
}

std::shared_ptr<VansUIScreen> VansUIScreenManager::ReloadScreen(
    const std::string& configPath,
    std::shared_ptr<VansUIViewModel> vm)
{
    ReleaseScreen(configPath);

    std::vector<std::shared_ptr<VansUIScreen>> matching;
    for (const auto& screen : m_Screens)
    {
        if (screen && screen->GetConfig().sourceAssetGuid == configPath)
            matching.push_back(screen);
    }
    for (const auto& screen : matching)
        UnloadScreen(screen);

    return CreateScreenFromConfig(configPath, std::move(vm));
}

std::shared_ptr<VansUIScreen> VansUIScreenManager::SetHUDConfig(
    const std::string& configPath,
    std::shared_ptr<VansUIViewModel> vm)
{
    if (m_HUDDocument)
    {
        const auto existing = std::find_if(m_Screens.begin(), m_Screens.end(),
            [document = m_HUDDocument](const std::shared_ptr<VansUIScreen>& screen)
            {
                return screen && screen->GetDocument() == document;
            });
        if (existing != m_Screens.end())
        {
            UnloadScreen(*existing);
        }
        else
        {
            m_UISystem.UnloadDocument(m_HUDDocument);
            m_HUDDocument.reset();
        }
    }

    auto screen = CreateScreenFromConfig(configPath, std::move(vm));
    if (screen)
        m_HUDDocument = screen->GetDocument();
    return screen;
}

std::shared_ptr<VansUIScreen> VansUIScreenManager::ShowModalConfig(
    const std::string& configPath,
    std::shared_ptr<VansUIViewModel> vm)
{
    HideModal();
    auto screen = CreateScreenFromConfig(configPath, std::move(vm));
    if (screen)
        m_ModalDocument = screen->GetDocument();
    return screen;
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen stack
// ─────────────────────────────────────────────────────────────────────────────

void VansUIScreenManager::PushScreen(const std::string& xamlAssetGuid,
                                      std::shared_ptr<VansUIViewModel> vm)
{
    // Hide the current top screen before pushing a new one
    if (!m_ScreenStack.empty() && m_ScreenStack.top())
    {
        m_ScreenStack.top()->Hide();
    }

    auto doc = m_UISystem.LoadDocument(xamlAssetGuid);
    if (!doc) return;

    if (vm)
    {
        doc->SetDataContext(vm.get());
    }
    doc->Show();
    m_ScreenStack.push(std::move(doc));
}

void VansUIScreenManager::PopScreen()
{
    if (!m_ConfigScreenStack.empty())
    {
        auto top = m_ConfigScreenStack.back();
        m_ConfigScreenStack.pop_back();
        UnloadScreen(top);
        if (!m_ConfigScreenStack.empty() && m_ConfigScreenStack.back())
            m_ConfigScreenStack.back()->Show();
        return;
    }

    if (m_ScreenStack.empty()) return;

    auto top = m_ScreenStack.top();
    m_ScreenStack.pop();

    if (top)
    {
        m_UISystem.UnloadDocument(top);
    }

    // Reveal the new top screen
    if (!m_ScreenStack.empty() && m_ScreenStack.top())
    {
        m_ScreenStack.top()->Show();
    }
}

void VansUIScreenManager::ReplaceScreen(const std::string& xamlAssetGuid,
                                         std::shared_ptr<VansUIViewModel> vm)
{
    // Unload current top without revealing the one beneath it
    if (!m_ScreenStack.empty())
    {
        auto top = m_ScreenStack.top();
        m_ScreenStack.pop();
        if (top)
        {
            m_UISystem.UnloadDocument(top);
        }
    }

    auto doc = m_UISystem.LoadDocument(xamlAssetGuid);
    if (!doc) return;

    if (vm)
    {
        doc->SetDataContext(vm.get());
    }
    doc->Show();
    m_ScreenStack.push(std::move(doc));
}

// ─────────────────────────────────────────────────────────────────────────────
// HUD
// ─────────────────────────────────────────────────────────────────────────────

void VansUIScreenManager::SetHUD(const std::string& xamlAssetGuid,
                                   std::shared_ptr<VansUIViewModel> vm)
{
    if (m_HUDDocument)
    {
        m_UISystem.UnloadDocument(m_HUDDocument);
        m_HUDDocument.reset();
    }

    m_HUDDocument = m_UISystem.LoadDocument(xamlAssetGuid);
    if (m_HUDDocument && vm)
    {
        m_HUDDocument->SetDataContext(vm.get());
    }
}

void VansUIScreenManager::ShowHUD()
{
    if (m_HUDDocument) m_HUDDocument->Show();
}

void VansUIScreenManager::HideHUD()
{
    if (m_HUDDocument) m_HUDDocument->Hide();
}

// ─────────────────────────────────────────────────────────────────────────────
// Modal
// ─────────────────────────────────────────────────────────────────────────────

void VansUIScreenManager::ShowModal(const std::string& xamlAssetGuid,
                                     std::shared_ptr<VansUIViewModel> vm)
{
    // Only one modal at a time
    if (m_ModalDocument)
    {
        HideModal();
    }

    m_ModalDocument = m_UISystem.LoadDocument(xamlAssetGuid);
    if (!m_ModalDocument) return;

    if (vm)
    {
        m_ModalDocument->SetDataContext(vm.get());
    }
    m_ModalDocument->Show();
}

void VansUIScreenManager::HideModal()
{
    if (!m_ModalDocument) return;

    const auto existing = std::find_if(m_Screens.begin(), m_Screens.end(),
        [document = m_ModalDocument](const std::shared_ptr<VansUIScreen>& screen)
        {
            return screen && screen->GetDocument() == document;
        });
    if (existing != m_Screens.end())
    {
        UnloadScreen(*existing);
        return;
    }

    m_UISystem.UnloadDocument(m_ModalDocument);
    m_ModalDocument.reset();
}

bool VansUIScreenManager::IsModalVisible() const
{
    return m_ModalDocument && m_ModalDocument->IsVisible();
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlays
// ─────────────────────────────────────────────────────────────────────────────

void VansUIScreenManager::ShowOverlay(const std::string& name,
                                       const std::string& xamlAssetGuid,
                                       std::shared_ptr<VansUIViewModel> vm)
{
    // Hide existing overlay with the same name first
    HideOverlay(name);

    auto doc = m_UISystem.LoadDocument(xamlAssetGuid);
    if (!doc) return;

    if (vm)
    {
        doc->SetDataContext(vm.get());
    }
    doc->Show();
    m_Overlays[name] = std::move(doc);
}

void VansUIScreenManager::HideOverlay(const std::string& name)
{
    auto it = m_Overlays.find(name);
    if (it == m_Overlays.end()) return;

    if (it->second)
    {
        m_UISystem.UnloadDocument(it->second);
    }
    m_Overlays.erase(it);
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<VansUIDocument> VansUIScreenManager::GetTopScreen() const
{
    if (m_ScreenStack.empty()) return nullptr;
    return m_ScreenStack.top();
}

std::shared_ptr<VansUIScreen> VansUIScreenManager::GetScreen(VansUIHandleId handle) const
{
    if (handle == kInvalidUIHandle)
        return nullptr;

    for (const auto& screen : m_Screens)
        if (screen && screen->GetHandleId() == handle)
            return screen;
    return nullptr;
}

std::shared_ptr<VansUIScreen> VansUIScreenManager::GetScreenByName(const std::string& name) const
{
    if (name.empty())
        return nullptr;

    for (const auto& screen : m_Screens)
        if (screen && (screen->GetName() == name || screen->GetGuid() == name))
            return screen;
    return nullptr;
}

bool VansUIScreenManager::IsScreenStackEmpty() const
{
    return m_ScreenStack.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// CloseAll
// ─────────────────────────────────────────────────────────────────────────────

void VansUIScreenManager::CloseAll()
{
    m_PreloadedScreens.clear();
    m_ConfigScreenStack.clear();
    std::vector<std::shared_ptr<VansUIScreen>> screens = std::move(m_Screens);
    m_Screens.clear();
    for (const auto& screen : screens)
    {
        if (!screen)
            continue;
        screen->Close();
        if (auto document = screen->GetDocument())
        {
            m_UISystem.UnloadDocument(document);
            if (document == m_HUDDocument)
                m_HUDDocument.reset();
            if (document == m_ModalDocument)
                m_ModalDocument.reset();
            for (auto it = m_Overlays.begin(); it != m_Overlays.end();)
            {
                if (it->second == document)
                    it = m_Overlays.erase(it);
                else
                    ++it;
            }
        }
    }

    // Overlays
    for (auto& [name, doc] : m_Overlays)
    {
        if (doc) m_UISystem.UnloadDocument(doc);
    }
    m_Overlays.clear();

    // Modal
    if (m_ModalDocument)
    {
        m_UISystem.UnloadDocument(m_ModalDocument);
        m_ModalDocument.reset();
    }

    // Stack
    while (!m_ScreenStack.empty())
    {
        auto top = m_ScreenStack.top();
        m_ScreenStack.pop();
        if (top) m_UISystem.UnloadDocument(top);
    }

    // HUD
    if (m_HUDDocument)
    {
        m_UISystem.UnloadDocument(m_HUDDocument);
        m_HUDDocument.reset();
    }
}

void VansUIScreenManager::CloseScreen(VansUIHandleId handle)
{
    UnloadScreen(GetScreen(handle));
}

void VansUIScreenManager::CloseScreenByName(const std::string& name)
{
    UnloadScreen(GetScreenByName(name));
}

} // namespace VansRuntime
