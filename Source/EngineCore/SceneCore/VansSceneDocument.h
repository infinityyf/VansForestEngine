#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "VansSceneDiagnostics.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace Vans
{
using SceneStateId = std::uint64_t;

struct SceneFileFingerprint
{
    std::uintmax_t size = 0;
    std::uint64_t contentHash = 0;
    std::filesystem::file_time_type writeTime{};
    bool valid = false;

    bool operator==(const SceneFileFingerprint& other) const;
    bool operator!=(const SceneFileFingerprint& other) const { return !(*this == other); }
};

struct SceneDocumentSnapshot
{
    SceneDocumentSnapshot();
    SceneDocumentSnapshot(
        std::shared_ptr<const VansSerializedValue> root,
        std::filesystem::path sourcePath,
        SceneFileFingerprint sourceFingerprint,
        SceneStateId stateId);
    ~SceneDocumentSnapshot();

    SceneDocumentSnapshot(const SceneDocumentSnapshot&) = default;
    SceneDocumentSnapshot& operator=(const SceneDocumentSnapshot&) = default;
    SceneDocumentSnapshot(SceneDocumentSnapshot&&) noexcept = default;
    SceneDocumentSnapshot& operator=(SceneDocumentSnapshot&&) noexcept = default;

    VansSerializedValue SerializedRootSnapshot() const;
    bool HasRoot() const { return static_cast<bool>(m_Root); }

    std::filesystem::path sourcePath;
    SceneFileFingerprint sourceFingerprint;
    SceneStateId stateId = 0;

private:
    std::shared_ptr<const VansSerializedValue> m_Root;
};

class VansSceneEditService;
class VansSceneSaveService;
class VansSetScenePropertyCommand;
class VansRemoveScenePropertyCommand;
class VansAppendSceneEntitiesCommand;
class VansReparentSceneEntityCommand;
class VansSetSceneEntityTransformCommand;

class VansSceneDocument
{
public:
    VansSceneDocument();
    ~VansSceneDocument();

    VansSceneDocument(const VansSceneDocument&) = delete;
    VansSceneDocument& operator=(const VansSceneDocument&) = delete;

    VansSerializedValue SerializedRootSnapshot() const;
    const std::filesystem::path& SourcePath() const { return m_SourcePath; }
    const SceneDiagnostics& Diagnostics() const { return m_Diagnostics; }
    SceneStateId CurrentStateId() const { return m_CurrentStateId; }
    SceneStateId SavedStateId() const { return m_SavedStateId; }
    bool IsDirty() const { return m_CurrentStateId != m_SavedStateId; }
    bool IsHealthy() const;
    SceneDocumentSnapshot CreateSnapshot() const;

private:
    friend class VansSceneDocumentLoader;
    friend class VansSceneEditService;
    friend class VansSceneSaveService;
    friend class VansSetScenePropertyCommand;
    friend class VansRemoveScenePropertyCommand;
    friend class VansAppendSceneEntitiesCommand;
    friend class VansReparentSceneEntityCommand;
    friend class VansSetSceneEntityTransformCommand;

    SceneStateId AllocateStateId();
    SceneStateId ApplyEditedSerializedRoot(VansSerializedValue root);
    void RestoreEditedSerializedRoot(VansSerializedValue root, SceneStateId stateId);
    void MarkSaved(const std::filesystem::path& path,
        const SceneFileFingerprint& fingerprint, SceneStateId savedStateId);

    std::unique_ptr<VansSerializedValue> m_Root;
    std::filesystem::path m_SourcePath;
    SceneFileFingerprint m_LoadedFingerprint;
    SceneDiagnostics m_Diagnostics;
    SceneStateId m_CurrentStateId = 1;
    SceneStateId m_SavedStateId = 1;
    SceneStateId m_NextStateId = 2;
};
}
