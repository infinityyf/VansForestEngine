#pragma once

#include "VansTriangleGeometryQuery.h"
#include <string>
#include <functional>

namespace VansGraphics
{
    class VansScene;
    class VansVKDevice;

    struct VansGeometryReceiverBounds
    {
        glm::vec3 minimum{}, maximum{};
    };

    struct VansSceneGeometrySnapshot
    {
        VansTriangleGeometryQuery opaque;
        std::vector<VansGeometryTriangle> transmissionReceivers;
        std::vector<VansGeometryReceiverBounds> dynamicReceivers;
        // Optional native scene fields supply layout constraints without triangle or entity proxies.
        std::function<bool(glm::vec3,float)> additionalPositionValid;
        std::function<VansGeometrySurfaceMeasure(glm::vec3,glm::vec3)> additionalSurface;
        uint32_t meshCount = 0;
        uint32_t staticInstanceCount = 0;
        uint32_t dynamicInstanceCount = 0;

        // 仅收集场景几何；调用方独立决定受光需求、布局和预算。
        // 场景准备阶段或现有 render-thread idle transaction 内调用，失败不发布部分快照。
        static bool Capture(const VansScene& scene, VansVKDevice& device,
            VansSceneGeometrySnapshot& output, std::string& error);
    };
}
