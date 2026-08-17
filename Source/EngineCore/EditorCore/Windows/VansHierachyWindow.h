#pragma once

#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
class VansHierachuWindow final : public VansBaseWindowComponent
{
public:
    void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;
};
}
