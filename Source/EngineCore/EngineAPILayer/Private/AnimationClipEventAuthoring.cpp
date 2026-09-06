#include "../Public/AnimationClipEventAuthoring.h"
#include "../../AnimationCore/VansAnimationClip.h"
#include "../../AssetCore/VansAssetDocument.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RuntimeCore/VansStableIdentity.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace Vans::EditorAPI
{
struct AnimationClipEventDocumentState
{
    VansGraphics::VansAnimationClipAsset asset;
    VansAssetFileFingerprint fingerprint;
};

bool OpenAnimationClipEvents(const std::string& assetGuid, AnimationClipEventDocument& document, std::string& error)
{
    error.clear();
    VansAssetGuid guid;
    auto& project = VansProjectManager::Get();
    auto* database = project.GetAssetDatabase();
    if (!database || !VansAssetGuid::TryParse(assetGuid, guid)) { error = "Animation Clip asset GUID is invalid"; return false; }
    const auto record = database->Find(guid);
    if (!record || record->type != VansAssetType::AnimationClip) { error = "Animation Clip is not registered"; return false; }
    VansScopedIOContext io(VansIODomain::Authoring, "AnimationClipEvents.Open");
    auto state = std::make_shared<AnimationClipEventDocumentState>();
    const auto path = record->authoringPath.empty() ? record->sourcePath : record->authoringPath;
    state->fingerprint = VansAssetDocument::Fingerprint(path, error);
    std::string bytes;
    if (!error.empty() || !VansFileStorage::ReadAllBytes(path, bytes, error)
        || !VansGraphics::VansAnimationClipBinaryCodec::Decode(bytes, state->asset.clip, state->asset.skeleton, error)) return false;
    if (state->fingerprint != VansAssetDocument::Fingerprint(path, error)) { error = "Clip changed while opening"; return false; }
    AnimationClipEventDocument result;
    result.assetGuid = assetGuid;
    result.path = path.string();
    result.clipName = state->asset.clip.clipName;
    result.duration = state->asset.clip.duration;
    result.state = state;
    for (const auto& event : state->asset.clip.events)
    {
        nlohmann::json payload;
        std::visit([&](const auto& value)
        {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) payload = nullptr;
            else if constexpr (std::is_same_v<T, glm::vec3>) payload = {value.x, value.y, value.z};
            else payload = value;
        }, event.payload);
        result.events.push_back({event.id, event.time, event.name, payload.dump()});
    }
    document = std::move(result);
    return true;
}

bool SaveAnimationClipEvents(AnimationClipEventDocument& document, std::string& error)
{
    error.clear();
    if (!document.state) { error = "Open an Animation Clip event document first"; return false; }
    auto asset = std::make_shared<VansGraphics::VansAnimationClipAsset>(document.state->asset);
    asset->clip.events.clear();
    try
    {
        for (const auto& item : document.events)
        {
            if (item.name.empty() || !std::isfinite(item.time) || item.time < 0 || item.time > asset->clip.duration)
                throw std::runtime_error("Clip event requires a name and a time inside the Clip");
            VansGraphics::AnimationClipEvent event;
            event.id = item.id ? item.id : VansStableHash64(item.name + "@" + std::to_string(item.time));
            event.name = item.name;
            event.time = item.time;
            const auto payload = nlohmann::json::parse(item.payloadJson);
            if (payload.is_null()) event.payload = std::monostate{};
            else if (payload.is_boolean()) event.payload = payload.get<bool>();
            else if (payload.is_number_integer()) event.payload = payload.get<std::int64_t>();
            else if (payload.is_number()) event.payload = payload.get<double>();
            else if (payload.is_string()) event.payload = payload.get<std::string>();
            else if (payload.is_array() && payload.size() == 3 && payload[0].is_number() && payload[1].is_number() && payload[2].is_number())
                event.payload = glm::vec3(payload[0].get<float>(), payload[1].get<float>(), payload[2].get<float>());
            else throw std::runtime_error("Clip event payload must be a JSON scalar or a three-number vector");
            asset->clip.events.push_back(std::move(event));
        }
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    std::stable_sort(asset->clip.events.begin(), asset->clip.events.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    std::string bytes;
    if (!VansGraphics::VansAnimationClipBinaryCodec::Encode(asset->clip, asset->skeleton, bytes, error)) return false;
    VansScopedIOContext io(VansIODomain::Authoring, "AnimationClipEvents.Save", true);
    if (document.state->fingerprint != VansAssetDocument::Fingerprint(document.path, error))
    { error = "Clip changed on disk; reopen before saving events"; return false; }
    VansStagedFile stage;
    if (!VansFileStorage::StageWriteBytes(document.path, bytes, stage, error)) return false;
    VansStagedFileTransaction transaction;
    transaction.Add(stage);
    if (document.state->fingerprint != VansAssetDocument::Fingerprint(document.path, error))
    { error = "Clip changed while staging event edits"; return false; }
    if (!transaction.Publish(error)) return false;
    auto& project = VansProjectManager::Get();
    auto* database = project.GetAssetDatabase();
    if (!database || !database->RegisterOrRefresh(document.path, VansAssetOperationPolicy::ReadOnly(), error)) return false;
    VansAssetGuid guid;
    VansAssetGuid::TryParse(document.assetGuid, guid);
    VansAssetObjectSnapshotInfo previous;
    project.GetAssetObjectRepository().FindInfo(guid, previous);
    if (!project.GetAssetObjectRepository().Publish<VansGraphics::VansAnimationClipAsset>(guid,
        VansAssetType::AnimationClip, VansStableHash64(bytes), asset, previous.dependencies, error).IsValid()) return false;
    return OpenAnimationClipEvents(document.assetGuid, document, error);
}
}
