#pragma once
#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
    class VansTerrainWindow : public VansBaseWindowComponent
    {
    private:
        void ShowWindow(VansVKDevice& device) override;
    };
}
