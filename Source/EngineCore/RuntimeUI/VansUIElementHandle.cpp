#include "Public/VansUIElementHandle.h"

// Noesis headers — only included in the .cpp (not in the public header)
#include <NsGui/UIElement.h>
#include <NsGui/TextBlock.h>
#include <NsGui/ContentControl.h>
#include <NsGui/BaseButton.h>
#include <NsGui/RoutedEvent.h>

namespace VansRuntime
{

// ─────────────────────────────────────────────────────────────────────────────
// Internal casting helper
// ─────────────────────────────────────────────────────────────────────────────

static Noesis::UIElement* GetElement(void* native)
{
    return static_cast<Noesis::UIElement*>(native);
}

// ─────────────────────────────────────────────────────────────────────────────

bool VansUIElementHandle::IsValid() const
{
    return m_NativeElement != nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Text
// ─────────────────────────────────────────────────────────────────────────────

void VansUIElementHandle::SetText(const std::string& text)
{
    if (!IsValid()) return;

    auto* element = GetElement(m_NativeElement);

    // TextBlock
    if (auto* tb = Noesis::DynamicCast<Noesis::TextBlock*>(element))
    {
        tb->SetText(text.c_str());
        return;
    }

    // ContentControl (Button, Label, etc.) — sets its Content string
    if (auto* cc = Noesis::DynamicCast<Noesis::ContentControl*>(element))
    {
        // ContentControl::SetContent takes a BaseComponent*, so we box the string
        cc->SetContent(Noesis::Boxing::Box<Noesis::String>(text.c_str()).GetPtr());
        return;
    }
}

std::string VansUIElementHandle::GetText() const
{
    if (!IsValid()) return {};

    auto* element = GetElement(m_NativeElement);

    if (auto* tb = Noesis::DynamicCast<Noesis::TextBlock*>(element))
    {
        const char* txt = tb->GetText();
        return txt ? txt : "";
    }

    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Visibility
// ─────────────────────────────────────────────────────────────────────────────

void VansUIElementHandle::SetVisible(bool visible)
{
    if (!IsValid()) return;
    GetElement(m_NativeElement)->SetVisibility(
        visible ? Noesis::Visibility_Visible : Noesis::Visibility_Collapsed);
}

bool VansUIElementHandle::IsVisible() const
{
    if (!IsValid()) return false;
    return GetElement(m_NativeElement)->GetVisibility() == Noesis::Visibility_Visible;
}

// ─────────────────────────────────────────────────────────────────────────────
// Click binding
// ─────────────────────────────────────────────────────────────────────────────

void VansUIElementHandle::BindClick(std::function<void()> callback)
{
    if (!IsValid()) return;

    auto* element = GetElement(m_NativeElement);
    if (auto* btn = Noesis::DynamicCast<Noesis::BaseButton*>(element))
    {
        // Use a heap-allocated copy of the callback that survives the lambda capture
        auto* cb = new std::function<void()>(std::move(callback));

        btn->Click() += [cb](Noesis::BaseComponent* /*sender*/,
                             const Noesis::RoutedEventArgs& /*args*/)
        {
            if (*cb) (*cb)();
        };

        // NOTE: The callback is intentionally leaked here for simplicity.
        //       A production implementation would attach its lifetime to the element.
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Generic property setter
// ─────────────────────────────────────────────────────────────────────────────

void VansUIElementHandle::SetProperty(const std::string& /*property*/,
                                      const std::string& /*value*/)
{
    // TODO: Use Noesis::DependencyObject::SetValue with reflection-based property lookup.
    //       Requires mapping property name → DependencyProperty* via the type system.
    //       Deferred to when VansNoesisTypeRegistry is complete.
}

} // namespace VansRuntime
