#pragma once

#include "Serialization/VansAudioMixValueCodec.h"

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <string>
#include <utility>
#include <vector>

namespace Vans
{
struct VansAudioDuckingRulesAsset
{
    std::string guid;
    std::string displayName = "Audio Ducking Rules";
    std::vector<VansEngine::AudioDuckingRule> rules;
};

inline bool ReadAudioDuckingRulesAsset(
    const VansSerializedValue& root,
    VansAudioDuckingRulesAsset& asset,
    std::string& error)
{
    if (root.kind != VansSerializedValue::Kind::Object)
    {
        error = "Audio ducking rules asset root must be an object";
        return false;
    }

    VansAudioDuckingRulesAsset parsed;
    parsed.guid = ReadSerializedStringField(root, "guid");
    parsed.displayName = ReadSerializedStringField(root, "displayName", parsed.displayName);

    const VansSerializedValue* rules = FindObjectField(root, "rules");
    if (!rules)
    {
        error = "Audio ducking rules asset must contain a rules field";
        return false;
    }
    if (rules->kind != VansSerializedValue::Kind::Array)
    {
        error = "Audio ducking rules field must be an array";
        return false;
    }

    for (const VansSerializedValue& item : rules->arrayItems)
    {
        VansEngine::AudioDuckingRule rule;
        if (!VansEngine::VansAudioMixValueCodec::DecodeDuckingRule(item, rule, error))
            return false;
        parsed.rules.push_back(std::move(rule));
    }

    if (parsed.rules.empty())
    {
        error = "Audio ducking rules asset did not contain any valid rules";
        return false;
    }

    asset = std::move(parsed);
    return true;
}

inline VansSerializedValue WriteAudioDuckingRulesAssetRoot(
    const VansAudioDuckingRulesAsset& asset)
{
    std::vector<VansSerializedValue> ruleValues;
    ruleValues.reserve(asset.rules.size());
    for (const VansEngine::AudioDuckingRule& rule : asset.rules)
        ruleValues.push_back(VansEngine::VansAudioMixValueCodec::EncodeDuckingRule(rule));

    VansSerializedValue root = VansSerializedValue::Object({});
    if (!asset.guid.empty())
        SetSerializedObjectField(root, "guid", VansSerializedValue::String(asset.guid));
    SetSerializedObjectField(root, "assetKind", VansSerializedValue::String("AudioDuckingRules"));
    SetSerializedObjectField(root, "displayName", VansSerializedValue::String(asset.displayName));
    SetSerializedObjectField(root, "rules", VansSerializedValue::Array(std::move(ruleValues)));
    return root;
}
}
