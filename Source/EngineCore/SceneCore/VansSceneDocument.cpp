#include "VansSceneDocument.h"

namespace Vans
{
bool SceneFileFingerprint::operator==(const SceneFileFingerprint& other) const
{
    if (!valid || !other.valid)
        return valid == other.valid;
    return size == other.size && contentHash == other.contentHash && writeTime == other.writeTime;
}

bool VansSceneDocument::IsHealthy() const
{
    for (const SceneDiagnostic& diagnostic : m_Diagnostics)
    {
        if (diagnostic.severity == SceneDiagnosticSeverity::Error)
            return false;
    }
    return m_Root.is_object();
}

SceneDocumentSnapshot VansSceneDocument::CreateSnapshot() const
{
    return { m_Root, m_SourcePath, m_LoadedFingerprint, m_CurrentStateId };
}

SceneStateId VansSceneDocument::AllocateStateId()
{
    return m_NextStateId++;
}

void VansSceneDocument::MarkSaved(const std::filesystem::path& path,
    const SceneFileFingerprint& fingerprint, SceneStateId savedStateId)
{
    m_SourcePath = path;
    m_LoadedFingerprint = fingerprint;
    m_SavedStateId = savedStateId;
}
}
