#pragma once

#include "Serialization/VansAudioMixValueCodec.h"

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <string>
#include <utility>

namespace Vans
{
struct VansAudioBusSnapshotAsset
{
    std::string guid;
    std::string displayName = "Audio Bus Snapshot";
    VansEngine::AudioBusSnapshot snapshot;
};

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
    if (!VansEngine::VansAudioMixValueCodec::DecodeSnapshot(root, parsed.snapshot, error))
        return false;

    asset = std::move(parsed);
    return true;
}

inline VansSerializedValue WriteAudioBusSnapshotAssetRoot(
    const VansAudioBusSnapshotAsset& asset)
{
    VansSerializedValue root =
        VansEngine::VansAudioMixValueCodec::EncodeSnapshot(asset.snapshot);
    if (!asset.guid.empty())
        SetSerializedObjectField(root, "guid", VansSerializedValue::String(asset.guid));
    SetSerializedObjectField(root, "assetKind", VansSerializedValue::String("AudioBusSnapshot"));
    SetSerializedObjectField(root, "displayName", VansSerializedValue::String(asset.displayName));
    return root;
}
}
