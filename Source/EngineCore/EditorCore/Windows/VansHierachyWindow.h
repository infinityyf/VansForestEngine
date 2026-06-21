#pragma once

#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
class VansHierachuWindow final : public VansBaseWindowComponent
{
public:
    // Consumed by the optional Scene view motion-matching overlay.
    inline static bool m_ShowMMViz = false;

private:
    void ShowWindow(VansVKDevice& device) override;
};
}
