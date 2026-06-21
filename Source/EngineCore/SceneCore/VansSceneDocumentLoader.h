#pragma once

#include "VansSceneDocument.h"

#include <memory>

namespace Vans
{
struct SceneDocumentLoadResult
{
    std::unique_ptr<VansSceneDocument> document;
    SceneDiagnostics diagnostics;

    explicit operator bool() const { return document && document->IsHealthy(); }
};

class VansSceneDocumentLoader
{
public:
    static SceneDocumentLoadResult Load(const std::filesystem::path& path);
    static SceneFileFingerprint Fingerprint(const std::filesystem::path& path, std::string* error = nullptr);
};
}
