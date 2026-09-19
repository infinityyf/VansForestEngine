#pragma once

#include "VansBaseWindowComponent.h"
#include <cstdint>

namespace VansGraphics
{
class VansHierachuWindow final : public VansBaseWindowComponent
{
public:
    void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;
private:
    std::uint64_t m_SelectionRevision = 0;
};
}
