#include "Public/VansUIComponent.h"

#include "Public/VansUIActionBus.h"
#include "Public/VansUIElementHandle.h"

#include <type_traits>
#include <utility>

namespace VansRuntime
{
    namespace
    {
        std::string VariantToString(const VansUIVariant& value)
        {
            return std::visit([](const auto& typedValue) -> std::string
            {
                using T = std::decay_t<decltype(typedValue)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    return {};
                else if constexpr (std::is_same_v<T, bool>)
                    return typedValue ? "true" : "false";
                else if constexpr (std::is_same_v<T, std::int64_t>)
                    return std::to_string(typedValue);
                else if constexpr (std::is_same_v<T, double>)
                    return std::to_string(typedValue);
                else if constexpr (std::is_same_v<T, std::string>)
                    return typedValue;
                else if constexpr (std::is_same_v<T, VansUIHandleId>)
                    return std::to_string(typedValue);
                else
                    return {};
            }, value.value);
        }
    }

    VansUIComponentInstance::VansUIComponentInstance(
        VansUIHandleId handle,
        VansUIComponentConfig config,
        std::shared_ptr<VansUIDocument> document)
        : m_Handle(handle)
        , m_Config(std::move(config))
        , m_Document(std::move(document))
    {
        for (const VansUIComponentPropertyConfig& property : m_Config.publicProperties)
        {
            m_Properties[property.name] = property.defaultValue;
            if (!property.name.empty())
                SetProperty(property.name, property.defaultValue);
        }

        for (const VansUIComponentStateConfig& state : m_Config.states)
        {
            if (state.isDefault)
            {
                m_State = state.name;
                break;
            }
        }

        BindConfiguredEvents();
    }

    void VansUIComponentInstance::Close()
    {
        if (m_Document)
            m_Document->Hide();
        m_Document.reset();
    }

    VansUIElementHandle VansUIComponentInstance::FindElement(const std::string& name)
    {
        return m_Document ? m_Document->FindElement(name) : VansUIElementHandle{};
    }

    void VansUIComponentInstance::SetProperty(const std::string& name, const VansUIVariant& value)
    {
        if (name.empty())
            return;

        m_Properties[name] = value;
        const VansUIComponentPropertyConfig* property = FindPropertyConfig(name);
        const std::string target = property && !property->binding.empty() ? property->binding : name;
        VansUIElementHandle element = FindElement(target);
        if (element.IsValid())
            element.SetText(VariantToString(value));
    }

    VansUIVariant VansUIComponentInstance::GetProperty(const std::string& name) const
    {
        const auto it = m_Properties.find(name);
        return it != m_Properties.end() ? it->second : VansUIVariant();
    }

    void VansUIComponentInstance::SetState(const std::string& state)
    {
        m_State = state;
    }

    void VansUIComponentInstance::PlayAnimation(const std::string& name)
    {
        m_LastAnimation = name;
    }

    const VansUIComponentPropertyConfig* VansUIComponentInstance::FindPropertyConfig(
        const std::string& name) const
    {
        for (const VansUIComponentPropertyConfig& property : m_Config.publicProperties)
        {
            if (property.name == name)
                return &property;
        }
        return nullptr;
    }

    void VansUIComponentInstance::BindConfiguredEvents()
    {
        for (const VansUIComponentEventConfig& event : m_Config.events)
        {
            if (event.name != "Click")
                continue;

            VansUIElementHandle element = FindElement(event.source);
            if (!element.IsValid())
                continue;

            const VansUIHandleId componentId = m_Handle;
            const std::string source = event.source;
            const std::string actionName = !event.action.empty()
                ? event.action
                : m_Config.guid + "." + event.name;
            const std::string actionParam = event.actionParam;
            const VansUIVariantMap baseParams = event.params;

            element.BindClick([this, componentId, source, actionName, actionParam, baseParams]()
            {
                VansUIVariantMap params = baseParams;
                params["component"] = VansUIVariant(componentId);
                if (!actionParam.empty())
                    params[actionParam] = GetProperty(actionParam);

                VansUIActionBus::Get().Dispatch(VansUIAction{
                    actionName,
                    std::move(params),
                    kInvalidUIHandle,
                    source
                });
            });
        }
    }
}
