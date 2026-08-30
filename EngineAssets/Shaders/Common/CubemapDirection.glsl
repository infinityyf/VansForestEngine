#ifndef VANS_CUBEMAP_DIRECTION_GLSL
#define VANS_CUBEMAP_DIRECTION_GLSL

// Vulkan cubemap layer order: +X, -X, +Y, -Y, +Z, -Z.
vec3 CubemapDirectionFromPixel(ivec2 pixel, uint face, uint resolution)
{
    vec2 uv = (vec2(pixel) + 0.5) / float(max(resolution, 1u));
    vec2 p = uv * 2.0 - 1.0;
    p.y = -p.y;
    if (face == 0u) return normalize(vec3( 1.0, p.y, -p.x));
    if (face == 1u) return normalize(vec3(-1.0, p.y,  p.x));
    if (face == 2u) return normalize(vec3( p.x, 1.0, -p.y));
    if (face == 3u) return normalize(vec3( p.x,-1.0,  p.y));
    if (face == 4u) return normalize(vec3( p.x, p.y,  1.0));
    return normalize(vec3(-p.x, p.y, -1.0));
}

#endif
