#ifndef VANS_GRASS_INSTANCE_GLSL
#define VANS_GRASS_INSTANCE_GLSL
// 与 CPU GrassInstance 一致，完整变换由 PCG 确定。
struct GrassInstance
{
    mat4 modelMatrix;
    float boundsRadius;
    uint randomSeed;
    uvec2 padding;
};
vec3 grassRoot(GrassInstance instance) { return instance.modelMatrix[3].xyz; }
mat3 grassBindFrame(GrassInstance instance)
{
    vec3 up = normalize(instance.modelMatrix[1].xyz);
    vec3 right = normalize(instance.modelMatrix[0].xyz - up * dot(instance.modelMatrix[0].xyz, up));
    return mat3(right, up, normalize(cross(right, up)));
}
mat4 grassInverseBind(GrassInstance instance, float restHeight)
{
    mat3 frameInverse = transpose(grassBindFrame(instance));
    mat4 result = mat4(frameInverse);
    result[3] = vec4(frameInverse * (-normalize(instance.modelMatrix[1].xyz) * restHeight), 1.0);
    return result;
}
#endif
