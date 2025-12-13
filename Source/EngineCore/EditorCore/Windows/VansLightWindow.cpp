#include "VansLightWindow.h"
#include "../../RenderCore/VansScene.h"

#include "imgui.h"

void VansGraphics::VansLightWindow::ShowWindow(VansVKDevice& device)
{
    //绘制menu bar
    ImGui::Begin("Light Info");   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
    
    for (auto& node : m_Scene->GetLightManager()->GetDirectionLights())
    {
        //绘制直接光信息
        float direction[3] = { node.m_Direction.x, node.m_Direction.y, node.m_Direction.z };
        ImGui::SliderFloat3("direct light direction", direction, -1,1);
        node.m_Direction = glm::vec3(direction[0], direction[1], direction[2]);
    }

    for (auto& node : m_Scene->GetLightManager()->GetPointLights())
    {
        //绘制直接光信息
    }

    for (auto& node : m_Scene->GetLightManager()->GetSpotLight())
    {
        //绘制直接光信息
    }

    ImGui::End();
}
