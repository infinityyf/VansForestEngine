#pragma once

#include "VansSceneDocument.h"

namespace Vans
{
enum class SceneSaveError
{
    None,
    NotDirty,
    InvalidDocument,
    InvalidTarget,
    ExternalConflict,
    WriteFailed,
    FlushFailed,
    ReplaceFailed
};

struct SceneSaveResult
{
    SceneSaveError error = SceneSaveError::None;
    std::string message;
    std::filesystem::path path;
    bool wroteFile = false;

    explicit operator bool() const { return error == SceneSaveError::None || error == SceneSaveError::NotDirty; }
};

class VansSceneSaveService
{
public:
    SceneSaveResult Save(VansSceneDocument& document) const;
    SceneSaveResult SaveAs(VansSceneDocument& document, const std::filesystem::path& target,
        bool allowOverwrite = false) const;

private:
    SceneSaveResult SaveSnapshot(VansSceneDocument& document, const SceneDocumentSnapshot& snapshot,
        const std::filesystem::path& target, bool checkSourceFingerprint, bool allowOverwrite) const;
};
}
