#include "VansTerrainInteraction.h"
#include <cmath>

namespace Vans
{
bool ValidateTerrainInteraction(const VansTerrainInteraction& input, std::string& error)
{
    glm::vec4 rectangle; std::array<std::int64_t, 2> origin;
    if (!ResolveWorldTextureWindow(input.window, {}, rectangle, origin))
        error="Terrain interaction requires a finite positive world rectangle and nonzero resolution";
    else if (input.window.resolution.x>16384 || input.window.resolution.y>16384)
        error="Terrain interaction resolution exceeds 16384";
    else if (!std::isfinite(input.albedoStrength) || input.albedoStrength<0 || input.albedoStrength>1 ||
             !std::isfinite(input.normalStrength) || input.normalStrength<0 || input.normalStrength>1 ||
             !std::isfinite(input.maxDisplacementMeters) || input.maxDisplacementMeters<0 ||
             !std::isfinite(input.edgeFadeMeters) || input.edgeFadeMeters<0 ||
             input.edgeFadeMeters>=0.5f*glm::min(input.window.worldRect.z,input.window.worldRect.w))
        error="Terrain interaction strengths, displacement bound or edge fade are invalid";
    else
    {
        for (int i=0; i<4; ++i) if (!std::isfinite(input.heightWeights[i]))
        { error="Terrain interaction height weights must be finite"; return false; }
        for (const auto* key : {&input.albedo,&input.normal,&input.height})
            if (key->size()>512 || key->find('\0')!=std::string::npos)
            { error="Terrain interaction resource key is invalid"; return false; }
        error.clear(); return true;
    }
    return false;
}
}
