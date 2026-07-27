#pragma once

#include "Serialization/VansSerializedValue.h"

#include <filesystem>
#include <cstdint>
#include <memory>
#include <string>

namespace Vans
{
using VansAssetDocumentStateId = std::uint64_t;

namespace EditorInternal
{
class SetAssetPropertyCommand;
}

struct VansAssetFileFingerprint
{
    bool exists = false;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type lastWriteTime{};
    std::uint64_t contentHash = 0;

    friend bool operator==(const VansAssetFileFingerprint& left, const VansAssetFileFingerprint& right)
    {
        return left.exists == right.exists &&
            left.size == right.size &&
            left.lastWriteTime == right.lastWriteTime &&
            left.contentHash == right.contentHash;
    }

    friend bool operator!=(const VansAssetFileFingerprint& left, const VansAssetFileFingerprint& right)
    {
        return !(left == right);
    }
};

struct VansAssetDocumentSaveStage
{
    std::filesystem::path targetPath;
    std::filesystem::path temporaryPath;
    VansAssetDocumentStateId stateId = 0;
};

class VansAssetDocument
{
    friend class EditorInternal::SetAssetPropertyCommand;

public:
    VansAssetDocument();
    ~VansAssetDocument();

    VansAssetDocument(const VansAssetDocument&) = delete;
    VansAssetDocument& operator=(const VansAssetDocument&) = delete;

    bool Load(const std::filesystem::path& path, std::string& error);
    bool StageSave(VansAssetDocumentSaveStage& stage, std::string& error) const;
    bool AdoptStagedSave(const VansAssetDocumentSaveStage& stage, std::string& error);

    const std::filesystem::path& Path() const { return m_Path; }
    VansSerializedValue SerializedRootSnapshot() const;
    bool IsLoaded() const { return m_Loaded; }
    VansAssetDocumentStateId CurrentStateId() const { return m_CurrentStateId; }
    VansAssetDocumentStateId SavedStateId() const { return m_SavedStateId; }
    bool IsDirty() const { return m_CurrentStateId != m_SavedStateId; }
    void Reset();

    static VansAssetFileFingerprint Fingerprint(const std::filesystem::path& path, std::string& error);

private:
    std::filesystem::path m_Path;
    std::unique_ptr<VansSerializedValue> m_Root;
    VansAssetFileFingerprint m_LoadedFingerprint;
    bool m_Loaded = false;
    VansAssetDocumentStateId m_CurrentStateId = 1;
    VansAssetDocumentStateId m_SavedStateId = 1;
    VansAssetDocumentStateId m_NextStateId = 2;

    VansAssetDocumentStateId AllocateStateId();
    VansAssetDocumentStateId ApplyEditedSerializedRoot(VansSerializedValue root);
    void RestoreEditedSerializedRoot(VansSerializedValue root, VansAssetDocumentStateId stateId);
};
}
