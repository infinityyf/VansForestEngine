#include "../Common/CameraData.glsl"
#include "../Atmosphere/AtmosphereCommon.glsl"
#include "../Atmosphere/AtmosphereMediaComposition.glsl"
layout(location=0) in vec4 fragColor;
layout(location=1) in vec2 fragUV;
layout(location=2) in vec3 fragWorldPos;
layout(set=1,binding=0) uniform sampler2D ribbonTexture;
#if VANS_RIBBON_SOFT_DEPTH
layout(set=1,binding=1) uniform sampler2D sceneDepth;
#endif
layout(push_constant) uniform RibbonParameters { vec4 parameters; } ribbon;
layout(location=0) out vec4 outColor;
void main()
{
    vec4 color = texture(ribbonTexture,fragUV)*fragColor;
    // 横向边缘有连续衰减；纵向 UV 由稳定的点年龄产生。
    float edge = 1.0-smoothstep(0.2,1.0,abs(fragUV.y*2.0-1.0));
    color.a *= edge;
    vec2 screenUV = gl_FragCoord.xy/max(ScreenParams.xy,vec2(1));
#if VANS_RIBBON_SOFT_DEPTH
    float depth = texture(sceneDepth,screenUV).r;
    vec4 surface = InverseProjectionMatrix*vec4(screenUV*2.0-1.0,depth,1.0);
    float surfaceZ = -surface.z/surface.w;
    float particleZ = -(ViewMatrix*vec4(fragWorldPos,1)).z;
    color.a *= clamp((surfaceZ-particleZ)/ribbon.parameters.x,0.0,1.0);
#endif
    if (color.a < 0.001) discard;
    color.rgb = CompositeAtmosphereSurfaceRadiance(screenUV,fragWorldPos,color.rgb);
    outColor = color;
}
