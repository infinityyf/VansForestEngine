#include "VansAudioMixValueCodec.h"

#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <initializer_list>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VansEngine
{
namespace
{
using Value = Vans::VansSerializedValue;

bool ReadOptionalNumber(
    const Value& object,
    const char* key,
    float minimum,
    float maximum,
    float& value,
    std::string& error)
{
    const Value* field = Vans::FindObjectField(object, key);
    if (!field)
        return true;
    if (field->kind != Value::Kind::Int && field->kind != Value::Kind::Float)
    {
        error = std::string("Audio mix field '") + key + "' must be numeric";
        return false;
    }
    value = std::clamp(
        static_cast<float>(Vans::ReadSerializedNumber(*field, value)),
        minimum,
        maximum);
    return true;
}

bool ReadOptionalBool(
    const Value& object,
    const char* key,
    bool& value,
    bool& overridden,
    std::string& error)
{
    const Value* field = Vans::FindObjectField(object, key);
    if (!field)
        return true;
    if (field->kind != Value::Kind::Bool)
    {
        error = std::string("Audio mix field '") + key + "' must be boolean";
        return false;
    }
    value = field->boolValue;
    overridden = true;
    return true;
}

bool ValidateFields(
    const Value& object,
    std::initializer_list<const char*> allowed,
    const char* context,
    std::string& error)
{
    for (const auto& field : object.objectFields)
    {
        const std::string& name = field.first;
        const bool known = std::any_of(
            allowed.begin(),
            allowed.end(),
            [&](const char* candidate) { return name == candidate; });
        if (!known)
        {
            error = std::string(context) + " contains unknown field '" + name + "'";
            return false;
        }
    }
    return true;
}

bool ReadRequiredString(
    const Value& object,
    const char* key,
    std::string& value,
    std::string& error)
{
    const Value* field = Vans::FindObjectField(object, key);
    if (!field || field->kind != Value::Kind::String || field->stringValue.empty())
    {
        error = std::string("Audio mix field '") + key + "' must be a non-empty string";
        return false;
    }
    value = field->stringValue;
    return true;
}
}

bool VansAudioMixValueCodec::DecodeSnapshot(
    const Value& value,
    AudioBusSnapshot& snapshot,
    std::string& error)
{
    error.clear();
    if (value.kind != Value::Kind::Object)
    {
        error = "Audio bus snapshot must be an object";
        return false;
    }
    if (!ValidateFields(
        value,
        { "guid", "assetKind", "displayName", "fadeSeconds", "buses" },
        "Audio bus snapshot",
        error))
        return false;

    AudioBusSnapshot parsed;
    if (!ReadOptionalNumber(value, "fadeSeconds", 0.0f, 60.0f, parsed.fadeSeconds, error))
        return false;

    const Value* buses = Vans::FindObjectField(value, "buses");
    if (!buses || buses->kind != Value::Kind::Array)
    {
        error = "Audio bus snapshot 'buses' must be an array";
        return false;
    }

    std::unordered_set<std::string> busNames;
    parsed.buses.reserve(buses->arrayItems.size());
    for (const Value& item : buses->arrayItems)
    {
        if (item.kind != Value::Kind::Object)
        {
            error = "Audio bus snapshot entry must be an object";
            return false;
        }
        if (!ValidateFields(
            item,
            { "bus", "gain", "muted", "soloed", "lowpassHighFrequencyGain" },
            "Audio bus snapshot entry",
            error))
            return false;

        AudioBusSnapshotEntry entry;
        if (!ReadRequiredString(item, "bus", entry.busName, error))
            return false;
        entry.busName = NormalizeAudioBusName(entry.busName);
        if (!busNames.insert(entry.busName).second)
        {
            error = "Audio bus snapshot contains duplicate bus '" + entry.busName + "'";
            return false;
        }
        if (!ReadOptionalNumber(item, "gain", 0.0f, 4.0f, entry.gain, error) ||
            !ReadOptionalBool(item, "muted", entry.muted, entry.overrideMuted, error) ||
            !ReadOptionalBool(item, "soloed", entry.soloed, entry.overrideSoloed, error) ||
            !ReadOptionalNumber(
                item,
                "lowpassHighFrequencyGain",
                0.0f,
                1.0f,
                entry.lowpassHighFrequencyGain,
                error))
        {
            return false;
        }
        entry.overrideLowpassHighFrequencyGain =
            Vans::FindObjectField(item, "lowpassHighFrequencyGain") != nullptr;
        parsed.buses.push_back(std::move(entry));
    }

    if (parsed.buses.empty())
    {
        error = "Audio bus snapshot must contain at least one bus entry";
        return false;
    }
    snapshot = std::move(parsed);
    return true;
}

Value VansAudioMixValueCodec::EncodeSnapshot(const AudioBusSnapshot& snapshot)
{
    std::vector<Value> buses;
    buses.reserve(snapshot.buses.size());
    for (AudioBusSnapshotEntry entry : snapshot.buses)
    {
        entry.busName = NormalizeAudioBusName(entry.busName);
        Value value = Value::Object({
            { "bus", Value::String(entry.busName) },
            { "gain", Value::Float(std::clamp(entry.gain, 0.0f, 4.0f)) }
        });
        if (entry.overrideMuted)
            Vans::SetSerializedObjectField(value, "muted", Value::Bool(entry.muted));
        if (entry.overrideSoloed)
            Vans::SetSerializedObjectField(value, "soloed", Value::Bool(entry.soloed));
        if (entry.overrideLowpassHighFrequencyGain)
        {
            Vans::SetSerializedObjectField(
                value,
                "lowpassHighFrequencyGain",
                Value::Float(std::clamp(entry.lowpassHighFrequencyGain, 0.0f, 1.0f)));
        }
        buses.push_back(std::move(value));
    }
    return Value::Object({
        { "fadeSeconds", Value::Float(std::clamp(snapshot.fadeSeconds, 0.0f, 60.0f)) },
        { "buses", Value::Array(std::move(buses)) }
    });
}

bool VansAudioMixValueCodec::DecodeDuckingRule(
    const Value& value,
    AudioDuckingRule& rule,
    std::string& error)
{
    error.clear();
    if (value.kind != Value::Kind::Object)
    {
        error = "Audio ducking rule must be an object";
        return false;
    }

    if (!ValidateFields(
        value,
        { "triggerBus", "targetBus", "targetGain", "attackSeconds", "releaseSeconds", "enabled" },
        "Audio ducking rule",
        error))
    {
        return false;
    }

    AudioDuckingRule parsed;
    if (!ReadRequiredString(value, "triggerBus", parsed.triggerBusName, error) ||
        !ReadRequiredString(value, "targetBus", parsed.targetBusName, error) ||
        !ReadOptionalNumber(value, "targetGain", 0.0f, 1.0f, parsed.targetGain, error) ||
        !ReadOptionalNumber(value, "attackSeconds", 0.0f, 10.0f, parsed.attackSeconds, error) ||
        !ReadOptionalNumber(value, "releaseSeconds", 0.0f, 10.0f, parsed.releaseSeconds, error))
    {
        return false;
    }
    if (const Value* enabled = Vans::FindObjectField(value, "enabled"))
    {
        if (enabled->kind != Value::Kind::Bool)
        {
            error = "Audio mix field 'enabled' must be boolean";
            return false;
        }
        parsed.enabled = enabled->boolValue;
    }
    parsed.Normalize();
    if (parsed.triggerBusName == parsed.targetBusName)
    {
        error = "Audio ducking triggerBus and targetBus must differ";
        return false;
    }
    rule = std::move(parsed);
    return true;
}

Value VansAudioMixValueCodec::EncodeDuckingRule(const AudioDuckingRule& rule)
{
    AudioDuckingRule normalized = rule;
    normalized.Normalize();
    return Value::Object({
        { "triggerBus", Value::String(normalized.triggerBusName) },
        { "targetBus", Value::String(normalized.targetBusName) },
        { "targetGain", Value::Float(normalized.targetGain) },
        { "attackSeconds", Value::Float(normalized.attackSeconds) },
        { "releaseSeconds", Value::Float(normalized.releaseSeconds) },
        { "enabled", Value::Bool(normalized.enabled) }
    });
}
}
