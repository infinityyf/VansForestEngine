#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"

namespace Vans::EditorAPI
{
class IEngineEditorAPI;
}

namespace Vans
{
void MergeMaterialAuthoringParameterSchema(
    VansSerializedValue& materialRoot,
    const VansSerializedValue& shaderParameters);

void MergeMaterialAuthoringSchema(
    EditorAPI::IEngineEditorAPI& api,
    VansSerializedValue& materialRoot);
}
