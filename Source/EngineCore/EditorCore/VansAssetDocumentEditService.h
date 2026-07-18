#pragma once

#include "../AssetCore/VansAssetDocument.h"

#include <string>

namespace Vans
{
struct AssetDocumentEditResult
{
    bool success = false;
    std::string message;

    explicit operator bool() const { return success; }
};

class VansAssetDocumentEditService
{
public:
    static AssetDocumentEditResult SetAssetReference(
        VansAssetDocument& document,
        const std::string& jsonPointer,
        const std::string& assetGuid,
        bool writeObjectReference);

    static bool CanUndo(const VansAssetDocument& document);
    static bool CanRedo(const VansAssetDocument& document);
    static AssetDocumentEditResult Undo(VansAssetDocument& document);
    static AssetDocumentEditResult Redo(VansAssetDocument& document);
    static void ClearHistory(const VansAssetDocument& document);
    static void ClearAllHistories();
};
}
