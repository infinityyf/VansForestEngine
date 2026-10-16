#pragma once
#include <cstddef>
#include <glm/glm.hpp>

namespace VansGraphics
{
    // CPU 端唯一 GI dispatch 数据布局；与 GIProbeDispatch.glsl 同步。
    struct alignas(16) VansGIProbeDispatchParameters
    {
        glm::vec4 gridParams;
        glm::vec4 dispatchParams;
        glm::vec4 frameParams;
        glm::vec4 regionParams;
        glm::vec4 lightingParams;
        glm::vec4 temporalParams;
        glm::vec4 traceParams;
    };
    static_assert(sizeof(VansGIProbeDispatchParameters) == 112, "GI push constant layout must match GLSL");
    static_assert(alignof(VansGIProbeDispatchParameters) == 16, "GI push constant alignment must match GLSL vec4");
    static_assert(offsetof(VansGIProbeDispatchParameters, gridParams) == 0, "GI grid parameters must occupy slot 0");
    static_assert(offsetof(VansGIProbeDispatchParameters, dispatchParams) == 16, "GI dispatch parameters must occupy slot 1");
    static_assert(offsetof(VansGIProbeDispatchParameters, frameParams) == 32, "GI frame parameters must occupy slot 2");
    static_assert(offsetof(VansGIProbeDispatchParameters, regionParams) == 48, "GI region parameters must occupy slot 3");
    static_assert(offsetof(VansGIProbeDispatchParameters, lightingParams) == 64, "GI lighting parameters must occupy slot 4");
    static_assert(offsetof(VansGIProbeDispatchParameters, temporalParams) == 80, "GI temporal parameters must occupy slot 5");
    static_assert(offsetof(VansGIProbeDispatchParameters, traceParams) == 96, "GI trace parameters must occupy slot 6");
}
