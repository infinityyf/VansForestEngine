#include "VansAssetDocument.h"

#include "Serialization/VansSerializedValueJsonAdapter.h"
#include "Serialization/VansJsonDocumentCodec.h"
#include "Storage/VansFileStorage.h"
#include "Storage/VansJsonFileStorage.h"
#include "VansAssetDocumentJson.h"

#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

namespace Vans
{
namespace
{
VansAssetFileFingerprint FingerprintLoadedAssetBytes(
    const std::filesystem::path& path,
    const std::string& bytes,
    std::string& error)
{
    VansAssetFileFingerprint fingerprint;
    std::error_code ec;
    fingerprint.lastWriteTime = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        error = ec.message();
        return fingerprint;
    }
    fingerprint.exists = true;
    fingerprint.size = bytes.size();
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char value : bytes)
    {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    fingerprint.contentHash = hash;
    return fingerprint;
}
}

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
	VansScopedIOContext ioContext(
		VansIODomain::Authoring, "AssetDocument.Fingerprint", false);
    VansAssetFileFingerprint fingerprint;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return fingerprint;
    if (ec)
    {
        error = ec.message();
        return fingerprint;
    }

    std::string bytes;
    if (!VansFileStorage::ReadAllBytes(path, bytes, error))
        return {};
	return FingerprintLoadedAssetBytes(path, bytes, error);
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
	VansScopedIOContext ioContext(
		VansIODomain::Authoring, "AssetDocument.Load", false);
    Reset();
    try
    {
		std::string bytes;
		if (!VansFileStorage::ReadAllBytes(path, bytes, error))
			return false;
        AssetDocumentJson root;
		if (!VansJsonDocumentCodec::Parse(bytes, root, error))
            return false;
        if (!root.is_object())
        {
            error = "Asset document root must be an object: " + path.string();
            return false;
        }
        *m_Root = DecodeSerializedValueJson(root);
        m_Path = std::filesystem::absolute(path).lexically_normal();
		m_LoadedFingerprint = FingerprintLoadedAssetBytes(m_Path, bytes, error);
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

bool VansAssetDocument::InitializeNew(
	const std::filesystem::path& path,
	VansSerializedValue root,
	std::string& error)
{
	error.clear();
	Reset();
	if (path.empty() || root.kind != VansSerializedValue::Kind::Object)
	{
		error = "New asset document requires an object root and a target path";
		return false;
	}
	m_Path = std::filesystem::absolute(path).lexically_normal();
	*m_Root = std::move(root);
	m_LoadedFingerprint = {};
	m_Loaded = true;
	m_CurrentStateId = 2;
	m_SavedStateId = 1;
	m_NextStateId = 3;
	return true;
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
	VansScopedIOContext ioContext(
		VansIODomain::Authoring, "AssetDocument.StageSave", true);
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

    AssetDocumentJson root = EncodeSerializedValueJson<AssetDocumentJson>(*m_Root);
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
	if (!ObservePublishedSave(stage, error))
		return false;
	AdoptObservedSave(stage);
	return true;
}

bool VansAssetDocument::ObservePublishedSave(
	const VansAssetDocumentSaveStage& stage,
	std::string& error)
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
    return true;
}

void VansAssetDocument::AdoptObservedSave(const VansAssetDocumentSaveStage& stage)
{
	if (!m_Loaded) return;
	m_SavedStateId = stage.stateId != 0 ? stage.stateId : m_CurrentStateId;
}
}
