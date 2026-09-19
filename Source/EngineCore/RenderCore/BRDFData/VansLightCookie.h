#pragma once
#include <array>
#include <string>
#include <glm/glm.hpp>

namespace VansGraphics
{
    // 与灯光帧索引一一对应，不改变已有光源的 GPU ABI。
    inline constexpr unsigned VANS_LIGHT_COOKIE_COUNT = 161;
    inline constexpr unsigned VansLightCookieOffset(unsigned kind)
    {
        return kind == 0 ? 0 : kind == 1 ? 1 : kind == 2 ? 65 : 129;
    }

    struct VansLightCookieSettings
    {
        bool enabled = false;
        std::string textureGuid;
        float strength = 1.0f;
        float sizeX = 10.0f;
        float sizeY = 10.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float rotationDegrees = 0.0f;
        bool repeat = false;
        bool useAlpha = false;
    };

    struct alignas(16) VansLightCookieGPU
    {
        glm::mat4 worldToLight{1.0f};
        glm::vec4 scaleOffset{1.0f, 1.0f, 0.0f, 0.0f};
        // x/y: 投影尺寸或聚光锥切线；z/w: UV 旋转 cos/sin。
        glm::vec4 projection{10.0f, 10.0f, 1.0f, 0.0f};
        // x: 强度；y: 灯型；z: 平铺；w: Alpha 通道。
        glm::vec4 options{0.0f};
    };
    static_assert(sizeof(VansLightCookieGPU) == 112);

    struct VansLightCookieFrame
    {
        std::array<VansLightCookieGPU, VANS_LIGHT_COOKIE_COUNT> data{};
        std::array<std::string, VANS_LIGHT_COOKIE_COUNT> textures{};
    };
}
