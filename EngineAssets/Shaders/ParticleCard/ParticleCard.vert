#version 450
#extension GL_GOOGLE_include_directive : require
#include "../Common/CameraData.glsl"
layout(location=0) in vec2 inLocalPos;
layout(location=1) in vec2 inUV;
layout(location=2) in vec3 instWorldPos;
layout(location=3) in float instSize;
layout(location=4) in vec4 instColor;
layout(location=5) in float instRotation;
layout(location=6) in float instFrameIndex;
layout(location=7) in uvec2 instCardData;
layout(push_constant) uniform CardParameters { vec4 sheet; vec4 style; vec4 material; vec4 breakup; } card;
layout(location=0) out vec4 fragColor;
layout(location=1) out vec2 fragUV;
layout(location=2) out vec3 fragWorldPos;
layout(location=3) out vec2 fragNextUV;
layout(location=4) out float fragPhase;
layout(location=5) out vec2 fragLocalUV;
layout(location=6) out vec2 fragCardData;
vec3 DecodeNormal(vec2 oct)
{
    vec3 n=vec3(oct,1.0-abs(oct.x)-abs(oct.y));
    if(n.z<0.0) n.xy=(1.0-abs(n.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(n.xy,vec2(0)));
    return normalize(n);
}
vec2 SheetUV(float frame)
{
    float columns=max(card.sheet.x,1.0);
    float bounded=clamp(frame,0.0,columns*max(card.sheet.y,1.0)-1.0);
    return (inUV+vec2(mod(bounded,columns),floor(bounded/columns)))/max(card.sheet.xy,vec2(1));
}
void main()
{
    vec3 right=vec3(ViewMatrix[0][0],ViewMatrix[1][0],ViewMatrix[2][0]);
    vec3 up=vec3(ViewMatrix[0][1],ViewMatrix[1][1],ViewMatrix[2][1]);
    vec3 normal=DecodeNormal(unpackSnorm2x16(instCardData.x));
    if(card.sheet.z>0.5)
    {
        if(card.sheet.z>1.5)
        {
            up=vec3(0,1,0)-normal*normal.y;
            if(dot(up,up)<0.00001) up=vec3(0,0,-1)-normal*(-normal.z);
            up=normalize(up); right=normalize(cross(up,normal));
        }
        else
        {
            right=cross(normal,cameraPosition.xyz-instWorldPos);
            if(dot(right,right)<0.00001) right=cross(normal,abs(normal.z)<0.9 ? vec3(0,0,1) : vec3(1,0,0));
            right=normalize(right); up=normal;
        }
    }
    float c=cos(instRotation),s=sin(instRotation);
    vec2 local=vec2(c*inLocalPos.x-s*inLocalPos.y,s*inLocalPos.x+c*inLocalPos.y);
    local.y+=card.sheet.w;
    vec3 world=instWorldPos+instSize*(right*local.x+up*local.y/max(card.style.x,0.01));
    gl_Position=VPMatrix*vec4(world,1);
    fragUV=SheetUV(floor(instFrameIndex));
    fragNextUV=SheetUV(floor(instFrameIndex)+1.0);
    fragPhase=fract(instFrameIndex);
    fragWorldPos=world;
    fragColor=instColor;
    fragLocalUV=inUV;
    fragCardData=unpackHalf2x16(instCardData.y);
}
