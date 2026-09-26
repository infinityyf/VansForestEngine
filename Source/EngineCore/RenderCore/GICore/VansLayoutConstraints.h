#pragma once

#include "../GeometryCore/VansTriangleGeometryQuery.h"

namespace VansGraphics
{
    // Layout policy supplied by GI world data. Geometry snapshots remain data-only.
    class IVansLayoutConstraints
    {
    public:
        virtual ~IVansLayoutConstraints() = default;
        virtual bool IsPositionValid(glm::vec3 position, float clearance) const = 0;
        virtual VansGeometrySurfaceMeasure MeasureSurface(
            glm::vec3 minimum, glm::vec3 maximum) const = 0;
    };
}
