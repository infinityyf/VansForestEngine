#define IMGUI_DEFINE_MATH_OPERATORS
#include "VansGizmos.h"
#include "imgui.h"
#include "../RenderCore/VulkanCore/VansMesh.h"
#include "../Util/VansInputManager.h"
#include <glm/gtc/type_ptr.hpp>

namespace VansGraphics
{

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

ImGuizmo::OPERATION VansGizmos::OperationFromMode(GizmoMode mode)
{
    switch (mode)
    {
    case GizmoMode::Translate: return ImGuizmo::TRANSLATE;
    case GizmoMode::Rotate:    return ImGuizmo::ROTATE;
    case GizmoMode::Scale:     return ImGuizmo::SCALE;
    default:                   return ImGuizmo::TRANSLATE;
    }
}

void VansGizmos::UnprojectRay(VansCamera*  camera,
                               float        ndcX,
                               float        ndcY,
                               glm::vec3&   outOrigin,
                               glm::vec3&   outDir)
{
    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectiveMatrix();

    // Use the matrix as stored (Vulkan Y-flip included); the inverse already
    // accounts for it when unprojecting back to world space.
    glm::mat4 invPV = glm::inverse(proj * view);

    // Vulkan depth range is [0, 1]
    glm::vec4 nearPt = invPV * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPt  = invPV * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPt /= nearPt.w;
    farPt  /= farPt.w;

    outOrigin = glm::vec3(nearPt);
    outDir    = glm::normalize(glm::vec3(farPt) - glm::vec3(nearPt));
}

float VansGizmos::RaySphereIntersect(const glm::vec3& ro,
                                      const glm::vec3& rd,
                                      const glm::vec3& center,
                                      float            radius)
{
    glm::vec3 oc = ro - center;
    float b = glm::dot(oc, rd);
    float c = glm::dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0f) return -1.0f;
    float sqrtDisc = glm::sqrt(disc);
    float t0 = -b - sqrtDisc;
    float t1 = -b + sqrtDisc;
    if (t0 > 0.0f) return t0;
    if (t1 > 0.0f) return t1;
    return -1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
//  VansGizmos::Draw
// ─────────────────────────────────────────────────────────────────────────────

void VansGizmos::Draw(VansScene*  scene,
                      VansCamera* camera,
                      ImVec2      windowPos,
                      ImVec2      windowSize)
{
    if (!scene || !camera) return;

    VansRenderNode* node = scene->m_SelectedNode;
    if (!node)  return;

    // ── 1. Bind ImGuizmo to the current ImGui window ─────────────────────────
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(windowPos.x, windowPos.y,
                      windowSize.x, windowSize.y);

    // ── 2. Build camera matrices ──────────────────────────────────────────────
    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectiveMatrix();

    // ImGuizmo renders through ImGui's screen-space draw list; pass the
    // projection matrix exactly as the engine stores it (Vulkan Y-flip included).
    // Do NOT negate proj[1][1] here — ImGuizmo's screen-space math already
    // compensates, and negating it causes the visible Y-axis flip.

    // ── 3. Current model matrix ───────────────────────────────────────────────
    VansTransform& tf = VansTransformStore::GetTransform(node->m_TransformID);
    glm::mat4 modelMatrix = tf.GetModelMatrix();

    // ── 4. Mode and space ─────────────────────────────────────────────────────
    ImGuizmo::OPERATION op    = OperationFromMode(m_Mode);
    ImGuizmo::MODE      space = (m_Space == GizmoSpace::World)
                                ? ImGuizmo::WORLD
                                : ImGuizmo::LOCAL;

    // ── 5. Draw + interact ────────────────────────────────────────────────────
    float delta[16] = {};
    bool changed = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        op, space,
        glm::value_ptr(modelMatrix),
        delta
    );

    // ── 6. Write back decomposed T / R / S ────────────────────────────────────
    if (changed)
    {
        glm::vec3 pos, rotDeg, scale;
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(modelMatrix),
            glm::value_ptr(pos),
            glm::value_ptr(rotDeg),   // ImGuizmo returns Euler degrees
            glm::value_ptr(scale)
        );

        tf.m_Position = pos;
        tf.m_Rotation = rotDeg;  // engine stores degrees; GetModelMatrix() calls glm::radians() internally
        tf.m_Scale    = scale;

        // Mark dirty so UpdateTransformRenderData() re-uploads the GPU SSBO.
        VansTransformStore::TransformIDToTransformDirty[node->m_TransformID] = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  VansGizmos::TryPickObject
// ─────────────────────────────────────────────────────────────────────────────

void VansGizmos::TryPickObject(VansScene*  scene,
                                VansCamera* camera,
                                ImVec2      mousePos,
                                ImVec2      windowPos,
                                ImVec2      windowSize)
{
    if (!scene || !camera) return;
    if (windowSize.x <= 0.0f || windowSize.y <= 0.0f) return;

    // Mouse → NDC in [-1, 1]
    float ndcX = 2.0f * (mousePos.x - windowPos.x) / windowSize.x - 1.0f;
    float ndcY = 2.0f * (mousePos.y - windowPos.y) / windowSize.y - 1.0f;

    glm::vec3 rayOrigin, rayDir;
    UnprojectRay(camera, ndcX, ndcY, rayOrigin, rayDir);

    // Collect all candidate render nodes
    struct Candidate
    {
        VansRenderNode* node;
        float           t;
    };

    float         bestT    = FLT_MAX;
    VansRenderNode* bestNode = nullptr;

    auto testNode = [&](VansRenderNode* node)
    {
        if (!node || !node->m_Mesh) return;

        VansTransform& tf   = VansTransformStore::GetTransform(node->m_TransformID);
        glm::vec3      pos  = tf.m_Position;

        // ── Prefer exact bounds from CPU mesh data when available ─────────────
        const std::vector<float>& rawPos = node->m_Mesh->GetMeshRawPositionData();
        float radius = 1.0f;

        if (rawPos.size() >= 3)
        {
            // Compute local AABB from raw XYZ triples and convert to world-space sphere
            glm::vec3 localMin( FLT_MAX), localMax(-FLT_MAX);
            for (size_t i = 0; i + 2 < rawPos.size(); i += 3)
            {
                glm::vec3 v(rawPos[i], rawPos[i + 1], rawPos[i + 2]);
                localMin = glm::min(localMin, v);
                localMax = glm::max(localMax, v);
            }
            glm::vec3 halfExt = (localMax - localMin) * 0.5f * tf.m_Scale;
            // World-space AABB center (approximate – ignores rotation)
            glm::vec3 localCenter = (localMin + localMax) * 0.5f;
            pos += localCenter * tf.m_Scale;
            radius = glm::length(halfExt);
        }
        else
        {
            // Fallback: sphere whose radius approximates the mesh extent via scale
            radius = glm::max(glm::length(tf.m_Scale) * 0.5f, 0.25f);
        }

        float t = RaySphereIntersect(rayOrigin, rayDir, pos, radius);
        if (t > 0.0f && t < bestT)
        {
            bestT    = t;
            bestNode = node;
        }
    };

    for (VansRenderNode* n : scene->m_OpaqueRenderNodes)      testNode(n);
    for (VansRenderNode* n : scene->m_TransParentRenderNodes) testNode(n);

    if (bestNode)
        scene->m_SelectedNode = bestNode;
}

// ─────────────────────────────────────────────────────────────────────────────
//  VansGizmos::HandleHotkeys
// ─────────────────────────────────────────────────────────────────────────────

void VansGizmos::HandleHotkeys(VansScene* scene)
{
    // Only fire when no text widget is active (avoids conflicts with input fields)
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    auto& input = Vans::VansInputManager::Get();

    if (input.IsKeyPressed(GLFW_KEY_W)) m_Mode  = GizmoMode::Translate;
    if (input.IsKeyPressed(GLFW_KEY_E)) m_Mode  = GizmoMode::Rotate;
    if (input.IsKeyPressed(GLFW_KEY_R)) m_Mode  = GizmoMode::Scale;

    if (input.IsKeyPressed(GLFW_KEY_X))
        m_Space = (m_Space == GizmoSpace::World) ? GizmoSpace::Local : GizmoSpace::World;

    if (input.IsKeyPressed(GLFW_KEY_ESCAPE) && scene)
        scene->m_SelectedNode = nullptr;
}

} // namespace VansGraphics
