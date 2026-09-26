#include "VansSceneGeometrySnapshot.h"
#include "VansMeshGeometryReadback.h"
#include "../VansScene.h"
#include "../VansRenderNode.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace VansGraphics
{
    bool VansSceneGeometrySnapshot::Capture(const VansScene& scene, VansVKDevice& device,
        VansSceneGeometrySnapshot& output, std::string& error)
    {
        error.clear();
        struct Instance { VansRenderNode* node; uint32_t mesh; bool transmission; bool twoSided; };
        std::vector<Instance> instances;
        std::vector<VansMesh*> meshes;
        std::unordered_map<VansMesh*, uint32_t> meshIndices;
        std::unordered_set<uint32_t> movingTransforms;
        for (const auto* physics : scene.GetPhysicsNodes())
            if (physics && physics->GetProperties().bodyType != VansEngine::PhysicsBodyType::Static)
                movingTransforms.insert(physics->GetTransformID());
        auto collectMoving = [&](const std::vector<VansRenderNode*>& nodes)
        {
            for (const auto* node : nodes)
				if (node && (node->m_AnimationEnabled
					|| node->m_VertexDeformationState.BuildFeatureMask() != 0))
                    movingTransforms.insert(node->m_TransformID);
        };
        collectMoving(scene.GetOpaqueRenderNodes());
        collectMoving(scene.GetForwardOpaquePreAtmosphereRenderNodes());
        collectMoving(scene.GetTransparentRenderNodes());
        std::unordered_map<uint32_t, bool> movement;
        auto isMoving = [&](uint32_t transform)
        {
            std::vector<uint32_t> path;
            bool moving = false;
            while (transform != UINT32_MAX)
            {
                if (movingTransforms.count(transform)) { moving = true; break; }
                const auto found = movement.find(transform);
                if (found != movement.end()) { moving = found->second; break; }
                if (std::find(path.begin(), path.end(), transform) != path.end())
                    throw std::runtime_error("Cyclic transform hierarchy in geometry snapshot");
                path.push_back(transform);
                transform = scene.GetParentTransformID(transform);
            }
            for (uint32_t child : path) movement.emplace(child, moving);
            return moving;
        };
        VansSceneGeometrySnapshot pending;
        auto collect = [&](const std::vector<VansRenderNode*>& nodes, bool transmission, const char* pass)
        {
            for (auto* node : nodes)
            {
                if (!node || !node->IsEnabled() || !node->m_Mesh || !node->m_Material) continue;
                auto* shader = node->m_Material->GetPassShader(pass);
                if (!shader) continue;
                // 动态父物体的子网格同样不能成为静态遮挡物。
                const bool moving = isMoving(node->m_TransformID);
                const glm::mat4 model = node->GetTransformMatrix();
                if (moving)
                {
                    if (node->m_Mesh->HasLocalBounds())
                    {
                        const auto bounds = MakeRenderBoundsFromLocalAABB(node->m_Mesh->GetLocalBoundsMin(), node->m_Mesh->GetLocalBoundsMax(), model);
                        if (bounds.IsValid()) pending.dynamicReceivers.push_back({bounds.aabb.min, bounds.aabb.max});
                    }
                    continue;
                }
                if (shader->GetPipelineProgramDesc().graphicsState.primitiveTopology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
                    throw std::runtime_error("Geometry snapshot requires triangle-list static surfaces: " + node->m_NodeName);
                const auto inserted = meshIndices.emplace(node->m_Mesh, static_cast<uint32_t>(meshes.size()));
                if (inserted.second) meshes.push_back(node->m_Mesh);
                instances.push_back({node, inserted.first->second, transmission,
                    shader->GetPipelineProgramDesc().graphicsState.cullMode == VK_CULL_MODE_NONE});
            }
        };
        try
        {
            collect(scene.GetOpaqueRenderNodes(), false, VansPass::GBUFFER);
            collect(scene.GetForwardOpaquePreAtmosphereRenderNodes(), false, VansPass::FORWARD_OPAQUE_PRE_ATMOSPHERE);
            collect(scene.GetTransparentRenderNodes(), true, VansPass::FORWARD_TRANSPARENT);
            std::vector<VansMeshGeometryData> geometry;
            if (!VansMeshGeometryReadback::Read(device, meshes, geometry, error)) return false;
            std::vector<VansGeometryTriangle> opaque;
            for (uint32_t owner = 0; owner < instances.size(); ++owner)
            {
                const auto& instance = instances[owner]; const auto& mesh = geometry[instance.mesh];
                const glm::mat4 model = instance.node->GetTransformMatrix();
                for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(model[column][row])) throw std::runtime_error("Non-finite scene geometry transform");
                const float determinant = glm::determinant(glm::mat3(model));
                if (std::abs(determinant) < 1e-12f) continue;
                const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
                auto& triangles = instance.transmission ? pending.transmissionReceivers : opaque;
                for (size_t index = 0; index < mesh.indices.size(); index += 3)
                {
                    const uint32_t a = mesh.indices[index], b = mesh.indices[index + 1], c = mesh.indices[index + 2];
                    VansGeometryTriangle triangle;
                    triangle.a = glm::vec3(model * glm::vec4(mesh.positions[a], 1.0f));
                    triangle.b = glm::vec3(model * glm::vec4(mesh.positions[b], 1.0f));
                    triangle.c = glm::vec3(model * glm::vec4(mesh.positions[c], 1.0f));
                    if (!mesh.normals.empty()) triangle.normal = normalMatrix * (mesh.normals[a] + mesh.normals[b] + mesh.normals[c]);
                    triangle.owner = owner; triangle.twoSided = instance.twoSided;
                    triangles.push_back(triangle);
                }
                ++pending.staticInstanceCount;
            }
            if (!pending.opaque.Build(std::move(opaque), error)) return false;
            // 透射物体只是接收表面，不能参与实体内部或遮挡判定。
            VansTriangleGeometryQuery transmission;
            if (!transmission.Build(std::move(pending.transmissionReceivers), error)) return false;
            pending.transmissionReceivers = transmission.GetTriangles();
            output = std::move(pending);
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
