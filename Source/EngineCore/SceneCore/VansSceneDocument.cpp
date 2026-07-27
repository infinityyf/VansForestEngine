#include "VansSceneDocument.h"

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
