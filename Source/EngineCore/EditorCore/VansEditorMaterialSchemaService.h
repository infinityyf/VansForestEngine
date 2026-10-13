#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"

namespace Vans::EditorAPI
{
class IAssetAuthoringEditorAPI;
class IAssetEditorAPI;
}

namespace Vans
{
void MergeMaterialAuthoringParameterSchema(
    VansSerializedValue& materialRoot,
    const VansSerializedValue& shaderParameters);

void MergeMaterialAuthoringSchema(
    EditorAPI::IAssetAuthoringEditorAPI& api,
    VansSerializedValue& materialRoot);

// Convert display-only empty Cloth slots into source absence and reject
// invalid references/import roles before committing an Inspector edit.
bool PrepareClothMaterialAuthoringEdit(EditorAPI::IAssetEditorAPI& api,
    const VansSerializedValue& displayRoot, VansSerializedValue& sourceRoot,
    std::string& error);
}
