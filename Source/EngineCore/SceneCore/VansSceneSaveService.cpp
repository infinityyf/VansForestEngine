#include "VansSceneSaveService.h"
#include "VansSceneDocument.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansStagedFileTransaction.h"
#include "../AssetCore/Storage/VansFileStorage.h"
#include "Storage/VansSceneFileStorage.h"
#include "VansSceneDocumentLoader.h"
#include "VansSceneSchema.h"

#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

namespace Vans
{
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
	VansScopedIOContext ioContext(
		VansIODomain::Authoring, "SceneDocument.Save", true);
    const SceneJson snapshotRoot =
        EncodeSerializedValueJson<SceneJson>(snapshot.SerializedRootSnapshot());
    if (!document.IsHealthy() || !snapshotRoot.is_object() ||
        !VansSceneSchema::ValidateSceneJson(snapshotRoot).empty())
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

    VansStagedFile stage;
    std::string stageError;
    if (!VansSceneFileStorage::StageSceneDocument(target, snapshotRoot, stage, stageError))
    {
        return { SceneSaveError::WriteFailed, stageError, target, false };
    }

    VansStagedFileTransaction transaction;
    transaction.Add(std::move(stage));
    std::string publishError;
    if (!transaction.Publish(publishError))
    {
        return { SceneSaveError::ReplaceFailed, publishError, target, false };
    }

    std::string fingerprintError;
    const SceneFileFingerprint savedFingerprint = VansSceneDocumentLoader::Fingerprint(target, &fingerprintError);
    if (!savedFingerprint.valid)
        return { SceneSaveError::ReplaceFailed, fingerprintError, target, true };

    document.MarkSaved(target, savedFingerprint, snapshot.stateId);
    return { SceneSaveError::None, "Scene saved", target, true };
}
}
