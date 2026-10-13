#include "VansParticleAsset.h"
#include <algorithm>

namespace VansGraphics
{
std::vector<Vans::VansAssetGuid> VansParticleAsset::TextureDependencies() const
{
    std::vector<Vans::VansAssetGuid> result;
    const auto add = [&](const std::string& text) {
        Vans::VansAssetGuid guid;
        if (Vans::VansAssetGuid::TryParse(text,guid) && std::find(result.begin(),result.end(),guid)==result.end()) result.push_back(guid);
    };
    for (const auto& emitter : m_Emitters)
    {
        if (!emitter || emitter->m_RendererConfig.m_Type==VansParticleRendererType::None) continue;
        const auto& renderer=emitter->m_RendererConfig;
        if (renderer.m_LightingMode==VansParticleLightingMode::SixWayLit)
        { add(renderer.m_SixWayLighting.m_PositiveAxesTextureGuid); add(renderer.m_SixWayLighting.m_NegativeAxesTextureGuid); }
        else add(renderer.m_TextureGuid);
        if (renderer.m_LightingMode==VansParticleLightingMode::SpriteCard && !renderer.m_Card.lookupTextureGuid.empty())
            add(renderer.m_Card.lookupTextureGuid);
        if (renderer.m_LightingMode==VansParticleLightingMode::SpriteCard && renderer.m_Card.breakup!=VansParticleCardBreakup::None)
            add(renderer.m_Card.breakupTextureGuid);
    }
    return result;
}
}
