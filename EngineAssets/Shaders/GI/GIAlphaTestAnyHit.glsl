// PBR coverage: base-color A. Cloth coverage: dedicated opacity R (slot 2).
// Cloth also uses any-hit for single-sided rejection; its solid interior need
// not fetch coverage when cutoff is zero. Blended materials remain excluded.
struct Vertex
{
    f16vec3 position;
    f16vec2 uv;
    f16vec3 normal;
    f16vec3 tangent;
    f16vec3 bitangent;
};

hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 3, std430, scalar) readonly buffer VertexBuffers
{
    Vertex vertices[];
} vertexBuffers[];

layout(set = 0, binding = 4, std430) readonly buffer IndexBuffers
{
    uint indices[];
} indexBuffers[];

layout(set = 0, binding = 5, std430) readonly buffer InstanceDataBuffer
{
    uint instances[];
} instanceData;

#include "GIInstanceMaterial.glsl"

layout(set = 0, binding = 50) uniform sampler2D PBRTextures[];


void main()
{
    const uint instanceID = gl_InstanceID;
    const uint packedTextureIndex = instanceMaterialData.materials[instanceID].packedTextureIndex;
    if (packedTextureIndex == 0xFFFFFFFFu)
        return;

    GIInstanceMaterial material = instanceMaterialData.materials[instanceID];
    if (GIRejectMaterialFace(material, gl_HitKindEXT != gl_HitKindBackFacingTriangleEXT))
    {
        ignoreIntersectionEXT;
        return;
    }
    if (instanceMaterialData.materials[instanceID].alphaCutoff <= 0.0)
        return;

    const uint textureIndex = GICoverageTextureIndex(material);
    if (textureIndex >= 2048u)
        return;

    const uint modelIndex = instanceData.instances[instanceID];
    const uint indexBase = gl_PrimitiveID * 3u;
    const Vertex v0 = vertexBuffers[modelIndex].vertices[indexBuffers[modelIndex].indices[indexBase]];
    const Vertex v1 = vertexBuffers[modelIndex].vertices[indexBuffers[modelIndex].indices[indexBase + 1u]];
    const Vertex v2 = vertexBuffers[modelIndex].vertices[indexBuffers[modelIndex].indices[indexBase + 2u]];
    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    const vec2 uv =
        vec2(v0.uv) * barycentrics.x +
        vec2(v1.uv) * barycentrics.y +
        vec2(v2.uv) * barycentrics.z;

    const vec4 coverageSample = textureLod(PBRTextures[nonuniformEXT(textureIndex)], uv, 0.0);
    if (GIRejectMaterialCoverage(material, coverageSample))
        ignoreIntersectionEXT;
}
