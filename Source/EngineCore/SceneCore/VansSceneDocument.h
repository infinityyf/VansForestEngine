#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "VansSceneDiagnostics.h"
#include "Prefab/VansPrefabAsset.h"

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
    VansSerializedValue AuthoringRootSnapshot() const;
    std::shared_ptr<const VansSerializedValue> authoringRoot;
    // 引用由当前快照持有；文档编辑发布新根，不会使旧快照失效。
    const VansSerializedValue& Root() const;
    bool HasRoot() const { return static_cast<bool>(m_Root); }

    std::filesystem::path sourcePath;
    SceneFileFingerprint sourceFingerprint;
    SceneStateId stateId = 0;

private:
    std::shared_ptr<const VansSerializedValue> m_Root;
};

struct SceneDocumentSaveStage
{
    std::filesystem::path targetPath;
    std::filesystem::path temporaryPath;
    SceneStateId stateId = 0;
};

class VansSceneEditService;
class VansSceneSaveService;
class VansSetScenePropertyCommand;
class VansRemoveScenePropertyCommand;
class VansAppendSceneEntitiesCommand;
class VansReparentSceneEntityCommand;
class VansSetSceneEntityTransformCommand;
class VansReplaceSceneRootCommand;

class VansSceneDocument
{
public:
    VansSceneDocument();
    ~VansSceneDocument();

    VansSceneDocument(const VansSceneDocument&) = delete;
    VansSceneDocument& operator=(const VansSceneDocument&) = delete;

    VansSerializedValue SerializedRootSnapshot() const;
    static std::unique_ptr<VansSceneDocument> CreateInMemory(VansSerializedValue root, VansPrefabLookup lookup, std::string& error);
    VansSerializedValue AuthoringRootSnapshot() const { return *m_Root; }
    bool SetPrefabLookup(VansPrefabLookup lookup, std::string& error);
    bool RefreshPrefabView(std::string& error);
    const VansPrefabLookup& PrefabLookup() const { return m_PrefabLookup; }
    const std::filesystem::path& SourcePath() const { return m_SourcePath; }
    const SceneDiagnostics& Diagnostics() const { return m_Diagnostics; }
    SceneStateId CurrentStateId() const { return m_CurrentStateId; }
    SceneStateId SavedStateId() const { return m_SavedStateId; }
    bool IsDirty() const { return m_CurrentStateId != m_SavedStateId; }
    bool IsHealthy() const;
    SceneDocumentSnapshot CreateSnapshot() const;
    bool StageSave(SceneDocumentSaveStage& stage, std::string& error) const;
    bool AdoptStagedSave(const SceneDocumentSaveStage& stage, std::string& error);
    bool ObservePublishedSave(const SceneDocumentSaveStage& stage, std::string& error);
    void AdoptObservedSave(const SceneDocumentSaveStage& stage);

private:
    friend class VansSceneDocumentLoader;
    friend class VansSceneEditService;
    friend class VansSceneSaveService;
    friend class VansSetScenePropertyCommand;
    friend class VansRemoveScenePropertyCommand;
    friend class VansAppendSceneEntitiesCommand;
    friend class VansReparentSceneEntityCommand;
    friend class VansSetSceneEntityTransformCommand;
    friend class VansReplaceSceneRootCommand;
    friend class VansApplyPrefabCommand;

    void RestoreAuthoringRoot(VansSerializedValue root, SceneStateId state);
    SceneStateId AllocateStateId();
    SceneStateId ApplyEditedSerializedRoot(VansSerializedValue root);
    void RestoreEditedSerializedRoot(VansSerializedValue root, SceneStateId stateId);
    void MarkSaved(const std::filesystem::path& path,
        const SceneFileFingerprint& fingerprint, SceneStateId savedStateId);

    std::shared_ptr<const VansSerializedValue> m_Root;
    std::shared_ptr<const VansSerializedValue> m_ResolvedRoot;
    VansPrefabLookup m_PrefabLookup;
    std::filesystem::path m_SourcePath;
    SceneFileFingerprint m_LoadedFingerprint;
    SceneFileFingerprint m_ObservedFingerprint;
    SceneDiagnostics m_Diagnostics;
    SceneStateId m_CurrentStateId = 1;
    SceneStateId m_SavedStateId = 1;
    SceneStateId m_NextStateId = 2;
};
}
