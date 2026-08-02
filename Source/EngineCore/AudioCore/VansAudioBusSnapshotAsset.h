#pragma once

#include "VansAudioBus.h"

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Vans
{
struct VansAudioBusSnapshotAsset
{
    std::string guid;
    std::string displayName = "Audio Bus Snapshot";
    VansEngine::AudioBusSnapshot snapshot;
};

inline float ReadAudioBusSnapshotFloat(
    const VansSerializedValue& object,
    const char* key,
    float fallback)
{
    const VansSerializedValue* field = FindObjectField(object, key);
    return field
        ? static_cast<float>(ReadSerializedNumber(*field, fallback))
        : fallback;
}

inline bool TryReadAudioBusSnapshotEntry(
    const VansSerializedValue& key,
    const VansSerializedValue& value,
    VansEngine::AudioBusSnapshotEntry& entry)
{
    if ((value.kind == VansSerializedValue::Kind::Float ||
        value.kind == VansSerializedValue::Kind::Int) &&
        key.kind == VansSerializedValue::Kind::String)
    {
        entry.busName = VansEngine::NormalizeAudioBusName(key.stringValue);
        entry.gain = std::clamp(
            static_cast<float>(ReadSerializedNumber(value, entry.gain)),
            0.0f,
            4.0f);
        return true;
    }

    if (value.kind != VansSerializedValue::Kind::Object)
        return false;

    entry.busName = ReadSerializedStringField(value, "bus");
    if (entry.busName.empty())
        entry.busName = ReadSerializedStringField(value, "name");
    if (entry.busName.empty() && key.kind == VansSerializedValue::Kind::String)
        entry.busName = key.stringValue;
    if (entry.busName.empty())
        return false;
    entry.busName = VansEngine::NormalizeAudioBusName(entry.busName);
    entry.gain = std::clamp(ReadAudioBusSnapshotFloat(value, "gain", entry.gain), 0.0f, 4.0f);

    if (const VansSerializedValue* muted = FindObjectField(value, "muted");
        muted && muted->kind == VansSerializedValue::Kind::Bool)
    {
        entry.muted = muted->boolValue;
        entry.overrideMuted = true;
    }
    if (const VansSerializedValue* soloed = FindObjectField(value, "soloed");
        soloed && soloed->kind == VansSerializedValue::Kind::Bool)
    {
        entry.soloed = soloed->boolValue;
        entry.overrideSoloed = true;
    }
    if (const VansSerializedValue* lowpass = FindObjectField(value, "lowpassHighFrequencyGain");
        lowpass && (lowpass->kind == VansSerializedValue::Kind::Float ||
            lowpass->kind == VansSerializedValue::Kind::Int))
    {
        entry.lowpassHighFrequencyGain = std::clamp(
            static_cast<float>(ReadSerializedNumber(*lowpass, entry.lowpassHighFrequencyGain)),
            0.0f,
            1.0f);
        entry.overrideLowpassHighFrequencyGain = true;
    }
    return true;
}

inline bool ReadAudioBusSnapshotAsset(
    const VansSerializedValue& root,
    VansAudioBusSnapshotAsset& asset,
    std::string& error)
{
    if (root.kind != VansSerializedValue::Kind::Object)
    {
        error = "Audio bus snapshot asset root must be an object";
        return false;
    }

    VansAudioBusSnapshotAsset parsed;
    parsed.guid = ReadSerializedStringField(root, "guid");
    parsed.displayName = ReadSerializedStringField(root, "displayName", parsed.displayName);
    parsed.snapshot.fadeSeconds = std::clamp(
        ReadAudioBusSnapshotFloat(root, "fadeSeconds", parsed.snapshot.fadeSeconds),
        0.0f,
        60.0f);
    parsed.snapshot.fadeSeconds = std::clamp(
        ReadAudioBusSnapshotFloat(root, "fade_seconds", parsed.snapshot.fadeSeconds),
        0.0f,
        60.0f);

    const VansSerializedValue* buses = FindObjectField(root, "buses");
    if (!buses)
    {
        error = "Audio bus snapshot asset must contain a buses field";
        return false;
    }

    if (buses->kind == VansSerializedValue::Kind::Array)
    {
        for (const VansSerializedValue& item : buses->arrayItems)
        {
            VansEngine::AudioBusSnapshotEntry entry;
            if (TryReadAudioBusSnapshotEntry(VansSerializedValue::Null(), item, entry))
                parsed.snapshot.buses.push_back(std::move(entry));
        }
    }
    else if (buses->kind == VansSerializedValue::Kind::Object)
    {
        for (const auto& [busName, busValue] : buses->objectFields)
        {
            VansEngine::AudioBusSnapshotEntry entry;
            if (TryReadAudioBusSnapshotEntry(VansSerializedValue::String(busName), busValue, entry))
                parsed.snapshot.buses.push_back(std::move(entry));
        }
    }
    else
    {
        error = "Audio bus snapshot buses field must be an array or object";
        return false;
    }

    if (parsed.snapshot.buses.empty())
    {
        error = "Audio bus snapshot asset did not contain any valid bus entries";
        return false;
    }

    asset = std::move(parsed);
    return true;
}

inline VansSerializedValue WriteAudioBusSnapshotEntry(
    VansEngine::AudioBusSnapshotEntry entry)
{
    entry.busName = VansEngine::NormalizeAudioBusName(entry.busName);
    entry.gain = std::clamp(entry.gain, 0.0f, 4.0f);

    VansSerializedValue value = VansSerializedValue::Object({
        { "bus", VansSerializedValue::String(entry.busName) },
        { "gain", VansSerializedValue::Float(entry.gain) }
    });
    if (entry.overrideMuted)
        SetSerializedObjectField(value, "muted", VansSerializedValue::Bool(entry.muted));
    if (entry.overrideSoloed)
        SetSerializedObjectField(value, "soloed", VansSerializedValue::Bool(entry.soloed));
    if (entry.overrideLowpassHighFrequencyGain)
    {
        SetSerializedObjectField(value, "lowpassHighFrequencyGain", VansSerializedValue::Float(
            std::clamp(entry.lowpassHighFrequencyGain, 0.0f, 1.0f)));
    }
    return value;
}

inline VansSerializedValue WriteAudioBusSnapshotAssetRoot(
    const VansAudioBusSnapshotAsset& asset)
{
    std::vector<VansSerializedValue> busValues;
    busValues.reserve(asset.snapshot.buses.size());
    for (const VansEngine::AudioBusSnapshotEntry& entry : asset.snapshot.buses)
        busValues.push_back(WriteAudioBusSnapshotEntry(entry));

    VansSerializedValue root = VansSerializedValue::Object({});
    if (!asset.guid.empty())
        SetSerializedObjectField(root, "guid", VansSerializedValue::String(asset.guid));
    SetSerializedObjectField(root, "assetKind", VansSerializedValue::String("AudioBusSnapshot"));
    SetSerializedObjectField(root, "displayName", VansSerializedValue::String(asset.displayName));
    SetSerializedObjectField(root, "fadeSeconds", VansSerializedValue::Float(
        std::clamp(asset.snapshot.fadeSeconds, 0.0f, 60.0f)));
    SetSerializedObjectField(root, "buses", VansSerializedValue::Array(std::move(busValues)));
    return root;
}
}
