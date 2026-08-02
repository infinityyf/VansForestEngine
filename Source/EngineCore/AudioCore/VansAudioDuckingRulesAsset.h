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
struct VansAudioDuckingRulesAsset
{
    std::string guid;
    std::string displayName = "Audio Ducking Rules";
    std::vector<VansEngine::AudioDuckingRule> rules;
};

inline float ReadAudioDuckingRuleFloat(
    const VansSerializedValue& object,
    const char* key,
    float fallback)
{
    const VansSerializedValue* field = FindObjectField(object, key);
    return field
        ? static_cast<float>(ReadSerializedNumber(*field, fallback))
        : fallback;
}

inline bool ReadAudioDuckingRuleBool(
    const VansSerializedValue& object,
    const char* key,
    bool fallback)
{
    const VansSerializedValue* field = FindObjectField(object, key);
    return field ? ReadSerializedBool(*field, fallback) : fallback;
}

inline bool TryReadAudioDuckingRule(
    const VansSerializedValue& value,
    VansEngine::AudioDuckingRule& rule)
{
    if (value.kind != VansSerializedValue::Kind::Object)
        return false;

    rule.triggerBusName = ReadSerializedStringField(value, "trigger");
    if (rule.triggerBusName.empty())
        rule.triggerBusName = ReadSerializedStringField(value, "triggerBus");
    if (rule.triggerBusName.empty())
        rule.triggerBusName = ReadSerializedStringField(value, "trigger_bus");

    rule.targetBusName = ReadSerializedStringField(value, "target");
    if (rule.targetBusName.empty())
        rule.targetBusName = ReadSerializedStringField(value, "targetBus");
    if (rule.targetBusName.empty())
        rule.targetBusName = ReadSerializedStringField(value, "target_bus");

    rule.targetGain = ReadAudioDuckingRuleFloat(value, "gain", rule.targetGain);
    rule.targetGain = ReadAudioDuckingRuleFloat(value, "targetGain", rule.targetGain);
    rule.targetGain = ReadAudioDuckingRuleFloat(value, "target_gain", rule.targetGain);
    rule.attackSeconds = ReadAudioDuckingRuleFloat(value, "attack", rule.attackSeconds);
    rule.attackSeconds = ReadAudioDuckingRuleFloat(value, "attackSeconds", rule.attackSeconds);
    rule.attackSeconds = ReadAudioDuckingRuleFloat(value, "attack_seconds", rule.attackSeconds);
    rule.releaseSeconds = ReadAudioDuckingRuleFloat(value, "release", rule.releaseSeconds);
    rule.releaseSeconds = ReadAudioDuckingRuleFloat(value, "releaseSeconds", rule.releaseSeconds);
    rule.releaseSeconds = ReadAudioDuckingRuleFloat(value, "release_seconds", rule.releaseSeconds);
    rule.enabled = ReadAudioDuckingRuleBool(value, "enabled", rule.enabled);
    rule.Normalize();
    return !rule.triggerBusName.empty() &&
        !rule.targetBusName.empty() &&
        rule.triggerBusName != rule.targetBusName;
}

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
        if (TryReadAudioDuckingRule(item, rule))
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

inline VansSerializedValue WriteAudioDuckingRule(
    VansEngine::AudioDuckingRule rule)
{
    rule.Normalize();
    return VansSerializedValue::Object({
        { "trigger", VansSerializedValue::String(rule.triggerBusName) },
        { "target", VansSerializedValue::String(rule.targetBusName) },
        { "gain", VansSerializedValue::Float(rule.targetGain) },
        { "attack", VansSerializedValue::Float(rule.attackSeconds) },
        { "release", VansSerializedValue::Float(rule.releaseSeconds) },
        { "enabled", VansSerializedValue::Bool(rule.enabled) }
    });
}

inline VansSerializedValue WriteAudioDuckingRulesAssetRoot(
    const VansAudioDuckingRulesAsset& asset)
{
    std::vector<VansSerializedValue> ruleValues;
    ruleValues.reserve(asset.rules.size());
    for (const VansEngine::AudioDuckingRule& rule : asset.rules)
        ruleValues.push_back(WriteAudioDuckingRule(rule));

    VansSerializedValue root = VansSerializedValue::Object({});
    if (!asset.guid.empty())
        SetSerializedObjectField(root, "guid", VansSerializedValue::String(asset.guid));
    SetSerializedObjectField(root, "assetKind", VansSerializedValue::String("AudioDuckingRules"));
    SetSerializedObjectField(root, "displayName", VansSerializedValue::String(asset.displayName));
    SetSerializedObjectField(root, "rules", VansSerializedValue::Array(std::move(ruleValues)));
    return root;
}
}
