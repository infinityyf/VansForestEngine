#pragma once

#include "VansSceneDiagnostics.h"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Vans
{
using SceneJson = nlohmann::ordered_json;
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
    SceneJson root;
    std::filesystem::path sourcePath;
    SceneFileFingerprint sourceFingerprint;
    SceneStateId stateId = 0;
};

class VansSceneEditService;
class VansSceneSaveService;
class VansSetScenePropertyCommand;
class VansRemoveScenePropertyCommand;

class VansSceneDocument
{
public:
    const SceneJson& Root() const { return m_Root; }
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

    SceneStateId AllocateStateId();
    void MarkSaved(const std::filesystem::path& path,
        const SceneFileFingerprint& fingerprint, SceneStateId savedStateId);

    SceneJson m_Root;
    std::filesystem::path m_SourcePath;
    SceneFileFingerprint m_LoadedFingerprint;
    SceneDiagnostics m_Diagnostics;
    SceneStateId m_CurrentStateId = 1;
    SceneStateId m_SavedStateId = 1;
    SceneStateId m_NextStateId = 2;
};
}
