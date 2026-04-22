#include "Public/VansUIScreenManager.h"
#include "Public/VansUISystem.h"
#include "Public/VansUIDocument.h"
#include "Public/VansUIViewModel.h"

#include <cassert>

namespace VansRuntime
{

VansUIScreenManager::VansUIScreenManager(VansUISystem& uiSystem)
    : m_UISystem(uiSystem)
{}

VansUIScreenManager::~VansUIScreenManager()
{
    CloseAll();
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen stack
// ─────────────────────────────────────────────────────────────────────────────

void VansUIScreenManager::PushScreen(const std::string& xamlPath,
                                      std::shared_ptr<VansUIViewModel> vm)
{
    // Hide the current top screen before pushing a new one
    if (!m_ScreenStack.empty() && m_ScreenStack.top())
    {
        m_ScreenStack.top()->Hide();
    }

    auto doc = m_UISystem.LoadDocument(xamlPath);
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

void VansUIScreenManager::ReplaceScreen(const std::string& xamlPath,
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

    auto doc = m_UISystem.LoadDocument(xamlPath);
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

void VansUIScreenManager::SetHUD(const std::string& xamlPath,
                                   std::shared_ptr<VansUIViewModel> vm)
{
    if (m_HUDDocument)
    {
        m_UISystem.UnloadDocument(m_HUDDocument);
        m_HUDDocument.reset();
    }

    m_HUDDocument = m_UISystem.LoadDocument(xamlPath);
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

void VansUIScreenManager::ShowModal(const std::string& xamlPath,
                                     std::shared_ptr<VansUIViewModel> vm)
{
    // Only one modal at a time
    if (m_ModalDocument)
    {
        HideModal();
    }

    m_ModalDocument = m_UISystem.LoadDocument(xamlPath);
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
                                       const std::string& xamlPath,
                                       std::shared_ptr<VansUIViewModel> vm)
{
    // Hide existing overlay with the same name first
    HideOverlay(name);

    auto doc = m_UISystem.LoadDocument(xamlPath);
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

bool VansUIScreenManager::IsScreenStackEmpty() const
{
    return m_ScreenStack.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// CloseAll
// ─────────────────────────────────────────────────────────────────────────────

void VansUIScreenManager::CloseAll()
{
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

} // namespace VansRuntime
