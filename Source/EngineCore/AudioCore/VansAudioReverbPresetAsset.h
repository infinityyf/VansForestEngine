#pragma once

#include "VansAudioReverbPreset.h"

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <string>
#include <utility>

namespace Vans
{
struct VansAudioReverbPresetAsset
{
    std::string guid;
    std::string displayName = "Reverb Preset";
    VansEngine::AudioReverbPresetParameters parameters;
};

inline float ReadAudioReverbPresetFloat(
    const VansSerializedValue& object,
    const char* key,
    float fallback)
{
    const VansSerializedValue* field = FindObjectField(object, key);
    return field
        ? static_cast<float>(ReadSerializedNumber(*field, fallback))
        : fallback;
}

inline bool ReadAudioReverbPresetAsset(
    const VansSerializedValue& root,
    VansAudioReverbPresetAsset& asset,
    std::string& error)
{
    if (root.kind != VansSerializedValue::Kind::Object)
    {
        error = "Audio reverb preset asset root must be an object";
        return false;
    }

    VansAudioReverbPresetAsset parsed;
    parsed.guid = ReadSerializedStringField(root, "guid");
    parsed.displayName = ReadSerializedStringField(root, "displayName", parsed.displayName);

    if (const VansSerializedValue* preset = FindObjectField(root, "preset"))
    {
        parsed.parameters = VansEngine::GetAudioReverbPresetParameters(
            VansEngine::AudioReverbPresetFromString(ReadSerializedString(*preset)));
    }

    const VansSerializedValue* parameterObject = FindObjectField(root, "parameters");
    if (parameterObject && parameterObject->kind != VansSerializedValue::Kind::Object)
    {
        error = "Audio reverb preset parameters must be an object";
        return false;
    }

    const VansSerializedValue& source = parameterObject ? *parameterObject : root;
    parsed.parameters.density = ReadAudioReverbPresetFloat(source, "density", parsed.parameters.density);
    parsed.parameters.diffusion = ReadAudioReverbPresetFloat(source, "diffusion", parsed.parameters.diffusion);
    parsed.parameters.gain = ReadAudioReverbPresetFloat(source, "gain", parsed.parameters.gain);
    parsed.parameters.gainHF = ReadAudioReverbPresetFloat(source, "gainHF", parsed.parameters.gainHF);
    parsed.parameters.decayTime = ReadAudioReverbPresetFloat(source, "decayTime", parsed.parameters.decayTime);
    parsed.parameters.Normalize();

    asset = std::move(parsed);
    return true;
}

inline VansSerializedValue WriteAudioReverbPresetParameters(
    VansEngine::AudioReverbPresetParameters parameters)
{
    parameters.Normalize();
    return VansSerializedValue::Object({
        { "density", VansSerializedValue::Float(parameters.density) },
        { "diffusion", VansSerializedValue::Float(parameters.diffusion) },
        { "gain", VansSerializedValue::Float(parameters.gain) },
        { "gainHF", VansSerializedValue::Float(parameters.gainHF) },
        { "decayTime", VansSerializedValue::Float(parameters.decayTime) }
    });
}

inline VansSerializedValue WriteAudioReverbPresetAssetRoot(
    const VansAudioReverbPresetAsset& asset)
{
    VansSerializedValue root = VansSerializedValue::Object({});
    if (!asset.guid.empty())
        SetSerializedObjectField(root, "guid", VansSerializedValue::String(asset.guid));
    SetSerializedObjectField(root, "assetKind", VansSerializedValue::String("AudioReverbPreset"));
    SetSerializedObjectField(root, "displayName", VansSerializedValue::String(asset.displayName));
    SetSerializedObjectField(root, "parameters", WriteAudioReverbPresetParameters(asset.parameters));
    return root;
}
}
