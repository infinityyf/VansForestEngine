#pragma once
#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
    class VansPcgWindow : public VansBaseWindowComponent
    {
    private:
        void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;
    };
}
