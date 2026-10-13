#pragma once

#include "VansBaseWindowComponent.h"
#include <cstdint>
#include <memory>

namespace VansGraphics
{
class VansHierachuWindow final : public VansBaseWindowComponent
{
public:
    VansHierachuWindow();
    ~VansHierachuWindow() override;
    void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;
private:
    struct HierarchyIndex;
    std::unique_ptr<HierarchyIndex> m_Hierarchy;
    std::uint64_t m_SelectionRevision = 0;
};
}
