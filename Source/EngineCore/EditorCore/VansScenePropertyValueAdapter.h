#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../EngineAPILayer/Public/EngineDTOs.h"

namespace Vans
{
VansSerializedValue ToSerializedValue(const EditorAPI::ScenePropertyValue& value);
EditorAPI::ScenePropertyValue FromSerializedValue(const VansSerializedValue& value);
}
