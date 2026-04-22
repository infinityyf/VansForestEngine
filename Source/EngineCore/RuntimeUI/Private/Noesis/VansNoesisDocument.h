#pragma once

#include "../../Public/VansUIDocument.h"
#include "../../Public/VansUIElementHandle.h"

#include <NsCore/Ptr.h>
#include <NsGui/IView.h>
#include <NsGui/FrameworkElement.h>
#include <NsGui/IRenderer.h>

#include <string>
#include <memory>

namespace VansRuntime
{

class VansNoesisInputAdapter;
class VansNoesisRenderDevice;

/// ─────────────────────────────────────────────────────────────────────────────
/// VansNoesisDocument
///
/// Concrete implementation of VansUIDocument backed by a Noesis IView.
///
/// Layout:
///   - m_View        — owns the Noesis view (and IRenderer)
///   - m_Content     — the XAML root FrameworkElement
///   - m_InputAdapter— raw pointer, NOT owned; registered/unregistered here
///   - m_RenderDevice— raw pointer, NOT owned
///
/// Render flow (called by VansNoesisUISystem each frame, in order):
///   1. Update(dt)      — triggers Noesis layout/binding updates
///   2. RenderOffscreen()— executes offscreen render commands
///   3. Render()        — executes on-screen render commands
/// ─────────────────────────────────────────────────────────────────────────────
class VansNoesisDocument : public VansUIDocument
{
public:
    VansNoesisDocument(
        Noesis::Ptr<Noesis::IView>          view,
        Noesis::Ptr<Noesis::FrameworkElement> content,
        std::string                          sourcePath,
        VansNoesisInputAdapter*              inputAdapter);

    ~VansNoesisDocument() override;

    // ── VansUIDocument interface ──────────────────────────────────────────

    void Show()                                  override;
    void Hide()                                  override;
    void SetVisible(bool visible)                override;
    bool IsVisible()                       const override;

    void SetSize(uint32_t width, uint32_t height) override;

    void SetDataContext(VansUIViewModel* vm)      override;

    VansUIElementHandle FindElement(const std::string& name) override;

    const std::string& GetSourcePath()     const override;

    // ── Extended Noesis-specific API ────────────────────────────────────

    /// Per-frame update — must be called before rendering
    void Update(double totalTimeSeconds);

    /// Execute offscreen render passes (e.g., gradients, effects)
    void RenderOffscreen();

    /// Execute the main render pass into the current Vulkan command buffer
    void Render();

    Noesis::IView*            GetView()    const { return m_View.GetPtr(); }
    Noesis::FrameworkElement* GetContent() const { return m_Content.GetPtr(); }

private:
    Noesis::Ptr<Noesis::IView>            m_View;
    Noesis::Ptr<Noesis::FrameworkElement> m_Content;

    std::string             m_SourcePath;
    bool                    m_Visible     = true;

    VansNoesisInputAdapter* m_InputAdapter = nullptr; // NOT owned
};

} // namespace VansRuntime
