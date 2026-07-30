#include "VansUIComponentConfigReader.h"

#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <utility>

namespace VansRuntime
{
    namespace
    {
        VansUIVariant ReadVariant(const Vans::VansSerializedValue& value)
        {
            switch (value.kind)
            {
            case Vans::VansSerializedValue::Kind::Bool:
                return VansUIVariant(value.boolValue);
            case Vans::VansSerializedValue::Kind::Int:
                return VansUIVariant(value.intValue);
            case Vans::VansSerializedValue::Kind::Float:
                return VansUIVariant(value.floatValue);
            case Vans::VansSerializedValue::Kind::String:
                return VansUIVariant(value.stringValue);
            case Vans::VansSerializedValue::Kind::Array:
            {
                VansUIVariantArray result;
                result.reserve(value.arrayItems.size());
                for (const Vans::VansSerializedValue& item : value.arrayItems)
                    result.push_back(ReadVariant(item));
                return VansUIVariant(std::move(result));
            }
            case Vans::VansSerializedValue::Kind::Object:
            {
                VansUIVariantMap result;
                for (const auto& [name, field] : value.objectFields)
                    result.emplace(name, ReadVariant(field));
                return VansUIVariant(std::move(result));
            }
            case Vans::VansSerializedValue::Kind::Null:
            default:
                return VansUIVariant();
            }
        }
    }

    bool VansUIComponentConfigReader::Read(
        const Vans::VansSerializedValue& root,
        VansUIComponentConfig& config,
        std::vector<std::string>& diagnostics)
    {
        if (root.kind != Vans::VansSerializedValue::Kind::Object)
        {
            diagnostics.push_back("UI component config root must be an object.");
            return false;
        }

        config.schemaVersion = static_cast<std::uint32_t>(
            std::max<std::int64_t>(1, Vans::ReadSerializedIntField(root, "schemaVersion", 1)));
        config.guid = Vans::ReadSerializedStringField(root, "guid");
        config.name = Vans::ReadSerializedStringField(root, "name");
        config.xamlPath = Vans::ReadSerializedStringField(root, "xaml");

        if (const Vans::VansSerializedValue* properties = Vans::FindObjectField(root, "publicProperties");
            properties && properties->kind == Vans::VansSerializedValue::Kind::Array)
        {
            for (const Vans::VansSerializedValue& propertyValue : properties->arrayItems)
            {
                if (propertyValue.kind != Vans::VansSerializedValue::Kind::Object)
                    continue;

                VansUIComponentPropertyConfig property;
                property.name = Vans::ReadSerializedStringField(propertyValue, "name");
                property.type = Vans::ReadSerializedStringField(propertyValue, "type");
                property.binding = Vans::ReadSerializedStringField(propertyValue, "binding");
                if (const Vans::VansSerializedValue* defaultValue = Vans::FindObjectField(propertyValue, "default"))
                    property.defaultValue = ReadVariant(*defaultValue);

                if (!property.name.empty())
                    config.publicProperties.push_back(std::move(property));
            }
        }

        if (const Vans::VansSerializedValue* events = Vans::FindObjectField(root, "events");
            events && events->kind == Vans::VansSerializedValue::Kind::Array)
        {
            for (const Vans::VansSerializedValue& eventValue : events->arrayItems)
            {
                if (eventValue.kind != Vans::VansSerializedValue::Kind::Object)
                    continue;

                VansUIComponentEventConfig event;
                event.source = Vans::ReadSerializedStringField(eventValue, "source");
                event.name = Vans::ReadSerializedStringField(eventValue, "name");
                if (event.name.empty())
                    event.name = Vans::ReadSerializedStringField(eventValue, "event");
                event.action = Vans::ReadSerializedStringField(eventValue, "action");
                event.actionParam = Vans::ReadSerializedStringField(eventValue, "actionParam");
                if (const Vans::VansSerializedValue* params = Vans::FindObjectField(eventValue, "params");
                    params && params->kind == Vans::VansSerializedValue::Kind::Object)
                {
                    for (const auto& [name, paramValue] : params->objectFields)
                        event.params.emplace(name, ReadVariant(paramValue));
                }
                if (!event.name.empty())
                    config.events.push_back(std::move(event));
            }
        }

        if (const Vans::VansSerializedValue* states = Vans::FindObjectField(root, "states");
            states && states->kind == Vans::VansSerializedValue::Kind::Array)
        {
            for (const Vans::VansSerializedValue& stateValue : states->arrayItems)
            {
                if (stateValue.kind != Vans::VansSerializedValue::Kind::Object)
                    continue;

                VansUIComponentStateConfig state;
                state.name = Vans::ReadSerializedStringField(stateValue, "name");
                state.isDefault = Vans::ReadSerializedBoolField(stateValue, "default", false);
                if (!state.name.empty())
                    config.states.push_back(std::move(state));
            }
        }

        if (const Vans::VansSerializedValue* animations = Vans::FindObjectField(root, "animations");
            animations && animations->kind == Vans::VansSerializedValue::Kind::Object)
        {
            for (const auto& [name, value] : animations->objectFields)
            {
                if (value.kind == Vans::VansSerializedValue::Kind::String)
                    config.animations.emplace(name, value.stringValue);
            }
        }

        if (config.guid.empty())
            diagnostics.push_back("UI component config is missing required field: guid.");
        if (config.name.empty())
            diagnostics.push_back("UI component config is missing required field: name.");
        if (config.xamlPath.empty())
            diagnostics.push_back("UI component config is missing required field: xaml.");

        return diagnostics.empty();
    }
}
