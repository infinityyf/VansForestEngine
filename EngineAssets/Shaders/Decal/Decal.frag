#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#include "../Common/CameraData.glsl"
#include "../Common/ModelData.glsl"
#include "../Common/VansDrawSubmission.glsl"
#include "../Common/CustomMaterialData.glsl"
#include "DecalResponse.glsl"
#include "DecalReceiverFilter.glsl"

layout(set = 0, binding = 50) uniform sampler2D globalPBRTextures[];
layout(set = 1, binding = 0) uniform sampler2D gBuffer2Sampler;
layout(set = 1, binding = 1) uniform sampler2D gBuffer1Sampler;
layout(set = 1, binding = 2) uniform sampler2D normalSampler;
layout(location = 0) out vec4 outDecalColor;
layout(location = 1) out vec4 outDecalNormal;
layout(location = 2) out vec4 outDecalRoughness;

void main()
{
    VansDrawData drawData = VansGetDrawData();
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    // 逐像素读取，避免轮廓两侧混合 material ID 和世界坐标。
    vec4 surface = texelFetch(gBuffer2Sampler, pixel, 0);
    if (surface.w <= 0.0) discard;
    vec4 receiver = texelFetch(gBuffer1Sampler, pixel, 0);
    int receiverID = DecodeDecalReceiverMaterialID(receiver.z);
    vec3 response = DecalReceiverResponse(receiverID);
    if (all(equal(response, vec3(0.0)))) discard;

    mat4 model = ModelBuffer.transforms[drawData.transformIndex].ModelMatrix;
    float receiverGroup = ModelBuffer.transforms[drawData.transformIndex].Position.w;
    if (!DecalReceiverMatches(receiverID, receiver.w, receiverGroup)) discard;
    float minimumDot = ModelBuffer.transforms[drawData.transformIndex].Scale.w;
    if (minimumDot >= 0.0 && !DecalNormalMatches(texelFetch(normalSampler, pixel, 0).xyz, model[1].xyz, minimumDot)) discard;
    if (abs(determinant(mat3(model))) < 1e-8) discard;
    vec3 localPosition = (inverse(model) * vec4(surface.xyz, 1.0)).xyz;
    if (any(greaterThan(abs(localPosition), vec3(1.0)))) discard;
    vec2 uv = localPosition.xz * 0.5 + 0.5;

    CustomMaterialPayload material = customMaterialDataBuffer.materials[drawData.materialIndex];
    vec4 colorSample = texture(globalPBRTextures[nonuniformEXT(material.textureIndices.x)], uv, MaterialMipBias);
    vec4 normalSample = texture(globalPBRTextures[nonuniformEXT(material.textureIndices.y)], uv, MaterialMipBias);
    vec4 roughnessSample = texture(globalPBRTextures[nonuniformEXT(material.textureIndices.z)], uv, MaterialMipBias);
    vec3 coverageMask = texture(globalPBRTextures[nonuniformEXT(material.textureIndices.w)], uv, MaterialMipBias).rgb;
    // 每种属性使用自己的 texture alpha；opacity 控制整体，权重控制单通道覆盖。
    vec3 coverage = DecalAttributeCoverage(vec3(colorSample.a, normalSample.a, roughnessSample.a),
        coverageMask, material.values[0].a, material.values[1].yzw, response);
    if (all(lessThanEqual(coverage, vec3(0.0)))) discard;

    vec3 N = DecalSafeNormal(model[1].xyz, vec3(0.0, 1.0, 0.0));
    vec3 T = DecalSafeNormal(model[0].xyz - N * dot(model[0].xyz, N), vec3(1.0, 0.0, 0.0));
    vec3 B = DecalSafeNormal(cross(N, T), vec3(0.0, 0.0, -1.0));
    vec3 normalWS = DecalSafeNormal(mat3(T, B, N) * (normalSample.rgb * 2.0 - 1.0), N);
    outDecalColor = vec4(clamp(material.values[0].rgb * colorSample.rgb, 0.0, 1.0), coverage.x);
    outDecalNormal = vec4(normalWS, coverage.y);
    outDecalRoughness = vec4(clamp(material.values[1].x * roughnessSample.r, 0.0, 1.0), 0.0, 0.0, coverage.z);
}
