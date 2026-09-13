#pragma once
#include "VansBaseWindowComponent.h"
#include <glm/mat4x4.hpp>
#include <imgui.h>

namespace VansGraphics
{
class VansParticleDebugWindow final : public VansBaseWindowComponent
{
public:
    void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& api) override;
    void DrawSceneOverlay(Vans::EditorAPI::IEngineEditorAPI& api,
        const glm::mat4& viewProjection, ImVec2 origin, ImVec2 size);
private:
    void Refresh(Vans::EditorAPI::IEngineEditorAPI& api);
    bool Includes(const Vans::EditorAPI::ParticleDebugEmitter& emitter) const;
    Vans::EditorAPI::ParticleDebugSnapshot m_Snapshot;
    std::string m_SelectedEmitter;
    int m_Frame = -1, m_Plane = 0;
    bool m_Overlay = true, m_Indices = false, m_Frozen = false;
};
}
