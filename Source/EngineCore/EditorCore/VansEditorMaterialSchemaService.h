#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"

namespace Vans::EditorAPI
{
class IAssetAuthoringEditorAPI;
}

namespace Vans
{
void MergeMaterialAuthoringParameterSchema(
    VansSerializedValue& materialRoot,
    const VansSerializedValue& shaderParameters);

void MergeMaterialAuthoringSchema(
    EditorAPI::IAssetAuthoringEditorAPI& api,
    VansSerializedValue& materialRoot);
}
