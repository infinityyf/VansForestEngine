#pragma once

#include "../BRDFData/VansLightFrameTypes.h"
#include "../BRDFData/VansLightCookie.h"
#include <array>
#include <optional>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace VansGraphics
{
    struct VansRenderLightFrameData;

    struct VansGILightingBounds
    {
        glm::vec3 minimum{0.0f}, maximum{0.0f};
        bool IntersectsProbe(const glm::vec3& position, float reach) const;
    };

    struct VansGILightingChanges
    {
        bool global = false;
        // 由光源语义变化决定响应，不能从随机 GI 样本反推权重。
        float changeAmount = 1.0f;
        // 合并变化灯光的新旧影响范围；探针只扫描一次，不建立灯数乘探针数的循环。
        std::optional<VansGILightingBounds> local;
        bool Any() const { return global || local.has_value(); }
    };

    // 资源发布者提供的有界内容变化；按纹理层寻址，避免把可交换的灯索引当成资产身份。
    struct VansGILightingContentChanges
    {
        std::array<float,VANS_LIGHT_COOKIE_COUNT> cookies{};
        std::array<float,VANS_MAX_RECT_LIGHTS> rectLayers{};
        float surfaces=0.0f;

        void AddCookie(size_t slot,float amount) { Add(cookies.at(slot),amount); }
        void AddRectLayer(size_t layer,float amount) { Add(rectLayers.at(layer),amount); }
        void AddSurface(float amount) { Add(surfaces,amount); }
    private:
        static void Add(float& target,float amount)
        {
            if(!std::isfinite(amount) || amount<0.0f || amount>1.0f)
                throw std::invalid_argument("GI texture change must be finite and in [0, 1]");
            target=std::min(1.0f,target+amount);
        }
    };

    // GI 自己拥有已消费的灯光状态；不依赖 Editor、阴影驻留或 Vulkan 资源。
    class VansGILightingChangeTracker
    {
    public:
        VansGILightingChanges Update(const VansRenderLightFrameData& frame, const VansGILightingContentChanges& content = {});

    private:
        struct LightState
        {
            uint64_t signature = 0, discreteSignature = 0;
            glm::vec3 energy{0.0f};
            std::array<glm::vec3, 3> orientation{};
            std::array<glm::vec4, 2> shape{};
            VansLightCookieGPU cookie;
            VansGILightingBounds bounds;
            bool present = false;
        };
        static float ChangeAmount(const LightState& before, const LightState& after);
        std::array<LightState, VANS_MAX_DIRECTION_LIGHTS> m_Directional{};
        float m_SkyScale = 0.0f;
        std::array<LightState, VANS_MAX_POINT_LIGHTS + VANS_MAX_SPOT_LIGHTS + VANS_MAX_RECT_LIGHTS> m_Local{};
        uint64_t m_GlobalSignature = 0;
        bool m_Initialized = false;
    };
}
