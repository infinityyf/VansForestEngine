#pragma once

#include "VansBaseWindowComponent.h"

#include <memory>

namespace VansGraphics
{
class VansInspectorWindow final : public VansBaseWindowComponent
{
public:
    VansInspectorWindow();
    ~VansInspectorWindow() override;

private:
    void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
}
