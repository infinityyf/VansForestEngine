#include "VansParticleDebugWindow.h"
#include "../VansEditorWindow.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace VansGraphics
{
namespace
{
using namespace Vans::EditorAPI;
constexpr ImU32 RootColor = IM_COL32(255, 160, 70, 255);
constexpr ImU32 NodeColor = IM_COL32(100, 220, 245, 255);
constexpr ImU32 LineColor = IM_COL32(100, 190, 210, 180);
bool Finite(const Vec3& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}
std::string Key(const ParticleDebugEmitter& emitter)
{
    return std::to_string(emitter.instanceIndex) + ":" + std::to_string(emitter.instanceGeneration)
        + "/" + std::to_string(emitter.emitterIndex);
}
std::string Label(const ParticleDebugEmitter& emitter)
{
    return emitter.effectName + " / " + emitter.emitterName + " [" + Key(emitter) + "]";
}
void DrawNode(ImDrawList* draw, ImVec2 position, bool root, std::size_t index, bool indices)
{
    draw->AddCircleFilled(position, root ? 4.5f : 3.0f, root ? RootColor : NodeColor, 10);
    if (indices)
    {
        char label[32];
        std::snprintf(label, sizeof(label), root ? "R" : "%zu", index);
        draw->AddText(ImVec2(position.x + 5, position.y - 7), NodeColor, label);
    }
}
}
void VansParticleDebugWindow::Refresh(Vans::EditorAPI::IEngineEditorAPI& api)
{
    if (m_Frame == ImGui::GetFrameCount()) return;
    m_Frame = ImGui::GetFrameCount();
    auto current = api.GetParticleDebugSnapshot();
    if (!current.available || current.sceneGeneration != m_Snapshot.sceneGeneration)
    {
        m_Frozen = false;
        m_SelectedEmitter.clear();
    }
    if (!m_Frozen) m_Snapshot = std::move(current);
    if (!m_SelectedEmitter.empty() && std::none_of(m_Snapshot.emitters.begin(), m_Snapshot.emitters.end(),
        [&](const auto& emitter) { return Key(emitter) == m_SelectedEmitter; })) m_SelectedEmitter.clear();
}
bool VansParticleDebugWindow::Includes(const ParticleDebugEmitter& emitter) const
{
    return m_SelectedEmitter.empty() || Key(emitter) == m_SelectedEmitter;
}
void VansParticleDebugWindow::ShowWindow(IEngineEditorAPI& api)
{
    if (!VansEditorWindow::m_ParticleDebugWindowOpen)
    {
        m_Snapshot = {}; m_SelectedEmitter.clear(); m_Frozen = false; m_Frame = -1;
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(530, 470), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Particle Debug", &VansEditorWindow::m_ParticleDebugWindowOpen))
    { ImGui::End(); return; }
    Refresh(api);
    ImGui::Checkbox("Scene Overlay (X-ray)", &m_Overlay);
    ImGui::SameLine(); ImGui::Checkbox("Node Indices", &m_Indices);
    ImGui::BeginDisabled(m_Snapshot.emitters.empty());
    ImGui::Checkbox("Freeze Display", &m_Frozen);
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::TextDisabled("Display only; simulation continues.");
    ImGui::TextColored(ImVec4(1, .63f, .27f, 1), "Orange: source root");
    ImGui::SameLine(); ImGui::TextColored(ImVec4(.39f, .86f, .96f, 1), "Cyan: simulated node");
    if (!m_Snapshot.available || m_Snapshot.emitters.empty())
    {
        ImGui::TextDisabled(m_Snapshot.available ? "No live Ribbon nodes. Play a Ribbon effect to inspect it."
            : "No runtime scene.");
        ImGui::TextDisabled("Only Ribbon visualization is supported in this window.");
        ImGui::End(); return;
    }
    std::string selectedLabel = "All Ribbon emitters";
    for (const auto& emitter : m_Snapshot.emitters)
        if (Key(emitter) == m_SelectedEmitter) selectedLabel = Label(emitter);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##Emitter", selectedLabel.c_str()))
    {
        if (ImGui::Selectable("All Ribbon emitters", m_SelectedEmitter.empty())) m_SelectedEmitter.clear();
        for (const auto& emitter : m_Snapshot.emitters)
            if (ImGui::Selectable(Label(emitter).c_str(), Key(emitter) == m_SelectedEmitter)) m_SelectedEmitter = Key(emitter);
        ImGui::EndCombo();
    }
    ImGui::Text("%llu emitters | %llu / %llu nodes%s",
        static_cast<unsigned long long>(m_Snapshot.totalEmitters),
        static_cast<unsigned long long>(m_Snapshot.capturedPoints),
        static_cast<unsigned long long>(m_Snapshot.totalPoints), m_Frozen ? " | FROZEN" : "");
    if (m_Snapshot.truncated) ImGui::TextDisabled("Display capped at 4096 nodes / 256 emitters; simulation is unaffected.");
    const char* planes[] = {"Front (XY)", "Side (ZY)", "Top (XZ)"};
    ImGui::SetNextItemWidth(145); ImGui::Combo("Preview", &m_Plane, planes, 3);
    // 使用轻量正交节点预览，无需新增渲染目标、深度采样或预览模拟器。
    const auto planar = [&](const Vec3& point) {
        return m_Plane == 0 ? ImVec2(point.x, point.y) : m_Plane == 1 ? ImVec2(point.z, point.y) : ImVec2(point.x, point.z);
    };
    ImVec2 lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), hi(-lo.x, -lo.y);
    for (const auto& emitter : m_Snapshot.emitters) if (Includes(emitter))
        for (const auto& ribbon : emitter.ribbons) for (const auto& node : ribbon.points)
        {
            if (!Finite(node.position)) continue;
            const auto p = planar(node.position);
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
        }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(100.0f, ImGui::GetContentRegionAvail().x), std::max(140.0f, ImGui::GetContentRegionAvail().y));
    ImGui::InvisibleButton("##RibbonNodes", size);
    if (lo.x > hi.x || lo.y > hi.y)
    { ImGui::End(); return; }
    const bool hovered = ImGui::IsItemHovered();
    const float scale = std::min((size.x - 40) / std::max(.02f, hi.x - lo.x), (size.y - 40) / std::max(.02f, hi.y - lo.y));
    const auto project = [&](const Vec3& point) {
        const auto p = planar(point);
        return ImVec2(origin.x + size.x * .5f + (p.x - (lo.x + hi.x) * .5f) * scale,
            origin.y + size.y * .5f - (p.y - (lo.y + hi.y) * .5f) * scale);
    };
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(25, 28, 32, 255));
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    const ParticleRibbonPointSnapshot* hoveredNode = nullptr;
    for (const auto& emitter : m_Snapshot.emitters) if (Includes(emitter))
        for (const auto& ribbon : emitter.ribbons)
            for (std::size_t i = 0; i < ribbon.points.size(); ++i)
            {
                const auto& node = ribbon.points[i];
                if (!Finite(node.position)) continue;
                const ImVec2 screen = project(node.position);
                if (i > 0 && Finite(ribbon.points[i - 1].position))
                    draw->AddLine(project(ribbon.points[i - 1].position), screen, LineColor, 1.3f);
                DrawNode(draw, screen, i == 0 && ribbon.hasSourceRoot, i, m_Indices);
                const auto mouse = ImGui::GetMousePos();
                if (hovered && std::hypot(mouse.x - screen.x, mouse.y - screen.y) < 7) hoveredNode = &node;
            }
    draw->PopClipRect();
    if (hoveredNode)
        ImGui::SetTooltip("Sequence: %llu\nWorld: %.3f, %.3f, %.3f\nWidth: %.3f m | Alpha: %.3f",
            static_cast<unsigned long long>(hoveredNode->sequence), hoveredNode->position.x, hoveredNode->position.y,
            hoveredNode->position.z, hoveredNode->width, hoveredNode->alpha);
    ImGui::End();
}
void VansParticleDebugWindow::DrawSceneOverlay(IEngineEditorAPI& api,
    const glm::mat4& viewProjection, ImVec2 origin, ImVec2 size)
{
    if (!VansEditorWindow::m_ParticleDebugWindowOpen || !m_Overlay) return;
    Refresh(api);
    const auto project = [&](const Vec3& point, ImVec2& screen) {
        const auto clip = viewProjection * glm::vec4(point.x, point.y, point.z, 1);
        if (!std::isfinite(clip.w) || clip.w <= .0001f) return false;
        const auto ndc = glm::vec3(clip) / clip.w;
        if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || ndc.z < 0 || ndc.z > 1) return false;
        screen = ImVec2(origin.x + (ndc.x * .5f + .5f) * size.x, origin.y + (-ndc.y * .5f + .5f) * size.y);
        return true;
    };
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    for (const auto& emitter : m_Snapshot.emitters) if (Includes(emitter))
        for (const auto& ribbon : emitter.ribbons)
            for (std::size_t i = 0; i < ribbon.points.size(); ++i)
            {
                ImVec2 screen, previous;
                if (!project(ribbon.points[i].position, screen)) continue;
                if (i > 0 && project(ribbon.points[i - 1].position, previous)) draw->AddLine(previous, screen, LineColor, 1.3f);
                DrawNode(draw, screen, i == 0 && ribbon.hasSourceRoot, i, m_Indices);
            }
    draw->PopClipRect();
}
}
