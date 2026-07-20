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
    vec4 microSlopeParams;
    vec4 microDomainParams;
} params;
layout(set = 1, binding = 1) uniform sampler2DArray displacementMap;
layout(set = 1, binding = 4) uniform sampler2DArray derivativeMap;

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
        float lowerWavelength = cascade == 0
            ? 2.0 * coverage / float(textureSize(displacementMap, 0).x)
            : coverage / params.spectrumScale.y;
        float frequencyWeight = smoothstep(0.5, 1.0, lowerWavelength / max(2.0 * geometryFootprint, 1e-4));
        vec2 uv = fract(worldXZ / coverage);
        vec4 displacement = textureLod(displacementMap, vec3(uv, float(cascade)), 0.0);
        vec3 dPdx = textureLod(derivativeMap, vec3(uv, float(cascade * 2)), 0.0).xyz;
        vec3 dPdz = textureLod(derivativeMap, vec3(uv, float(cascade * 2 + 1)), 0.0).xyz;
        result.displacement += displacement.xyz * frequencyWeight;
        result.dPdx += (dPdx - vec3(1.0, 0.0, 0.0)) * frequencyWeight;
        result.dPdz += (dPdz - vec3(0.0, 0.0, 1.0)) * frequencyWeight;
        result.foam = max(result.foam, displacement.w * frequencyWeight);
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
