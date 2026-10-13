#include "../Common/CameraData.glsl"
#include "../Atmosphere/AtmosphereMediaComposition.glsl"
layout(location=0) in vec4 fragColor;
layout(location=1) in vec2 fragUV;
layout(location=2) in vec3 fragWorldPos;
layout(location=3) in vec2 fragNextUV;
layout(location=4) in float fragPhase;
layout(location=5) in vec2 fragLocalUV;
layout(location=6) in vec2 fragCardData;
layout(set=1,binding=0) uniform sampler2D colorTexture;
layout(set=1,binding=1) uniform sampler2D lookupTexture;
layout(set=1,binding=3) uniform sampler2D breakupTexture;
#if VANS_CARD_SOFT_DEPTH
layout(set=1,binding=2) uniform sampler2D sceneDepth;
#endif
layout(push_constant) uniform CardParameters { vec4 sheet; vec4 style; vec4 material; vec4 breakup; } card;
layout(location=0) out vec4 outColor;
vec3 EncodeColor(vec3 linearColor)
{
    vec3 color=max(linearColor,vec3(0));
    return mix(1.055*pow(color,vec3(1.0/2.4))-0.055,12.92*color,lessThanEqual(color,vec3(0.0031308)));
}
void main()
{
    // Ordinary adjacent-frame interpolation; no invented motion-vector displacement.
    vec4 color=mix(texture(colorTexture,fragUV),texture(colorTexture,fragNextUV),fragPhase);
    // Recovered SpriteCard channel cases 12 and 6. RedAlpha broadcasts red
    // into all channels; RGBAlpha reconstructs coverage with Rec.601 luma.
    if(card.material.z>1.5) color=vec4(color.r);
    else if(card.material.z>0.5) color.a=dot(color.rgb,vec3(0.299,0.587,0.114));
    if(card.style.z>0.0)
    {
        float index=dot(EncodeColor(color.rgb*color.a),vec3(0.2125,0.7154,0.0721));
        vec3 lookup=texture(lookupTexture,vec2(clamp(index,0.0,1.0),0.5)).rgb;
        if(card.breakup.z>0.5) lookup*=color.rgb;
        color.rgb=mix(color.rgb,lookup,card.style.z);
    }
    if(card.material.w>0.5)
    {
        vec4 mask;
        if(card.material.w>1.5)
        {
            // Recovered ZOOM UV math. The resource uses normalized particle age
            // and zoom scale; the original CPU register packing remains unproven.
            float p=fract(fragCardData.y*card.breakup.x);
            float q=p/(1.0-0.4*(1.0-p));
            vec2 centered=fragLocalUV-0.5;
            mask=mix(texture(breakupTexture,centered*(1.0-0.9375*q)+0.5),
                     texture(breakupTexture,centered*(4.0-3.0*q)+0.5),q*q);
        }
        else mask=texture(breakupTexture,fragLocalUV);
        // RGB_A 的 SUBTRACT 通道将遮罩 alpha 置零，保留累计透明度。
        if(card.breakup.y>0.5) mask.a=0.0;
        color=max(color-mask*clamp(fragCardData.x,0.0,1.0),vec4(0));
    }
    // 材质显式选择 tint 与 desaturation 的顺序，不改变默认组合。
    if(card.breakup.w>0.5) color.rgb*=fragColor.rgb;
    float luminance=dot(color.rgb,vec3(0.2125,0.7154,0.0721));
    color.rgb=mix(color.rgb,vec3(luminance),card.style.w);
    if(card.breakup.w<=0.5) color.rgb*=fragColor.rgb;
    color.rgb*=card.material.x;
    color.a=clamp(color.a*fragColor.a,0.0,1.0);
    vec2 screenUV=gl_FragCoord.xy/max(ScreenParams.xy,vec2(1));
#if VANS_CARD_SOFT_DEPTH
    float depth=texture(sceneDepth,screenUV).r;
    vec4 surface=InverseProjectionMatrix*vec4(screenUV*2.0-1.0,depth,1);
    float surfaceZ=-surface.z/surface.w;
    float particleZ=-(ViewMatrix*vec4(fragWorldPos,1)).z;
    color.a*=clamp((surfaceZ-particleZ)/max(card.style.y,0.0001),0.0,1.0);
#endif
    if(color.a<0.0001) discard;
    if(card.material.y>0.5)
    {
        vec3 ray=fragWorldPos-cameraPosition.xyz;
        float distanceMeters=length(ray);
        vec3 scattering,opticalDepth;
        ExtractViewMediaInterval(screenUV,ray/max(distanceMeters,0.00001),distanceMeters,scattering,opticalDepth);
        color.rgb*=exp(-min(opticalDepth,vec3(80)))*color.a;
        // RGB=C*a，输出 alpha=k*a。浮点目标的 One/InvSrcAlpha 混合
        // 保留源标量：Additive k=0、HalfBlendAdd k=.5、BlendAdd k=-1。
        color.a*=card.material.y>2.5 ? -1.0 : card.material.y>1.5 ? 0.5 : 0.0;
    }
    else color.rgb=CompositeAtmosphereSurfaceRadiance(screenUV,fragWorldPos,color.rgb);
    outColor=color;
}
