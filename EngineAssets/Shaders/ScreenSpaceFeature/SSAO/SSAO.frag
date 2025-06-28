#version 450
#extension GL_GOOGLE_include_directive : require
#define CameraCBBind 0

#include "../../Common/CameraData.glsl"
#include "../../Common/Common.glsl"

layout( location = 0 ) in vec2 fragTexCoord;
layout(set = 1, binding = 0) uniform sampler2D  normalInput;
layout(set = 1, binding = 1) uniform sampler2D  gbufferInput0;
layout(set = 1, binding = 2) uniform sampler2D  gbufferInput1;
layout(set = 1, binding = 3) uniform sampler2D  gbufferInput2;
layout(set = 1, binding = 4) uniform sampler2D  depthInput;
layout(set = 1, binding = 5, rgba32f ) uniform image2D outColor;

void main() 
{
    float aoResult = 0;
    float currentDepth = texture(depthInput, fragTexCoord).x;
    if(currentDepth == 1.0)
    {
        imageStore(outColor, ivec2(fragTexCoord * ScreenParams.xy), vec4(1.0, 1.0, 1.0, 1.0));
        return;
    }

    vec3 position_world = texture(gbufferInput2, fragTexCoord).xyz;
    vec3 normal = texture(normalInput, fragTexCoord).xyz;

    vec3 viewDirection = normalize(cameraPosition.xyz - position_world);

    //构建TBN
    vec3 tangent = normalize(cross(viewDirection,normal));
    vec3 bitangent = cross(normal, tangent);
    mat3x3 TBN = mat3x3(tangent, bitangent, normal);

    vec2 pixelCoord = fragTexCoord * ScreenParams.xy;
    //获取多个采样点
    for (int i = 0; i < SSAO_SAMPLE_COUNT; ++i) 
    {
        float random0 = RandomInterLeavedWithScale(pixelCoord,FrameIndex + i);
        float random1 = RandomInterLeavedWithScale(pixelCoord,FrameIndex + i * 2);
        float random2 = RandomInterLeavedWithScale(pixelCoord,FrameIndex + i * 3);

        vec3 randomOffset = vec3(random0 * 2-1, random1 * 2-1, random2);
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

        //采样点的场景深度
        float depth = texture(depthInput, randSampleNDC.xy).x;
        //转到线性空间
        depth = LinearizeDepth(depth, NearPlane, FarPlane);

        //消除自遮挡
        float ao = (-viewPosition.z > (depth + SSAO_DEPHT_BIAS)) ? 1.0 : 0.0;

        //消除断层
        ao = (abs(-viewPosition.z - depth) < SSAO_DEPTH_THRESHOLD) ? ao : 0.0;

        //需要根据权重添加系数
        aoResult += ao;
    }
    aoResult /= float(SSAO_SAMPLE_COUNT); // 计算平均值
    aoResult = 1 - aoResult;

    imageStore(outColor, ivec2(fragTexCoord * ScreenParams.xy), vec4(aoResult,aoResult,aoResult, 1.0));
}