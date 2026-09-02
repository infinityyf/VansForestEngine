#include "VansSceneDocument.h"

#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansStagedFileTransaction.h"
#include "Storage/VansSceneFileStorage.h"
#include "VansSceneDocumentLoader.h"
#include "VansSceneSchema.h"

#include <nlohmann/json.hpp>

#include <utility>

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
    if (!m_Root)
        return VansSerializedValue::Object({});
    return *m_Root;
}

bool SceneFileFingerprint::operator==(const SceneFileFingerprint& other) const
{
    if (!valid || !other.valid)
        return valid == other.valid;
    return size == other.size && contentHash == other.contentHash && writeTime == other.writeTime;
}

VansSceneDocument::VansSceneDocument()
    : m_Root(std::make_unique<VansSerializedValue>(VansSerializedValue::Object({})))
{
}

VansSceneDocument::~VansSceneDocument() = default;

VansSerializedValue VansSceneDocument::SerializedRootSnapshot() const
{
    return *m_Root;
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
    return {
        std::make_shared<VansSerializedValue>(*m_Root),
        m_SourcePath,
        m_LoadedFingerprint,
        m_CurrentStateId
    };
}

bool VansSceneDocument::StageSave(SceneDocumentSaveStage& stage, std::string& error) const
{
    stage = {};
    error.clear();
    if (!IsDirty())
        return true;
    const SceneJson root = EncodeSerializedValueJson<SceneJson>(*m_Root);
    if (!IsHealthy() || !root.is_object() || !VansSceneSchema::ValidateSceneJson(root).empty())
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
    MarkSaved(target, fingerprint, stage.stateId != 0 ? stage.stateId : m_CurrentStateId);
    return true;
}

SceneStateId VansSceneDocument::AllocateStateId()
{
    return m_NextStateId++;
}

SceneStateId VansSceneDocument::ApplyEditedSerializedRoot(VansSerializedValue root)
{
    *m_Root = std::move(root);
    m_CurrentStateId = AllocateStateId();
    return m_CurrentStateId;
}

void VansSceneDocument::RestoreEditedSerializedRoot(VansSerializedValue root, SceneStateId stateId)
{
    *m_Root = std::move(root);
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
