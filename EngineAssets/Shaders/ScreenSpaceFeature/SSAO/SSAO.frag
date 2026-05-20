#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/Common.glsl"

layout( location = 0 ) in vec2 fragTexCoord;
layout(set = 1, binding = 0) uniform sampler2D  normalInput;
layout(set = 1, binding = 1) uniform sampler2D  gbufferInput0;
layout(set = 1, binding = 2) uniform sampler2D  gbufferInput1;
layout(set = 1, binding = 3) uniform sampler2D  gbufferInput2;
layout(set = 1, binding = 4) uniform sampler2D  depthInput;
layout(set = 1, binding = 5, rgba32f ) uniform image2D outColor;


#define SCREEN_SCALE 2

void main() 
{
    float aoResult = 0;
    ivec2 halfResSize = ivec2(ScreenParams.xy / SCREEN_SCALE);
    ivec2 outPixel = ivec2(fragTexCoord * ScreenParams.xy / SCREEN_SCALE);
    if (any(lessThan(outPixel, ivec2(0))) || any(greaterThanEqual(outPixel, halfResSize)))
        return;

    vec4 positionData = texture(gbufferInput2, fragTexCoord);
    vec3 position_world = positionData.xyz;
    float currentDepth = positionData.w;
    if(currentDepth <= 0.0)
    {
        imageStore(outColor, outPixel, vec4(1.0, 1.0, 1.0, 1.0));
        return;
    }

    vec3 normal = texture(normalInput, fragTexCoord).xyz;
    if (length(normal) < 0.5)
    {
        imageStore(outColor, outPixel, vec4(1.0, 1.0, 1.0, 1.0));
        return;
    }
    normal = normalize(normal);

    vec3 viewDirection = normalize(cameraPosition.xyz - position_world);

    //构建TBN
    vec3 helperAxis = (abs(normal.y) < 0.95) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helperAxis, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3x3 TBN = mat3x3(tangent, bitangent, normal);

    vec2 pixelCoord = fragTexCoord * ScreenParams.xy / SCREEN_SCALE;
    //获取多个采样点
    for (int i = 0; i < SSAO_SAMPLE_COUNT; ++i) 
    {
        // SSAO 当前没有 temporal 累积，采样核必须在帧间稳定，避免每帧随机导致噪点闪烁。
        float fi = float(i);
        float random0 = RandomInterLeavedWithScale(pixelCoord, fi);
        float random1 = RandomInterLeavedWithScale(pixelCoord + vec2(17.0, 31.0), fi);
        float random2 = RandomInterLeavedWithScale(pixelCoord + vec2(53.0, 7.0),  fi);

        vec3 randomOffset = vec3(random0 * 2.0 - 1.0, random1 * 2.0 - 1.0, random2);
        randomOffset = normalize(randomOffset);

        float scale = float(i) / SSAO_SAMPLE_COUNT;
        scale = mix(0.01f, 1.0f, scale * scale);
        randomOffset *= scale;

        //转到世界空间
        randomOffset *= SSAO_RADIUS;
        randomOffset = TBN * randomOffset;

        vec3 randSamplePositon = position_world + randomOffset;
        float currentRadius = length(randomOffset);

        vec4 viewPosition =  ViewMatrix * vec4(randSamplePositon, 1.0);
        vec4 randSampleNDC = ProjectionMatrix * viewPosition;
        randSampleNDC /= randSampleNDC.w;
        randSampleNDC.xy = randSampleNDC.xy * 0.5 + 0.5; // 转换到NDC坐标系
        randSampleNDC.y = 1.0 - randSampleNDC.y; // 翻转Y轴
        if (any(lessThan(randSampleNDC.xy, vec2(0.0))) || any(greaterThan(randSampleNDC.xy, vec2(1.0))))
            continue;

        // //采样点的场景深度
        // float depth = texture(depthInput, randSampleNDC.xy).x;
        // //转到线性空间
        // depth = LinearizeDepth(depth, NearPlane, FarPlane);

        float depth = texture(gbufferInput2, randSampleNDC.xy).w;
        if (depth <= 0.0)
            continue;

        //消除自遮挡
        float ao = (-viewPosition.z > (depth + SSAO_DEPHT_BIAS)) ? 1.0 : 0.0;

        //消除断层
        float depthDiff = abs(-viewPosition.z - depth);
        float rangeWeight = smoothstep(SSAO_DEPTH_THRESHOLD, 0.0, depthDiff);
        ao *= rangeWeight;

        //需要根据权重添加系数
        aoResult += ao;
    }
    aoResult /= float(SSAO_SAMPLE_COUNT); // 计算平均值
    aoResult = 1 - aoResult;
    aoResult = clamp(aoResult, 0.0, 1.0);

    imageStore(outColor, outPixel, vec4(aoResult, aoResult, aoResult, 1.0));
}