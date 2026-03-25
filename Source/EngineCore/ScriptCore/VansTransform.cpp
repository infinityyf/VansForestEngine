#include "VansTransform.h"
#include "../Util/VansLog.h"

// Static member definitions
std::vector<VansGraphics::VansTransform> VansGraphics::VansTransformStore::GlobalTransforms;
std::queue<uint32_t> VansGraphics::VansTransformStore::FreeTransformIndices;
std::map<uint32_t, bool> VansGraphics::VansTransformStore::TransformIDToTransformDirty;

glm::mat4x4 VansGraphics::VansTransform::GetModelMatrix()
{
    glm::mat4 model(1.0f);
    model = glm::translate(model, m_Position);
    // ZYX order: matches ComputeModelDataFromTransform (Rz * Ry * Rx) and
    // ImGuizmo's DecomposeMatrixToComponents convention so round-trips are stable.
    model = glm::rotate(model, glm::radians(m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, glm::radians(m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, m_Scale);
    return model;
}

// ══════════════════════════════════════════════════════════════════════════════
//  VansTransformParentSystem
// ══════════════════════════════════════════════════════════════════════════════

void VansGraphics::VansTransformParentSystem::SetParent(uint32_t childTransformID,
                                                         uint32_t parentTransformID)
{
    // Reject self-parenting
    if (childTransformID == parentTransformID)
    {
        VANS_LOG("[TransformParent] Cannot parent a transform to itself (ID " << childTransformID << ")");
        return;
    }

    // Cycle detection: walk up from the proposed parent; if we reach the child
    // we would create a cycle.
    {
        uint32_t walker = parentTransformID;
        while (true)
        {
            auto it = m_ChildToLinkIndex.find(walker);
            if (it == m_ChildToLinkIndex.end()) break;          // reached a root — no cycle
            uint32_t walkerParent = m_Links[it->second].parentTransformID;
            if (walkerParent == childTransformID)
            {
                VANS_LOG("[TransformParent] Cycle detected — rejecting SetParent("
                         << childTransformID << " -> " << parentTransformID << ")");
                return;
            }
            walker = walkerParent;
        }
    }

    // If the child already has a parent link, update it in place.
    auto it = m_ChildToLinkIndex.find(childTransformID);
    if (it != m_ChildToLinkIndex.end())
    {
        TransformParentLink& link = m_Links[it->second];
        link.parentTransformID = parentTransformID;
        link.offsetDirty = true;
        SortLinksTopologically();
        return;
    }

    // Create a new link.
    TransformParentLink link;
    link.childTransformID  = childTransformID;
    link.parentTransformID = parentTransformID;
    link.offsetDirty       = true;

    m_ChildToLinkIndex[childTransformID] = m_Links.size();
    m_Links.push_back(link);

    SortLinksTopologically();
}

void VansGraphics::VansTransformParentSystem::ClearParent(uint32_t childTransformID)
{
    auto it = m_ChildToLinkIndex.find(childTransformID);
    if (it == m_ChildToLinkIndex.end()) return;

    size_t idx = it->second;

    // Swap-and-pop to keep the vector compact.
    if (idx != m_Links.size() - 1)
    {
        std::swap(m_Links[idx], m_Links.back());
        // Update the index of the element we just swapped in.
        m_ChildToLinkIndex[m_Links[idx].childTransformID] = idx;
    }
    m_Links.pop_back();
    m_ChildToLinkIndex.erase(it);

    SortLinksTopologically();
}

bool VansGraphics::VansTransformParentSystem::HasParent(uint32_t childTransformID) const
{
    return m_ChildToLinkIndex.count(childTransformID) > 0;
}

uint32_t VansGraphics::VansTransformParentSystem::GetParent(uint32_t childTransformID) const
{
    auto it = m_ChildToLinkIndex.find(childTransformID);
    if (it == m_ChildToLinkIndex.end()) return UINT32_MAX;
    return m_Links[it->second].parentTransformID;
}

void VansGraphics::VansTransformParentSystem::MarkOffsetDirty(uint32_t childTransformID)
{
    auto it = m_ChildToLinkIndex.find(childTransformID);
    if (it == m_ChildToLinkIndex.end()) return;
    m_Links[it->second].offsetDirty = true;
}

void VansGraphics::VansTransformParentSystem::ResolveParentChildTransforms()
{
    for (auto& link : m_Links)
    {
        glm::mat4 parentWorld = VansTransformStore::GetTransform(link.parentTransformID).GetModelMatrix();

        if (link.offsetDirty)
        {
            // First frame after link creation — compute initial offset, no carry.
            glm::mat4 childWorld = VansTransformStore::GetTransform(link.childTransformID).GetModelMatrix();
            link.offsetMatrix          = glm::inverse(parentWorld) * childWorld;
            link.prevParentWorldMatrix = parentWorld;
            link.offsetDirty           = false;
            continue;
        }

        // If parent moved since last frame, carry the child along using last frame's offset.
        if (!MatrixApproxEqual(parentWorld, link.prevParentWorldMatrix))
        {
            glm::mat4 newChildWorld = parentWorld * link.offsetMatrix;

            glm::vec3 pos, rotDeg, scale;
            DecomposeMatrix(newChildWorld, pos, rotDeg, scale);

            VansTransform& childTf = VansTransformStore::GetTransform(link.childTransformID);
            childTf.m_Position = pos;
            childTf.m_Rotation = rotDeg;
            childTf.m_Scale    = scale;

            VansTransformStore::TransformIDToTransformDirty[link.childTransformID] = true;
        }

        // Always refresh the offset from the current world transforms so that
        // any child edits (editor, gizmo, physics) are automatically captured
        // for the next frame — no explicit MarkOffsetDirty() calls needed.
        glm::mat4 childWorld = VansTransformStore::GetTransform(link.childTransformID).GetModelMatrix();
        link.offsetMatrix          = glm::inverse(parentWorld) * childWorld;
        link.prevParentWorldMatrix = parentWorld;
    }
}

void VansGraphics::VansTransformParentSystem::Clear()
{
    m_Links.clear();
    m_ChildToLinkIndex.clear();
}

void VansGraphics::VansTransformParentSystem::SortLinksTopologically()
{
    // Build set of all child IDs (i.e. transforms that are parented).
    std::unordered_set<uint32_t> parentedSet;
    for (auto& link : m_Links)
        parentedSet.insert(link.childTransformID);

    // Stable sort: links whose parent is NOT itself a child come first.
    // This guarantees parent is resolved before child for multi-level chains.
    std::stable_sort(m_Links.begin(), m_Links.end(),
        [&](const TransformParentLink& a, const TransformParentLink& b)
        {
            bool aParentIsChild = parentedSet.count(a.parentTransformID) > 0;
            bool bParentIsChild = parentedSet.count(b.parentTransformID) > 0;
            return !aParentIsChild && bParentIsChild;
        });

    // Rebuild index map after sort.
    m_ChildToLinkIndex.clear();
    for (size_t i = 0; i < m_Links.size(); ++i)
        m_ChildToLinkIndex[m_Links[i].childTransformID] = i;
}

void VansGraphics::VansTransformParentSystem::DecomposeMatrix(const glm::mat4& m,
                                                                glm::vec3& pos,
                                                                glm::vec3& rotDeg,
                                                                glm::vec3& scale)
{
    pos = glm::vec3(m[3]);

    scale.x = glm::length(glm::vec3(m[0]));
    scale.y = glm::length(glm::vec3(m[1]));
    scale.z = glm::length(glm::vec3(m[2]));

    // Guard against zero scale.
    glm::vec3 safeScale = glm::max(scale, glm::vec3(1e-6f));

    glm::mat3 rotMat(
        glm::vec3(m[0]) / safeScale.x,
        glm::vec3(m[1]) / safeScale.y,
        glm::vec3(m[2]) / safeScale.z
    );

    // Extract Euler angles matching engine ZYX rotation order.
    glm::quat q = glm::quat_cast(rotMat);
    rotDeg = glm::degrees(glm::eulerAngles(q));
}

bool VansGraphics::VansTransformParentSystem::MatrixApproxEqual(const glm::mat4& a,
                                                                  const glm::mat4& b,
                                                                  float eps)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (glm::abs(a[c][r] - b[c][r]) > eps)
                return false;
    return true;
}