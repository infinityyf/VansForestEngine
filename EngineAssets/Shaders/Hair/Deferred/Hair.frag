#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/Common.glsl"

layout( location = 0 ) in vec2 frag_uv;
layout( location = 1 ) in vec3 normal_ws;
layout( location = 2 ) in vec3 tangent_ws;
layout( location = 3 ) in vec3 bitangent_ws;
layout( location = 4 ) in vec3 position_world;

// Hair textures (dedicated per-node descriptor set, Set 4)
layout( set = 4, binding = 0 ) uniform sampler2D hairAlbedoAlpha;  // .rgb = albedo, .a = legacy alpha
layout( set = 4, binding = 1 ) uniform sampler2D hairNormal;       // tangent-space normal
layout( set = 4, binding = 2 ) uniform sampler2D hairRoughness;    // .r = roughness
layout( set = 4, binding = 3 ) uniform sampler2D hairAO;           // .r = ambient occlusion
layout( set = 4, binding = 4 ) uniform sampler2D hairShift;        // .r = strand shift (0.5 = neutral)
layout( set = 4, binding = 5 ) uniform sampler2D hairAlphaMask;    // .r = dedicated alpha mask
layout( set = 4, binding = 6 ) uniform sampler2D hairFlowMap;      // .rg = tangent-space flow direction (0.5 = neutral)

// GBuffer MRT outputs
layout (location = 0) out vec4 outNormal;   // xyz = world normal, w = strand shift
layout (location = 1) out vec4 outGBuffer0; // rgb = albedo, w = roughness
layout (location = 2) out vec4 outGBuffer1; // x = tangent oct X [0,1], y = ao, z = material id, w = tangent oct Y [0,1]
layout (location = 3) out vec4 outGBuffer2; // xyz = position world, w = linear depth

layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;
} materialConst;

// Octahedral encode: unit vec3 → vec2 in [-1,1]
vec2 OctEncodeHair(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return n.xy;
}

// ── Hashed Alpha Testing (Wyman & McGuire, I3D 2017) ────────────────────
// Spatially-stable stochastic alpha test using a 3D position hash.
// Produces consistent noise that FSR / TAA denoise into smooth coverage
// without the temporal flicker of per-frame random thresholds.

float hash3D(vec3 s)
{
    // Deterministic 3D → [0,1] hash (paper Listing 1)
    return fract(sin(mod(dot(s, vec3(171.0, 131.0, 7.0)), 6.2831853)) * 53758.5453);
}

float HashedAlpha(float alpha, vec3 objPos, float frameJitter)
{
    // Discretized derivatives of world position → pixel footprint size
    float maxDeriv = max(max(fwidth(objPos.x), fwidth(objPos.y)), fwidth(objPos.z));

    // Pixel scale: how many hash cells fit in a pixel
    const float hashScale = 1.0;
    float pixScale = 1.0 / (hashScale * maxDeriv);

    // Two nearest log₂-discretized noise scales
    float pixScaleMin = exp2(floor(log2(pixScale)));
    float pixScaleMax = exp2(ceil(log2(pixScale)));

    // Hash at both scales — offset by per-frame jitter for temporal variation
    float alpha0 = hash3D(floor(pixScaleMin * objPos) + frameJitter);
    float alpha1 = hash3D(floor(pixScaleMax * objPos) + frameJitter);

    // Interpolation factor between the two scales
    float lerpFactor = fract(log2(pixScale));

    // Linear interpolation of the two hashes
    float x = mix(alpha0, alpha1, lerpFactor);

    // ── CDF transform → uniform distribution (paper Eq. 4) ──────────
    // Without this, the interpolated hash has a triangular PDF;
    // the CDF remapping makes thresholds uniformly distributed in [0,1].
    float a = min(lerpFactor, 1.0 - lerpFactor);
    vec3  cases = vec3(
        x * x / (2.0 * a * (1.0 - a)),                         // x < a
        (x - 0.5 * a) / (1.0 - a),                             // a ≤ x < 1-a
        1.0 - ((1.0 - x) * (1.0 - x) / (2.0 * a * (1.0 - a))) // x ≥ 1-a
    );

    float threshold = (x < (1.0 - a))
                    ? ((x < a) ? cases.x : cases.y)
                    : cases.z;

    // Avoid threshold == 0 (would never discard)
    threshold = clamp(threshold, 1.0e-6, 1.0);

    return step(threshold, alpha);  // 1.0 = keep, 0.0 = discard
}

void main()
{
    vec4 baseSample = texture(hairAlbedoAlpha, frag_uv);

    // ── Hashed Alpha Testing (Wyman & McGuire 2017) ─────────────────────
    // World-space position drives the hash → spatially stable, no flicker.
    // FSR / TAA naturally smooth the stochastic boundary over frames.
    float alpha = texture(hairAlphaMask, frag_uv).r;
    // Per-frame jitter using R2 low-discrepancy sequence for fast convergence
    float frameJitter = fract(float(uint(FrameIndex) % 256u) * 0.7548776662);
    if (HashedAlpha(alpha, position_world, frameJitter) < 0.5)
        discard;

    vec3 albedo    = baseSample.rgb;
    float roughness = texture(hairRoughness, frag_uv).r;
    float ao        = texture(hairAO, frag_uv).r;

    // Strand shift texture: 0.5 = neutral, remapped in BRDF to [-1,1]
    float strandShift = texture(hairShift, frag_uv).r;

    // Normal mapping (amplified xy for stronger detail)
    vec3 normal_sample = textureLod(hairNormal, frag_uv, 0.0).rgb;
    normal_sample = normal_sample * 2.0 - 1.0;
    const float normalStrength = 2.0;
    normal_sample.xy *= normalStrength;

    mat3 TBN = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal = normalize(TBN * normal_sample);

    // ── Flow map: bend tangent and normal along a painted UV direction ──
    // Flow map RG encodes a tangent-space 2D direction (0.5 = neutral).
    // The flow vector is projected into world space via the TBN matrix,
    // then used to rotate the fiber tangent and tilt the shading normal.
    vec3 flowTangent = normalize(tangent_ws);   // default: unmodified fiber tangent
    vec2 flowSample = texture(hairFlowMap, frag_uv).rg;
    vec2 flowDir    = flowSample * 2.0 - 1.0;   // remap [0,1] → [-1,1]
    float flowLen   = length(flowDir);
    if (flowLen > 0.001)
    {
        // Build world-space flow vector from tangent-space direction
        vec3 flowWS = normalize(TBN * vec3(flowDir, 0.0));
        // Bend the tangent toward the flow direction
        flowTangent = normalize(mix(flowTangent, flowWS, flowLen));
        // Tilt the normal slightly away from the flow (keeps energy)
        normal = normalize(normal - flowWS * flowLen * 0.25);
    }

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;

    // Octahedral-encode the (possibly flow-bent) hair fiber tangent
    vec2 octT = OctEncodeHair(flowTangent) * 0.5 + 0.5;

    outNormal   = vec4(normal, strandShift);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(octT.x, ao, float(MATERIAL_ID_HAIR), octT.y);
    outGBuffer2 = vec4(position_world, -linearDepth);
}
