#include "VansGILightingChangeTracker.h"
#include "../VansRenderSceneSnapshot.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{
    using namespace VansGraphics;

    uint64_t Mix(uint64_t seed, uint64_t value)
    {
        return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u));
    }

    uint64_t MixFloat(uint64_t seed, float value, double scale = 4096.0)
    {
        // 保留既有检测精度；使用 double 的位模式避免大数转 int64 溢出。
        const double rounded = std::isfinite(value) ? std::round(double(value) * scale) : 0.0;
        const double canonical = rounded == 0.0 ? 0.0 : rounded;
        uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(canonical));
        std::memcpy(&bits, &canonical, sizeof(bits));
        return Mix(seed, bits);
    }

    uint64_t MixVector(uint64_t seed, const glm::vec3& value, double scale = 4096.0)
    {
        for (int axis = 0; axis < 3; ++axis) seed = MixFloat(seed, value[axis], scale);
        return seed;
    }

    uint64_t MixCookie(uint64_t seed, const VansLightCookieFrame& cookies, unsigned kind, size_t index)
    {
        const size_t slot = VansLightCookieOffset(kind) + index;
        const auto& cookie = cookies.data[slot];
        const bool enabled = cookie.options.x > 0.0f;
        seed = Mix(seed, enabled);
        if (!enabled) return seed;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                seed = MixFloat(seed, cookie.worldToLight[column][row]);
        for (int axis = 0; axis < 4; ++axis)
        {
            seed = MixFloat(seed, cookie.scaleOffset[axis]);
            seed = MixFloat(seed, cookie.projection[axis]);
            seed = MixFloat(seed, cookie.options[axis]);
        }
        for (unsigned char character : cookies.textures[slot]) seed = Mix(seed, character);
        return seed;
    }

    void Include(std::optional<VansGILightingBounds>& bounds, const VansGILightingBounds& added)
    {
        if (!bounds) bounds = added;
        else
        {
            bounds->minimum = glm::min(bounds->minimum, added.minimum);
            bounds->maximum = glm::max(bounds->maximum, added.maximum);
        }
    }
}

bool VansGraphics::VansGILightingBounds::IntersectsProbe(const glm::vec3& position, float reach) const
{
    const auto distance = glm::max(glm::max(minimum - position, position - maximum), glm::vec3(0.0f));
    return glm::dot(distance, distance) <= reach * reach;
}

float VansGraphics::VansGILightingChangeTracker::ChangeAmount(const LightState& before, const LightState& after)
{
    if (before.present != after.present || before.discreteSignature != after.discreteSignature) return 1.0f;
    auto magnitude = [](glm::vec3 value) { value = glm::abs(value); return std::max({value.x,value.y,value.z}); };
    auto relative = [](float a, float b, float floor) { return std::abs(a-b) / std::max({std::abs(a),std::abs(b),floor}); };
    float change = magnitude(after.energy-before.energy) / std::max({magnitude(before.energy),magnitude(after.energy),1e-4f});
    const glm::vec3 oldCenter = (before.bounds.minimum+before.bounds.maximum)*0.5f;
    const glm::vec3 newCenter = (after.bounds.minimum+after.bounds.maximum)*0.5f;
    const float reach = std::max(magnitude(before.bounds.maximum-oldCenter),magnitude(after.bounds.maximum-newCenter));
    change = std::max(change,glm::length(newCenter-oldCenter)/std::max(reach*0.25f,0.01f));
    for (size_t i=0;i<before.orientation.size();++i)
        change = std::max(change,glm::length(after.orientation[i]-before.orientation[i]));
    for (size_t i=0;i<before.shape.size();++i)
        for (int axis=0;axis<4;++axis)
            change = std::max(change,relative(before.shape[i][axis],after.shape[i][axis],1.0f));
    if (after.cookie.options.x>0.0f)
    {
        for (int column=0;column<4;++column)
            for (int row=0;row<4;++row)
                change = std::max(change,relative(before.cookie.worldToLight[column][row],after.cookie.worldToLight[column][row],1.0f));
        for (int axis=0;axis<4;++axis)
        {
            change = std::max(change,relative(before.cookie.scaleOffset[axis],after.cookie.scaleOffset[axis],1.0f));
            change = std::max(change,relative(before.cookie.projection[axis],after.cookie.projection[axis],1.0f));
        }
        change = std::max(change,relative(before.cookie.options.x,after.cookie.options.x,1.0f));
    }
    // 源变化量与缓存更新频率解耦；突变和连续变化都不依赖蒙特卡洛样本。
    return std::clamp(change*2.0f,0.0f,1.0f);
}

VansGraphics::VansGILightingChanges VansGraphics::VansGILightingChangeTracker::Update(
    const VansRenderLightFrameData& frame, const VansGILightingContentChanges& content)
{
    if (frame.directionalLights.size() > VANS_MAX_DIRECTION_LIGHTS ||
        frame.pointLights.size() > VANS_MAX_POINT_LIGHTS || frame.spotLights.size() > VANS_MAX_SPOT_LIGHTS ||
        frame.rectLights.size() > VANS_MAX_RECT_LIGHTS)
        throw std::invalid_argument("GI lighting frame exceeds the native light capacity");

    auto validAmount=[](float value){return std::isfinite(value) && value>=0.0f && value<=1.0f;};
    if(!validAmount(content.surfaces) ||
        !std::all_of(content.cookies.begin(),content.cookies.end(),validAmount) ||
        !std::all_of(content.rectLayers.begin(),content.rectLayers.end(),validAmount))
        throw std::invalid_argument("GI texture changes contain an invalid amount");
    auto cookieChange=[&](unsigned kind,size_t index)
    {
        const size_t slot=VansLightCookieOffset(kind)+index;
        return frame.cookies.data[slot].options.x>0.0f?content.cookies[slot]:0.0f;
    };

    auto setCookie = [&](LightState& light, unsigned kind, size_t index)
    {
        const size_t slot = VansLightCookieOffset(kind)+index;
        light.cookie = frame.cookies.data[slot];
        const bool enabled = light.cookie.options.x>0.0f;
        light.discreteSignature = Mix(light.discreteSignature,enabled);
        if (!enabled) return;
        for (int axis=1;axis<4;++axis) light.discreteSignature = MixFloat(light.discreteSignature,light.cookie.options[axis]);
        for (unsigned char character : frame.cookies.textures[slot]) light.discreteSignature = Mix(light.discreteSignature,character);
    };
    decltype(m_Directional) directional{};
    uint64_t global = Mix(1469598103934665603ull, frame.directionalLights.size());
    global = MixFloat(global, frame.skyLighting.radianceScale);
    for (size_t i = 0; i < frame.directionalLights.size(); ++i)
    {
        // 帧快照已完成日/月转换，检测与 GPU 消费的同一组最终值。
        const auto& light = frame.directionalLights[i];
        global = MixVector(global, light.m_Direction, 1024.0);
        global = MixVector(global, light.m_Color);
        global = MixFloat(global, light.m_Intensity);
        global = MixCookie(global, frame.cookies, 0, i);
        auto& sample = directional[i];
        sample.present = true;
        sample.energy = light.m_Color*light.m_Intensity;
        sample.orientation[0] = light.m_Direction;
        setCookie(sample,0,i);
    }

    decltype(m_Local) current{};
    auto setLocal = [&](size_t slot, uint64_t signature, const glm::vec3& position, float radius) -> LightState&
    {
        auto& light = current[slot];
        light.present = true;
        light.signature = signature;
        const glm::vec3 extent(std::max(radius, 0.0f));
        light.bounds = { position - extent, position + extent };
        return light;
    };
    for (size_t i = 0; i < frame.pointLights.size(); ++i)
    {
        const auto& light = frame.pointLights[i];
        uint64_t signature = MixVector(1469598103934665603ull, light.m_Position, 1024.0);
        signature = MixVector(signature, light.m_Color);
        signature = MixFloat(signature, light.m_Intensity);
        signature = MixFloat(signature, light.m_Radius, 1024.0);
        signature = MixFloat(signature, light.m_IESProfileIndex);
        auto& sample = setLocal(i, MixCookie(signature, frame.cookies, 1, i), light.m_Position, light.m_Radius);
        sample.energy = light.m_Color*light.m_Intensity;
        sample.shape[0].x = light.m_Radius;
        sample.discreteSignature = MixFloat(0,light.m_IESProfileIndex);
        setCookie(sample,1,i);
    }
    for (size_t i = 0; i < frame.spotLights.size(); ++i)
    {
        const auto& light = frame.spotLights[i];
        uint64_t signature = MixVector(1469598103934665603ull, light.m_Position, 1024.0);
        signature = MixVector(signature, light.m_Direction, 1024.0);
        signature = MixVector(signature, light.m_Color);
        signature = MixFloat(signature, light.m_Intensity);
        signature = MixFloat(signature, light.m_Radius, 1024.0);
        signature = MixFloat(signature, light.m_InnerCutOff);
        signature = MixFloat(signature, light.m_OuterCutOff);
        signature = MixFloat(signature, light.m_IESProfileIndex);
        signature = MixFloat(signature, light.m_IESIntensityScale);
        auto& sample = setLocal(VANS_MAX_POINT_LIGHTS + i, MixCookie(signature, frame.cookies, 2, i), light.m_Position, light.m_Radius);
        sample.energy = light.m_Color*light.m_Intensity;
        sample.orientation[0] = light.m_Direction;
        sample.shape[0] = glm::vec4(light.m_Radius,light.m_InnerCutOff,light.m_OuterCutOff,light.m_IESIntensityScale);
        sample.discreteSignature = MixFloat(0,light.m_IESProfileIndex);
        setCookie(sample,2,i);
    }
    for (size_t i = 0; i < frame.rectLights.size(); ++i)
    {
        const auto& light = frame.rectLights[i];
        uint64_t signature = MixVector(1469598103934665603ull, light.m_Position, 1024.0);
        signature = MixVector(signature, light.m_Normal, 1024.0);
        signature = MixVector(signature, light.m_Right, 1024.0);
        signature = MixVector(signature, light.m_Up, 1024.0);
        signature = MixVector(signature, light.m_Color);
        signature = MixFloat(signature, light.m_Intensity);
        signature = MixFloat(signature, light.m_Range, 1024.0);
        signature = MixFloat(signature, light.m_HalfWidth, 1024.0);
        signature = MixFloat(signature, light.m_HalfHeight, 1024.0);
        signature = MixFloat(signature, light.m_TwoSided);
        signature = MixFloat(signature, light.m_AttenuationExp);
        signature = MixFloat(signature, light.m_TextureSlot);
        signature = MixFloat(signature, light.m_TexLodBias);
        const float extent = std::max(light.m_Range, 0.0f) + glm::length(glm::vec2(light.m_HalfWidth, light.m_HalfHeight));
        auto& sample = setLocal(VANS_MAX_POINT_LIGHTS + VANS_MAX_SPOT_LIGHTS + i,
            MixCookie(signature, frame.cookies, 3, i), light.m_Position, extent);
        sample.energy = light.m_Color*light.m_Intensity;
        sample.orientation = {light.m_Normal,light.m_Right,light.m_Up};
        sample.shape[0] = glm::vec4(light.m_Range,light.m_HalfWidth,light.m_HalfHeight,light.m_AttenuationExp);
        sample.shape[1].x = light.m_TexLodBias;
        sample.discreteSignature = MixFloat(MixFloat(0,light.m_TextureSlot),light.m_TwoSided);
        setCookie(sample,3,i);
    }

    VansGILightingChanges changes;
    changes.changeAmount = 0.0f;
    if (m_Initialized)
    {
        float textureGlobal=content.surfaces;
        for(size_t i=0;i<frame.directionalLights.size();++i)
            textureGlobal=std::max(textureGlobal,cookieChange(0,i));
        changes.global = global != m_GlobalSignature || textureGlobal>0.0f;
        if (changes.global)
        {
            const float skyDelta = std::abs(frame.skyLighting.radianceScale-m_SkyScale) /
                std::max({std::abs(frame.skyLighting.radianceScale),std::abs(m_SkyScale),1e-4f});
            changes.changeAmount = std::max(textureGlobal,std::clamp(skyDelta*2.0f,0.0f,1.0f));
            for (size_t i=0;i<directional.size();++i)
                changes.changeAmount = std::max(changes.changeAmount,ChangeAmount(m_Directional[i],directional[i]));
        }
        for (size_t i = 0; i < current.size(); ++i)
        {
            const auto& before = m_Local[i];
            const auto& after = current[i];
            float textureChange=0.0f;
            if(after.present)
            {
                const unsigned kind=i<VANS_MAX_POINT_LIGHTS?1u:i<VANS_MAX_POINT_LIGHTS+VANS_MAX_SPOT_LIGHTS?2u:3u;
                const size_t lightIndex=i-(kind==1u?0u:kind==2u?VANS_MAX_POINT_LIGHTS:VANS_MAX_POINT_LIGHTS+VANS_MAX_SPOT_LIGHTS);
                textureChange=cookieChange(kind,lightIndex);
                if(kind==3u)
                {
                    const float layer=frame.rectLights[lightIndex].m_TextureSlot;
                    if(layer>=0.0f && layer<float(content.rectLayers.size()))
                        textureChange=std::max(textureChange,content.rectLayers[size_t(layer)]);
                }
            }
            if (textureChange==0.0f && before.present == after.present && (!after.present || before.signature == after.signature)) continue;
            changes.changeAmount = std::max({changes.changeAmount,textureChange,ChangeAmount(before,after)});
            // 紧凑灯数组的 swap-pop 也包含旧范围，不会漏掉移除或移动后留下的光照。
            if (before.present) Include(changes.local, before.bounds);
            if (after.present) Include(changes.local, after.bounds);
        }
    }
    m_Directional = directional;
    m_SkyScale = frame.skyLighting.radianceScale;
    m_Local = current;
    m_GlobalSignature = global;
    m_Initialized = true;
    return changes;
}
