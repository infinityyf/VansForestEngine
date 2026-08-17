#ifndef VANS_VERTEX_DEFORMATION_GLSL
#define VANS_VERTEX_DEFORMATION_GLSL

#include "VertexFeatureFlags.glsl"
#include "SkeletalSkinning.glsl"

struct VansVertexSurface
{
    vec4 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
};

void VansApplyVertexDeformation(inout VansVertexSurface surface, uint featureMask)
{
    if (!VansHasVertexFeature(featureMask, VANS_VERTEX_FEATURE_SKELETAL_SKINNING))
        return;

    mat4 skinMatrix = VansBuildCurrentSkinMatrix();
    surface.position = skinMatrix * surface.position;

    mat3 skinNormal = mat3(skinMatrix);
    vec3 n = skinNormal * surface.normal;
    vec3 t = skinNormal * surface.tangent;
    vec3 b = skinNormal * surface.bitangent;
    float nl = dot(n, n);
    float tl = dot(t, t);
    float bl = dot(b, b);
    surface.normal = nl > 1e-8 ? n * inversesqrt(nl) : surface.normal;
    surface.tangent = tl > 1e-8 ? t * inversesqrt(tl) : surface.tangent;
    surface.bitangent = bl > 1e-8 ? b * inversesqrt(bl) : surface.bitangent;
}

void VansApplyVertexPositionDeformation(inout vec4 position, uint featureMask)
{
    if (VansHasVertexFeature(featureMask, VANS_VERTEX_FEATURE_SKELETAL_SKINNING))
        position = VansBuildCurrentSkinMatrix() * position;
}

void VansApplyPreviousVertexPositionDeformation(inout vec4 position, uint featureMask)
{
    if (VansHasVertexFeature(featureMask, VANS_VERTEX_FEATURE_SKELETAL_SKINNING))
        position = VansBuildPreviousSkinMatrix() * position;
}

#endif
