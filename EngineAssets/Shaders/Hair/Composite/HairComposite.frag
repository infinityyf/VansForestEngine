#version 450
layout(location=0) in vec2 fragUV;
layout(set=1,binding=0) uniform sampler2D hairColor;
layout(r32f,set=1,binding=1) uniform readonly image2D hairOpticalDepth;
layout(location=0) out vec4 outColor;
void main()
{
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 accumulation=texelFetch(hairColor,pixel,0);
    if(accumulation.a<=0.0001) discard;
    float opacity=1.0-exp(-imageLoad(hairOpticalDepth,pixel).r);
    outColor=vec4(accumulation.rgb/accumulation.a*opacity,opacity);
}
