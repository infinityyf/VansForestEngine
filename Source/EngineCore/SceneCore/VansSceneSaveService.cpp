#include "VansSceneSaveService.h"
#include "VansSceneDocumentLoader.h"
#include "VansSceneSchemaV2.h"

#include <chrono>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace Vans
{
namespace
{
std::filesystem::path MakeTemporaryPath(const std::filesystem::path& target)
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return target.parent_path() / (target.filename().string() + ".tmp." + std::to_string(nonce));
}

bool FlushFile(const std::filesystem::path& path)
{
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    const bool flushed = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    return flushed;
#else
    return true;
#endif
}

bool AtomicReplace(const std::filesystem::path& temporary, const std::filesystem::path& target)
{
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), target.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    return !ec;
#endif
}
}

SceneSaveResult VansSceneSaveService::Save(VansSceneDocument& document) const
{
    if (!document.IsDirty())
        return { SceneSaveError::NotDirty, "Scene has no changes", document.SourcePath(), false };
    return SaveSnapshot(document, document.CreateSnapshot(), document.SourcePath(), true, true);
}

SceneSaveResult VansSceneSaveService::SaveAs(VansSceneDocument& document,
    const std::filesystem::path& target, bool allowOverwrite) const
{
    return SaveSnapshot(document, document.CreateSnapshot(), target, false, allowOverwrite);
}

SceneSaveResult VansSceneSaveService::SaveSnapshot(VansSceneDocument& document,
    const SceneDocumentSnapshot& snapshot, const std::filesystem::path& rawTarget,
    bool checkSourceFingerprint, bool allowOverwrite) const
{
    if (!document.IsHealthy() || !snapshot.root.is_object() || !VansSceneSchemaV2::Validate(snapshot.root).empty())
        return { SceneSaveError::InvalidDocument, "Cannot save an invalid scene document", rawTarget, false };
    if (rawTarget.empty())
        return { SceneSaveError::InvalidTarget, "Scene save target is empty", rawTarget, false };

    const std::filesystem::path target = std::filesystem::absolute(rawTarget).lexically_normal();
    std::error_code ec;
    const bool targetExists = std::filesystem::exists(target, ec);
    if (ec)
        return { SceneSaveError::InvalidTarget, ec.message(), target, false };
    if (targetExists && !allowOverwrite)
        return { SceneSaveError::InvalidTarget, "Save As target already exists", target, false };

    if (checkSourceFingerprint)
    {
        std::string error;
        const SceneFileFingerprint current = VansSceneDocumentLoader::Fingerprint(target, &error);
        if (!current.valid)
            return { SceneSaveError::ExternalConflict, error, target, false };
        if (current != snapshot.sourceFingerprint)
            return { SceneSaveError::ExternalConflict, "Scene file was modified outside the editor", target, false };
    }

    const std::filesystem::path temporary = MakeTemporaryPath(target);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return { SceneSaveError::WriteFailed, "Cannot create temporary scene file", target, false };
        const std::string serialized = snapshot.root.dump(4) + "\n";
        output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        output.flush();
        if (!output)
        {
            output.close();
            std::filesystem::remove(temporary, ec);
            return { SceneSaveError::WriteFailed, "Failed writing temporary scene file", target, false };
        }
    }

    try
    {
        std::ifstream verification(temporary, std::ios::binary);
        const SceneJson verified = SceneJson::parse(verification);
        if (!verification || verified != snapshot.root)
        {
            std::filesystem::remove(temporary, ec);
            return { SceneSaveError::WriteFailed, "Temporary scene verification failed", target, false };
        }
    }
    catch (const SceneJson::exception& error)
    {
        std::filesystem::remove(temporary, ec);
        return { SceneSaveError::WriteFailed, error.what(), target, false };
    }

    if (!FlushFile(temporary))
    {
        std::filesystem::remove(temporary, ec);
        return { SceneSaveError::FlushFailed, "Failed flushing temporary scene file", target, false };
    }
    if (!AtomicReplace(temporary, target))
    {
        std::filesystem::remove(temporary, ec);
        return { SceneSaveError::ReplaceFailed, "Failed atomically replacing scene file", target, false };
    }

    std::string fingerprintError;
    const SceneFileFingerprint savedFingerprint = VansSceneDocumentLoader::Fingerprint(target, &fingerprintError);
    if (!savedFingerprint.valid)
        return { SceneSaveError::ReplaceFailed, fingerprintError, target, true };

    document.MarkSaved(target, savedFingerprint, snapshot.stateId);
    return { SceneSaveError::None, "Scene saved", target, true };
}
}
