#version 450

layout(location = 0) in vec2 inMeshPos;
layout(push_constant) uniform WaterPatchPC
{
    vec2 patchWorldOrigin;
    float patchWorldSize;
    int lodLevel;
    float waterLevel;
    uint outerEdgeMask;
    vec2 padding;
} pc;

layout(set = 1, binding = 0) uniform WaterSurfaceParams
{
    mat4 waterVPMatrix;
    mat4 waterViewMatrix;
    vec4 waterCameraPosition;
    ivec4 geometryParams;
    vec4 geometryScale;
    vec4 spectrumScale;
    vec4 windAndChop;
    ivec4 simulationParams;
    vec4 waveParticleParams0;
    vec4 waveParticleParams1;
    vec4 flowMapWorld;
    vec4 flowMapParams;
    vec4 flowMapFallback;
} params;
layout(set = 1, binding = 1) uniform sampler2DArray displacementMap;
layout(set = 1, binding = 4) uniform sampler2DArray derivativeMap;
layout(set = 1, binding = 5) uniform sampler2D flowMap;

const uint EDGE_LEFT = 1u;
const uint EDGE_RIGHT = 2u;
const uint EDGE_DOWN = 4u;
const uint EDGE_UP = 8u;

struct SurfaceData
{
    vec3 displacement;
    vec3 dPdx;
    vec3 dPdz;
    float foam;
};

struct CascadeSample
{
    vec4 displacement;
    vec3 dPdx;
    vec3 dPdz;
};

float EdgeMorph(vec2 uv)
{
    float width = clamp(1.0 - params.geometryScale.y, 0.05, 0.95);
    float morph = 0.0;
    if ((pc.outerEdgeMask & EDGE_LEFT) != 0u)  morph = max(morph, 1.0 - smoothstep(0.0, width, uv.x));
    if ((pc.outerEdgeMask & EDGE_RIGHT) != 0u) morph = max(morph, smoothstep(1.0 - width, 1.0, uv.x));
    if ((pc.outerEdgeMask & EDGE_DOWN) != 0u)  morph = max(morph, 1.0 - smoothstep(0.0, width, uv.y));
    if ((pc.outerEdgeMask & EDGE_UP) != 0u)    morph = max(morph, smoothstep(1.0 - width, 1.0, uv.y));
    return morph;
}

vec2 SampleFlowVector(vec2 worldXZ)
{
    if (params.flowMapParams.x <= 0.0)
        return vec2(0.0);

    vec2 flowSize = max(params.flowMapWorld.zw, vec2(0.001));
    vec2 flowUV = (worldXZ - params.flowMapWorld.xy) / flowSize;
    vec4 encoded = textureLod(flowMap, flowUV, 0.0);
    vec2 flow = encoded.xy * 2.0 - 1.0;
    if (dot(flow, flow) < 1e-5)
    {
        vec2 fallback = params.flowMapFallback.xy;
        flow = dot(fallback, fallback) < 1e-5 ? vec2(1.0, 0.0) : normalize(fallback);
    }
    return flow * params.flowMapParams.y;
}

CascadeSample SampleCascadePoint(vec2 worldXZ, float coverage, int cascade)
{
    CascadeSample result;
    vec2 baseUV = fract(worldXZ / coverage);
    if (params.flowMapParams.x <= 0.0 || params.flowMapParams.y <= 0.0)
    {
        result.displacement = textureLod(displacementMap, vec3(baseUV, float(cascade)), 0.0);
        result.dPdx = textureLod(derivativeMap, vec3(baseUV, float(cascade * 2)), 0.0).xyz;
        result.dPdz = textureLod(derivativeMap, vec3(baseUV, float(cascade * 2 + 1)), 0.0).xyz;
        return result;
    }

    vec2 flow = SampleFlowVector(worldXZ);
    float cycleLength = max(params.flowMapParams.w, 0.05);
    float phase0 = fract(params.spectrumScale.z * params.flowMapParams.z / cycleLength);
    float phase1 = fract(phase0 + 0.5);
    float blend = abs(phase0 * 2.0 - 1.0);
    vec2 uv0 = fract((worldXZ - flow * phase0 * cycleLength) / coverage);
    vec2 uv1 = fract((worldXZ - flow * phase1 * cycleLength) / coverage);

    vec4 disp0 = textureLod(displacementMap, vec3(uv0, float(cascade)), 0.0);
    vec4 disp1 = textureLod(displacementMap, vec3(uv1, float(cascade)), 0.0);
    vec3 dx0 = textureLod(derivativeMap, vec3(uv0, float(cascade * 2)), 0.0).xyz;
    vec3 dx1 = textureLod(derivativeMap, vec3(uv1, float(cascade * 2)), 0.0).xyz;
    vec3 dz0 = textureLod(derivativeMap, vec3(uv0, float(cascade * 2 + 1)), 0.0).xyz;
    vec3 dz1 = textureLod(derivativeMap, vec3(uv1, float(cascade * 2 + 1)), 0.0).xyz;

    result.displacement = mix(disp0, disp1, blend);
    result.dPdx = mix(dx0, dx1, blend);
    result.dPdz = mix(dz0, dz1, blend);
    return result;
}

CascadeSample SampleCascadeMaps(vec2 worldXZ, float coverage, int cascade,
                                float geometryFootprint)
{
    float texelWorld = coverage / float(textureSize(displacementMap, 0).x);
    if (geometryFootprint <= 2.0 * texelWorld)
        return SampleCascadePoint(worldXZ, coverage, cascade);

    // 远处顶点以几何 footprint 对周期场做显式低通。它保留 cascade 中仍可解析
    // 的长波，同时抑制同一层里的短波走样，避免按最短波长整层截断。
    float offset = geometryFootprint * 0.35;
    CascadeSample s00 = SampleCascadePoint(worldXZ + vec2(-offset, -offset), coverage, cascade);
    CascadeSample s10 = SampleCascadePoint(worldXZ + vec2( offset, -offset), coverage, cascade);
    CascadeSample s01 = SampleCascadePoint(worldXZ + vec2(-offset,  offset), coverage, cascade);
    CascadeSample s11 = SampleCascadePoint(worldXZ + vec2( offset,  offset), coverage, cascade);

    CascadeSample result;
    result.displacement = (s00.displacement + s10.displacement
        + s01.displacement + s11.displacement) * 0.25;
    result.dPdx = (s00.dPdx + s10.dPdx + s01.dPdx + s11.dPdx) * 0.25;
    result.dPdz = (s00.dPdz + s10.dPdz + s01.dPdz + s11.dPdz) * 0.25;
    return result;
}

SurfaceData SampleSurface(vec2 worldXZ, float geometryPatchSize)
{
    SurfaceData result;
    result.displacement = vec3(0.0);
    result.dPdx = vec3(1.0, 0.0, 0.0);
    result.dPdz = vec3(0.0, 0.0, 1.0);
    result.foam = 0.0;
    float geometryFootprint = geometryPatchSize / float(max(params.geometryParams.y - 1, 1));
    int cascadeCount = clamp(params.geometryParams.z, 1, 4);
    for (int cascade = 0; cascade < cascadeCount; ++cascade)
    {
        float coverage = params.spectrumScale.x * pow(params.spectrumScale.y, float(cascade));
        float upperWavelength = coverage;
        float frequencyWeight = smoothstep(
            0.5, 1.0, upperWavelength / max(2.0 * geometryFootprint, 1e-4));
        CascadeSample cascadeSample = SampleCascadeMaps(
            worldXZ, coverage, cascade, geometryFootprint);
        result.displacement += cascadeSample.displacement.xyz * frequencyWeight;
        result.dPdx += (cascadeSample.dPdx - vec3(1.0, 0.0, 0.0)) * frequencyWeight;
        result.dPdz += (cascadeSample.dPdz - vec3(0.0, 0.0, 1.0)) * frequencyWeight;
        result.foam = max(result.foam, cascadeSample.displacement.w * frequencyWeight);
    }
    return result;
}

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out float outLinearDepth;
layout(location = 2) out vec3 outWorldNormal;
layout(location = 3) flat out int outLodLevel;
layout(location = 4) out vec2 outWorldXZ;

void main()
{
    float morph = EdgeMorph(inMeshPos);
    vec2 meshPosition = inMeshPos;
    if (morph > 0.0)
    {
        float cells = float(params.geometryParams.y - 1);
        vec2 parentGrid = floor(inMeshPos * cells * 0.5) * 2.0 / cells;
        meshPosition = mix(inMeshPos, parentGrid, morph);
    }
    vec2 worldXZ = pc.patchWorldOrigin + meshPosition * pc.patchWorldSize;
    SurfaceData surface = SampleSurface(worldXZ, pc.patchWorldSize);
    if (morph > 0.0 && pc.lodLevel + 1 < params.geometryParams.x)
    {
        SurfaceData parent = SampleSurface(worldXZ, pc.patchWorldSize * params.geometryScale.w);
        surface.displacement = mix(surface.displacement, parent.displacement, morph);
        surface.dPdx = mix(surface.dPdx, parent.dPdx, morph);
        surface.dPdz = mix(surface.dPdz, parent.dPdz, morph);
        surface.foam = mix(surface.foam, parent.foam, morph);
    }

    vec3 worldPosition = vec3(worldXZ.x, pc.waterLevel, worldXZ.y) + surface.displacement;
    vec3 worldNormal = normalize(cross(surface.dPdz, surface.dPdx));
    vec4 viewPosition = params.waterViewMatrix * vec4(worldPosition, 1.0);
    outWorldPos = worldPosition;
    outLinearDepth = -viewPosition.z;
    outWorldNormal = worldNormal;
    outLodLevel = pc.lodLevel;
    outWorldXZ = worldXZ;
    gl_Position = params.waterVPMatrix * vec4(worldPosition, 1.0);
}
