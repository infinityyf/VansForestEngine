#include "VansNoesisDocument.h"
#include "VansNoesisInputAdapter.h"

#include "../../Public/VansUIViewModel.h"

#include <NsGui/IView.h>
#include <NsGui/IRenderer.h>
#include <NsGui/FrameworkElement.h>
#include <NsGui/DependencyObject.h>
#include <NsGui/LogicalTreeHelper.h>

#include <cassert>

namespace VansRuntime
{

// ─────────────────────────────────────────────────────────────────────────────

VansNoesisDocument::VansNoesisDocument(
    Noesis::Ptr<Noesis::IView>            view,
    Noesis::Ptr<Noesis::FrameworkElement> content,
    std::string                           sourcePath,
    VansNoesisInputAdapter*               inputAdapter)
    : m_View(std::move(view))
    , m_Content(std::move(content))
    , m_SourcePath(std::move(sourcePath))
    , m_InputAdapter(inputAdapter)
{
    // Register this view with the input adapter so it receives input events
    if (m_InputAdapter && m_View)
    {
        m_InputAdapter->AddView(m_View.GetPtr());
    }
}

VansNoesisDocument::~VansNoesisDocument()
{
    if (m_InputAdapter && m_View)
    {
        m_InputAdapter->RemoveView(m_View.GetPtr());
    }

    // Explicitly reset in reverse order of dependency
    m_Content.Reset();
    m_View.Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Visibility
// ─────────────────────────────────────────────────────────────────────────────

void VansNoesisDocument::Show()
{
    m_Visible = true;
}

void VansNoesisDocument::Hide()
{
    m_Visible = false;
}

void VansNoesisDocument::SetVisible(bool visible)
{
    m_Visible = visible;
}

bool VansNoesisDocument::IsVisible() const
{
    return m_Visible;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

void VansNoesisDocument::SetSize(uint32_t width, uint32_t height)
{
    if (m_View)
    {
        m_View->SetSize(width, height);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Data binding
// ─────────────────────────────────────────────────────────────────────────────

void VansNoesisDocument::SetDataContext(VansUIViewModel* /*vm*/)
{
    // TODO: implement when VansUIViewModel ↔ Noesis::INotifyPropertyChanged
    //       bridging is complete (VansNoesisTypeRegistry)
    // m_Content->SetDataContext(vm->GetNoesisObject());
}

// ─────────────────────────────────────────────────────────────────────────────
// Element access
// ─────────────────────────────────────────────────────────────────────────────

VansUIElementHandle VansNoesisDocument::FindElement(const std::string& name)
{
    VansUIElementHandle handle;

    if (!m_Content) return handle;

    // Noesis::LogicalTreeHelper::FindLogicalNode performs a depth-first search
    // through the logical tree (ResourceDictionary, NameScope, etc.)
    Noesis::Ptr<Noesis::FrameworkElement> found =
        Noesis::LogicalTreeHelper::FindLogicalNode(m_Content.GetPtr(), name.c_str());

    if (found)
    {
        handle.m_NativeElement = found.GetPtr();
    }

    return handle;
}

// ─────────────────────────────────────────────────────────────────────────────
// Source path
// ─────────────────────────────────────────────────────────────────────────────

const std::string& VansNoesisDocument::GetSourcePath() const
{
    return m_SourcePath;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame update and render
// ─────────────────────────────────────────────────────────────────────────────

void VansNoesisDocument::Update(double totalTimeSeconds)
{
    if (m_View && m_Visible)
    {
        m_View->Update(totalTimeSeconds);
    }
}

void VansNoesisDocument::RenderOffscreen()
{
    if (!m_View || !m_Visible) return;
    m_View->GetRenderer()->UpdateRenderTree();
    m_View->GetRenderer()->RenderOffscreen();
}

void VansNoesisDocument::Render()
{
    if (!m_View || !m_Visible) return;
    m_View->GetRenderer()->Render();
}

} // namespace VansRuntime
