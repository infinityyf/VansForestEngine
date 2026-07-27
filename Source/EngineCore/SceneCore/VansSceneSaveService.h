#pragma once

#include <filesystem>
#include <string>

namespace Vans
{
class VansSceneDocument;
struct SceneDocumentSnapshot;

enum class SceneSaveError
{
    None,
    NotDirty,
    InvalidDocument,
    InvalidTarget,
    ExternalConflict,
    WriteFailed,
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
