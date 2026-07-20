#ifndef WATER_SCREEN_COMMON_GLSL
#define WATER_SCREEN_COMMON_GLSL

// Screen-space water buffers are classification data, not filtered material
// textures.  Always read them with texelFetch so shoreline pixels never blend
// a valid surface with the attachment clear value.
const float WATER_INVALID_DEPTH = 9999.0;

bool WaterSurfaceValid(vec4 normalCoverage, vec4 positionDepth)
{
    return normalCoverage.a > 0.5 &&
           positionDepth.a > 0.0 && positionDepth.a < WATER_INVALID_DEPTH;
}

bool SceneDepthValid(float depth)
{
    return depth > 0.1 && depth < WATER_INVALID_DEPTH;
}

float WaterDepthEpsilon(float depth)
{
    // The water linear-depth attachment is RGBA32F.  A centimetre is enough
    // to absorb raster/depth reconstruction noise without creating a wide
    // no-man's-land at the shoreline.
    return max(0.01, depth * 0.00002);
}

bool SceneOccludesWater(float sceneDepth, float waterDepth)
{
    return SceneDepthValid(sceneDepth) &&
           sceneDepth <= waterDepth + WaterDepthEpsilon(waterDepth);
}

ivec2 WaterClampPixel(ivec2 pixel, ivec2 size)
{
    return clamp(pixel, ivec2(0), size - ivec2(1));
}

ivec2 WaterUVToPixel(vec2 uv, ivec2 size)
{
    return WaterClampPixel(ivec2(uv * vec2(size)), size);
}

vec2 WaterPixelUV(ivec2 pixel, ivec2 size)
{
    return (vec2(pixel) + 0.5) / vec2(size);
}

vec2 WaterNDCToAttachmentUV(vec2 ndc)
{
    // ForestEngine render passes use viewport.y = height and a negative
    // viewport height. NDC +Y therefore maps to attachment row 0.
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

#endif
