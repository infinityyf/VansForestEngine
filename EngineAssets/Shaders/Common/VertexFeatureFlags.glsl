#ifndef VANS_VERTEX_FEATURE_FLAGS_GLSL
#define VANS_VERTEX_FEATURE_FLAGS_GLSL

const uint VANS_VERTEX_FEATURE_SKELETAL_SKINNING = 1u << 0;
const uint VANS_VERTEX_FEATURE_PREVIOUS_SKINNING = 1u << 1;
const uint VANS_VERTEX_FEATURE_MORPH_TARGET = 1u << 2;
const uint VANS_VERTEX_FEATURE_PRE_SKINNED_CACHE = 1u << 3;

bool VansHasVertexFeature(uint featureMask, uint feature)
{
    return (featureMask & feature) != 0u;
}

#endif
