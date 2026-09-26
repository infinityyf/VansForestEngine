#pragma once

#include "VansMeshGeometryReadback.h"
#include "VansTriangleGeometryQuery.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VansGraphics
{
    class VansMesh;

    struct VansSceneTriangleInstance
    {
        VansMesh* mesh = nullptr;
        glm::mat4 model{1.0f};
        uint32_t id = 0;
        uint64_t priority = 0;
        bool twoSided = false;
    };

    struct VansSceneTriangleHit
    {
        uint32_t instance = UINT32_MAX;
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f};
        float distance = 0.0f;
        VansGeometryHit triangle;
    };

    // Shared Runtime/Editor cache for mesh BVHs, instance transforms and instance BVH traversal.
    // Consumers retain filtering and animated-deformation policy at their own layer.
    class VansSceneTriangleRaycastCache
    {
    public:
        VansSceneTriangleRaycastCache();
        ~VansSceneTriangleRaycastCache();
        VansSceneTriangleRaycastCache(VansSceneTriangleRaycastCache&&) noexcept;
        VansSceneTriangleRaycastCache& operator=(VansSceneTriangleRaycastCache&&) noexcept;
        VansSceneTriangleRaycastCache(const VansSceneTriangleRaycastCache&) = delete;
        VansSceneTriangleRaycastCache& operator=(const VansSceneTriangleRaycastCache&) = delete;

        // Removes dead meshes and returns each missing or buffer-replaced mesh once.
        std::vector<VansMesh*> FindStale(
            const std::vector<VansMesh*>& alive, const std::vector<VansMesh*>& needed);
        // Builds every replacement first, then publishes the complete batch atomically.
        bool Store(const std::vector<VansMesh*>& meshes,
            std::vector<VansMeshGeometryData> geometry, bool retainGeometry,
            std::string& error);
        static bool BuildMeshQuery(const VansMeshGeometryData& mesh,
            const std::vector<glm::vec3>& positions,
            VansTriangleGeometryQuery& query, std::string& error);
        const VansMeshGeometryData* FindGeometry(VansMesh* mesh) const;

        bool BuildInstances(std::vector<VansSceneTriangleInstance> instances,
            std::string& error);
        bool Raycast(const glm::vec3& origin, const glm::vec3& direction,
            float maxDistance, const std::function<bool(uint32_t)>& accept,
            VansSceneTriangleHit& hit, float minDistance = 0.0001f,
            const VansGeometryQueryOptions& options = {}) const;

        size_t MeshCount() const;
        size_t TriangleCount() const;
        size_t InstanceCount() const;

    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
}
