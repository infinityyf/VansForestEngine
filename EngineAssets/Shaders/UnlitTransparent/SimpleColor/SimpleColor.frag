#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"

layout( location = 0 ) in vec2 frag_uv;
layout( location = 1 ) in vec3 normal_ws;
layout( location = 2 ) in vec3 position_world;

// ── Per-material descriptor set (Set 1) ──
// Textures bound in the same order as the material JSON "textures" array.
layout( set = 1, binding = 0 ) uniform sampler2D baseColorTex;
layout( set = 1, binding = 1 ) uniform sampler2D normalMapTex;

// Single color output with alpha
layout( location = 0 ) out vec4 outColor;

void main()
{
    // Sample base color from texture
    vec4 texColor = texture(baseColorTex, frag_uv);

    // Basic rim-lighting effect for visual interest
    vec3 viewDir = normalize(cameraPosition.xyz - position_world);
    float NdotV  = max(dot(normalize(normal_ws), viewDir), 0.0);
    float rim    = 1.0 - NdotV;

    vec3  baseColor = texColor.rgb;
    float alpha     = texColor.a * (0.4 + 0.4 * rim);  // modulate texture alpha by rim

    outColor = vec4(baseColor, alpha);
}
