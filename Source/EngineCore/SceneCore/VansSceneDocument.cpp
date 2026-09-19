#include "VansSceneDocument.h"

#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansStagedFileTransaction.h"
#include "Storage/VansSceneFileStorage.h"
#include "VansSceneDocumentLoader.h"
#include "VansSceneSchema.h"

#include <nlohmann/json.hpp>

#include <utility>
#include <stdexcept>
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

namespace Vans
{
SceneDocumentSnapshot::SceneDocumentSnapshot() = default;

SceneDocumentSnapshot::SceneDocumentSnapshot(
    std::shared_ptr<const VansSerializedValue> root,
    std::filesystem::path sourcePath,
    SceneFileFingerprint sourceFingerprint,
    SceneStateId stateId)
    : sourcePath(std::move(sourcePath))
    , sourceFingerprint(sourceFingerprint)
    , stateId(stateId)
    , m_Root(std::move(root))
{
}

SceneDocumentSnapshot::~SceneDocumentSnapshot() = default;

VansSerializedValue SceneDocumentSnapshot::SerializedRootSnapshot() const
{
    return Root();
}

VansSerializedValue SceneDocumentSnapshot::AuthoringRootSnapshot() const
{
    return authoringRoot ? *authoringRoot : Root();
}

const VansSerializedValue& SceneDocumentSnapshot::Root() const
{
    static const VansSerializedValue emptyRoot = VansSerializedValue::Object({});
    if (!m_Root)
        return emptyRoot;
    return *m_Root;
}

bool SceneFileFingerprint::operator==(const SceneFileFingerprint& other) const
{
    if (!valid || !other.valid)
        return valid == other.valid;
    return size == other.size && contentHash == other.contentHash && writeTime == other.writeTime;
}

VansSceneDocument::VansSceneDocument()
    : m_Root(std::make_shared<const VansSerializedValue>(VansSerializedValue::Object({})))
{
}

VansSceneDocument::~VansSceneDocument() = default;

VansSerializedValue VansSceneDocument::SerializedRootSnapshot() const
{
    return m_ResolvedRoot ? *m_ResolvedRoot : *m_Root;
}

std::unique_ptr<VansSceneDocument> VansSceneDocument::CreateInMemory(VansSerializedValue root, VansPrefabLookup lookup, std::string& error)
{
    auto document = std::make_unique<VansSceneDocument>();
    document->m_Root = std::make_shared<const VansSerializedValue>(std::move(root));
    if (!document->SetPrefabLookup(std::move(lookup), error)) return {};
    document->m_Diagnostics = VansSceneSchema::ValidateSceneJson(EncodeSerializedValueJson<SceneJson>(document->SerializedRootSnapshot()));
    if (!document->IsHealthy()) { error = document->m_Diagnostics.front().message; return {}; }
    return document;
}

bool VansSceneDocument::SetPrefabLookup(VansPrefabLookup lookup, std::string& error)
{
    auto previous = m_PrefabLookup;
    m_PrefabLookup = std::move(lookup);
    if (RefreshPrefabView(error)) return true;
    m_PrefabLookup = std::move(previous);
    return false;
}

bool VansSceneDocument::RefreshPrefabView(std::string& error)
{
    error.clear();
    const auto* instances = FindObjectField(*m_Root, "prefabInstances");
    if (!instances || (instances->kind == VansSerializedValue::Kind::Array && instances->arrayItems.empty()))
    { m_ResolvedRoot.reset(); return true; }
    if (!m_PrefabLookup) { error = "Prefab asset lookup is unavailable"; return false; }
    VansSerializedValue resolved;
    if (!VansPrefabResolver::ResolveScene(*m_Root, m_PrefabLookup, resolved, error)) return false;
    m_ResolvedRoot = std::make_shared<const VansSerializedValue>(std::move(resolved));
    return true;
}

bool VansSceneDocument::IsHealthy() const
{
    for (const SceneDiagnostic& diagnostic : m_Diagnostics)
    {
        if (diagnostic.severity == SceneDiagnosticSeverity::Error)
            return false;
    }
    return m_Root && m_Root->kind == VansSerializedValue::Kind::Object;
}

SceneDocumentSnapshot VansSceneDocument::CreateSnapshot() const
{
    SceneDocumentSnapshot snapshot{
        m_ResolvedRoot ? m_ResolvedRoot : m_Root,
        m_SourcePath,
        m_LoadedFingerprint,
        m_CurrentStateId
    };
    snapshot.authoringRoot = m_Root;
    return snapshot;
}

bool VansSceneDocument::StageSave(SceneDocumentSaveStage& stage, std::string& error) const
{
    stage = {};
    error.clear();
    if (!IsDirty())
        return true;
    const SceneJson root = EncodeSerializedValueJson<SceneJson>(*m_Root);
    if (!IsHealthy() || !root.is_object() || !VansSceneSchema::ValidateSceneJson(
        EncodeSerializedValueJson<SceneJson>(SerializedRootSnapshot())).empty())
    {
        error = "Cannot save an invalid scene document";
        return false;
    }
    if (m_SourcePath.empty())
    {
        error = "Scene save target is empty";
        return false;
    }
    const std::filesystem::path target =
        std::filesystem::absolute(m_SourcePath).lexically_normal();
    const SceneFileFingerprint current = VansSceneDocumentLoader::Fingerprint(target, &error);
    if (!current.valid)
        return false;
    if (current != m_LoadedFingerprint)
    {
        error = "Scene file was modified outside the editor";
        return false;
    }
    VansStagedFile file;
    if (!VansSceneFileStorage::StageSceneDocument(target, root, file, error))
        return false;
    stage.targetPath = std::move(file.targetPath);
    stage.temporaryPath = std::move(file.temporaryPath);
    stage.stateId = m_CurrentStateId;
    return true;
}

bool VansSceneDocument::AdoptStagedSave(
    const SceneDocumentSaveStage& stage,
    std::string& error)
{
    if (!ObservePublishedSave(stage, error)) return false;
    AdoptObservedSave(stage);
    return true;
}

bool VansSceneDocument::ObservePublishedSave(const SceneDocumentSaveStage& stage, std::string& error)
{
    error.clear();
    if (stage.targetPath.empty())
        return true;
    const std::filesystem::path target =
        std::filesystem::absolute(stage.targetPath).lexically_normal();
    if (target != std::filesystem::absolute(m_SourcePath).lexically_normal())
    {
        error = "Scene document save stage target mismatch: " + target.string();
        return false;
    }
    const SceneFileFingerprint fingerprint = VansSceneDocumentLoader::Fingerprint(target, &error);
    if (!fingerprint.valid)
        return false;
    m_ObservedFingerprint = fingerprint;
    return true;
}

void VansSceneDocument::AdoptObservedSave(const SceneDocumentSaveStage& stage)
{
    if (!stage.targetPath.empty())
        MarkSaved(stage.targetPath, m_ObservedFingerprint, stage.stateId != 0 ? stage.stateId : m_CurrentStateId);
}

void VansSceneDocument::RestoreAuthoringRoot(VansSerializedValue root, SceneStateId state)
{
    auto original = m_Root;
    m_Root = std::make_shared<const VansSerializedValue>(std::move(root));
    std::string error;
    if (!RefreshPrefabView(error)) { m_Root = std::move(original); throw std::runtime_error(error); }
    m_CurrentStateId = state;
    if (state >= m_NextStateId) m_NextStateId = state + 1;
}

SceneStateId VansSceneDocument::AllocateStateId()
{
    return m_NextStateId++;
}

SceneStateId VansSceneDocument::ApplyEditedSerializedRoot(VansSerializedValue root)
{
    if (!FindObjectField(root, "prefabInstances"))
    {
        m_Root = std::make_shared<const VansSerializedValue>(std::move(root));
        m_ResolvedRoot.reset();
        m_CurrentStateId = AllocateStateId();
        return m_CurrentStateId;
    }
    VansSerializedValue authoring;
    std::string error;
    if (!VansPrefabResolver::CaptureScene(root, m_PrefabLookup, authoring, error))
        throw std::runtime_error(error);
    VansSerializedValue resolved;
    if (!VansPrefabResolver::ResolveScene(authoring, m_PrefabLookup, resolved, error))
        throw std::runtime_error(error);
    auto next = std::make_shared<const VansSerializedValue>(std::move(authoring));
    m_ResolvedRoot = std::make_shared<const VansSerializedValue>(std::move(resolved));
    m_Root = std::move(next);
    m_CurrentStateId = AllocateStateId();
    return m_CurrentStateId;
}

void VansSceneDocument::RestoreEditedSerializedRoot(VansSerializedValue root, SceneStateId stateId)
{
    if (!FindObjectField(root, "prefabInstances"))
    {
        m_Root = std::make_shared<const VansSerializedValue>(std::move(root));
        m_ResolvedRoot.reset();
        m_CurrentStateId = stateId;
        if (stateId >= m_NextStateId) m_NextStateId = stateId + 1;
        return;
    }
    VansSerializedValue authoring;
    std::string error;
    if (!VansPrefabResolver::CaptureScene(root, m_PrefabLookup, authoring, error))
        throw std::runtime_error(error);
    VansSerializedValue resolved;
    if (!VansPrefabResolver::ResolveScene(authoring, m_PrefabLookup, resolved, error))
        throw std::runtime_error(error);
    auto next = std::make_shared<const VansSerializedValue>(std::move(authoring));
    m_ResolvedRoot = std::make_shared<const VansSerializedValue>(std::move(resolved));
    m_Root = std::move(next);
    m_CurrentStateId = stateId;
    if (m_CurrentStateId >= m_NextStateId)
        m_NextStateId = m_CurrentStateId + 1;
}

void VansSceneDocument::MarkSaved(const std::filesystem::path& path,
    const SceneFileFingerprint& fingerprint, SceneStateId savedStateId)
{
    m_SourcePath = path;
    m_LoadedFingerprint = fingerprint;
    m_SavedStateId = savedStateId;
}
}
