#include "Public/VansUIElementHandle.h"

#include <NsGui/BaseButton.h>
#include <NsGui/ContentControl.h>
#include <NsGui/RoutedEvent.h>
#include <NsGui/TextBlock.h>
#include <NsGui/UIElement.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <utility>

namespace VansRuntime
{
    namespace
    {
        Noesis::UIElement* GetElement(void* native)
        {
            return static_cast<Noesis::UIElement*>(native);
        }

        std::string Lowercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
        }
    }

    bool VansUIElementHandle::IsValid() const
    {
        return m_NativeElement != nullptr;
    }

    void VansUIElementHandle::SetText(const std::string& text)
    {
        if (!IsValid())
            return;

        auto* element = GetElement(m_NativeElement);
        if (auto* textBlock = Noesis::DynamicCast<Noesis::TextBlock*>(element))
        {
            textBlock->SetText(text.c_str());
            return;
        }

        if (auto* contentControl = Noesis::DynamicCast<Noesis::ContentControl*>(element))
            contentControl->SetContent(Noesis::Boxing::Box<Noesis::String>(text.c_str()).GetPtr());
    }

    std::string VansUIElementHandle::GetText() const
    {
        if (!IsValid())
            return {};

        auto* element = GetElement(m_NativeElement);
        if (auto* textBlock = Noesis::DynamicCast<Noesis::TextBlock*>(element))
        {
            const char* text = textBlock->GetText();
            return text ? text : "";
        }

        return {};
    }

    void VansUIElementHandle::SetVisible(bool visible)
    {
        if (!IsValid())
            return;

        GetElement(m_NativeElement)->SetVisibility(
            visible ? Noesis::Visibility_Visible : Noesis::Visibility_Collapsed);
    }

    bool VansUIElementHandle::IsVisible() const
    {
        if (!IsValid())
            return false;

        return GetElement(m_NativeElement)->GetVisibility() == Noesis::Visibility_Visible;
    }

    void VansUIElementHandle::BindClick(std::function<void()> callback)
    {
        if (!IsValid())
            return;

        auto* element = GetElement(m_NativeElement);
        if (auto* button = Noesis::DynamicCast<Noesis::BaseButton*>(element))
        {
            auto sharedCallback = std::make_shared<std::function<void()>>(std::move(callback));
            button->Click() += [sharedCallback](
                Noesis::BaseComponent* /*sender*/,
                const Noesis::RoutedEventArgs& /*args*/)
            {
                if (*sharedCallback)
                    (*sharedCallback)();
            };
        }
    }

    void VansUIElementHandle::SetProperty(const std::string& property,
                                          const std::string& value)
    {
        if (!IsValid())
            return;

        const std::string name = Lowercase(property);
        if (name == "text" || name == "content")
        {
            SetText(value);
            return;
        }

        auto* element = GetElement(m_NativeElement);
        if (name == "visible" || name == "visibility")
        {
            const std::string normalizedValue = Lowercase(value);
            element->SetVisibility(
                normalizedValue == "false" || normalizedValue == "0" ||
                normalizedValue == "hidden" || normalizedValue == "collapsed"
                    ? Noesis::Visibility_Collapsed
                    : Noesis::Visibility_Visible);
            return;
        }

        if (name == "opacity")
        {
            char* parseEnd = nullptr;
            const double parsed = std::strtod(value.c_str(), &parseEnd);
            if (parseEnd != value.c_str())
                element->SetOpacity(static_cast<float>(std::clamp(parsed, 0.0, 1.0)));
        }
    }
}
