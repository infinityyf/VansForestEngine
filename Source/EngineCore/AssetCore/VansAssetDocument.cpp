#include "VansAssetDocument.h"

#include "Serialization/VansSerializedValueLegacyJsonAdapter.h"
#include "Storage/VansFileStorage.h"
#include "Storage/VansJsonFileStorage.h"
#include "VansAssetDocumentJson.h"

#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

namespace Vans
{
VansAssetDocument::VansAssetDocument()
    : m_Root(std::make_unique<VansSerializedValue>(VansSerializedValue::Object({})))
{
}

VansAssetDocument::~VansAssetDocument() = default;

VansSerializedValue VansAssetDocument::SerializedRootSnapshot() const
{
    return *m_Root;
}

VansAssetFileFingerprint VansAssetDocument::Fingerprint(const std::filesystem::path& path, std::string& error)
{
    VansAssetFileFingerprint fingerprint;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return fingerprint;
    if (ec)
    {
        error = ec.message();
        return fingerprint;
    }

    fingerprint.exists = true;
    fingerprint.size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        error = ec.message();
        return {};
    }
    fingerprint.lastWriteTime = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        error = ec.message();
        return {};
    }

    std::string bytes;
    if (!VansFileStorage::ReadAllBytes(path, bytes, error))
        return {};

    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char value : bytes)
    {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    fingerprint.contentHash = hash;
    return fingerprint;
}

void VansAssetDocument::Reset()
{
    m_Path.clear();
    *m_Root = VansSerializedValue::Object({});
    m_LoadedFingerprint = {};
    m_Loaded = false;
    m_CurrentStateId = 1;
    m_SavedStateId = 1;
    m_NextStateId = 2;
}

bool VansAssetDocument::Load(const std::filesystem::path& path, std::string& error)
{
    Reset();
    try
    {
        AssetDocumentJson root;
        if (!VansJsonFileStorage::Read(path, root, error))
            return false;
        if (!root.is_object())
        {
            error = "Asset document root must be an object: " + path.string();
            return false;
        }
        *m_Root = DecodeSerializedValueLegacyJson(root);
        m_Path = std::filesystem::absolute(path).lexically_normal();
        m_LoadedFingerprint = Fingerprint(m_Path, error);
        if (!m_LoadedFingerprint.exists)
            return false;
        m_Loaded = true;
        m_CurrentStateId = 1;
        m_SavedStateId = 1;
        m_NextStateId = 2;
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

VansAssetDocumentStateId VansAssetDocument::AllocateStateId()
{
    return m_NextStateId++;
}

VansAssetDocumentStateId VansAssetDocument::ApplyEditedSerializedRoot(VansSerializedValue root)
{
    if (!m_Loaded)
        return m_CurrentStateId;
    *m_Root = std::move(root);
    m_CurrentStateId = AllocateStateId();
    return m_CurrentStateId;
}

void VansAssetDocument::RestoreEditedSerializedRoot(VansSerializedValue root, VansAssetDocumentStateId stateId)
{
    if (!m_Loaded)
        return;
    *m_Root = std::move(root);
    m_CurrentStateId = stateId;
    if (m_CurrentStateId >= m_NextStateId)
        m_NextStateId = m_CurrentStateId + 1;
}

bool VansAssetDocument::StageSave(VansAssetDocumentSaveStage& stage, std::string& error) const
{
    stage = {};
    if (!m_Loaded || !IsDirty()) return true;
    const VansAssetFileFingerprint currentFingerprint = Fingerprint(m_Path, error);
    if (!error.empty())
        return false;
    if (currentFingerprint != m_LoadedFingerprint)
    {
        error = "Asset document changed on disk: " + m_Path.string();
        return false;
    }

    stage.targetPath = m_Path;
    stage.stateId = m_CurrentStateId;

    AssetDocumentJson root = EncodeSerializedValueLegacyJson<AssetDocumentJson>(*m_Root);
    VansStagedFile fileStage;
    if (!VansJsonFileStorage::StageWrite(m_Path, root, fileStage, error))
    {
        stage = {};
        return false;
    }
    stage.temporaryPath = std::move(fileStage.temporaryPath);
    return true;
}

bool VansAssetDocument::AdoptStagedSave(const VansAssetDocumentSaveStage& stage, std::string& error)
{
    if (!m_Loaded)
        return true;
    if (!stage.targetPath.empty() && stage.targetPath != m_Path)
    {
        error = "Asset document save stage target mismatch: " + stage.targetPath.string();
        return false;
    }
    m_LoadedFingerprint = Fingerprint(m_Path, error);
    if (!error.empty())
        return false;
    if (!m_LoadedFingerprint.exists)
    {
        error = "Saved asset document is missing: " + m_Path.string();
        return false;
    }
    m_SavedStateId = stage.stateId != 0 ? stage.stateId : m_CurrentStateId;
    return true;
}
}
