#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/PcgSplineFields.glsl"

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in float inLinearDepth;
layout(location = 2) in vec3 inWorldNormal;
layout(location = 3) flat in int inLodLevel;
layout(location = 4) in vec2 inWorldXZ;
layout(location = 5) in vec3 inMacroDPdx;
layout(location = 6) in vec3 inMacroDPdz;

layout(set = 1, binding = 0) uniform WaterSurfaceParams
{
    mat4 waterVPMatrix;
    mat4 waterViewMatrix;
    vec4 waterCameraPosition;
    ivec4 geometryParams;
    vec4 geometryScale;
    vec4 spectrumScale;
    vec4 windAndChop;
    ivec4 simulationParams;
    vec4 waveParticleParams0;
    vec4 waveParticleParams1;
    vec4 flowMapWorld;
    vec4 flowMapParams;
    vec4 flowMapFallback;
    vec4 surfaceOptics;
    vec4 scatteringCoeff;
    vec4 absorptionCoeff;
    ivec4 detailNormalFlags;
    vec4 detailNormalGlobal;
    vec4 detailLayerUvMotion[4];
    vec4 detailLayerStrengthFade[4];
    ivec4 detailLayerEnabled;
    vec4 effectiveRoughnessParams;
} params;
layout(set = 1, binding = 6) uniform sampler2D detailNormalMap;

layout(location = 0) out vec4 outWaterNormal;
layout(location = 1) out vec4 outWaterScatterThickness;
layout(location = 2) out vec4 outWaterAbsorptionFoam;
layout(location = 3) out vec4 outWaterPosDepth;

vec3 BuildFallbackTangent(vec3 normal)
{
    vec3 axis = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    return normalize(cross(axis, normal));
}

vec3 BuildDetailNormal(vec3 macroNormal,vec2 worldDx,vec2 worldDy)
{
    vec3 normal = normalize(macroNormal);
    vec3 tangentCandidate = inMacroDPdx - normal * dot(normal, inMacroDPdx);
    vec3 tangent = dot(tangentCandidate, tangentCandidate) > 1e-8
        ? normalize(tangentCandidate)
        : BuildFallbackTangent(normal);
    vec3 bitangent = normalize(cross(normal, tangent));
    if (dot(bitangent, inMacroDPdz) < 0.0)
        bitangent = -bitangent;

    if (params.detailNormalFlags.x == 0 || params.detailNormalFlags.y == 0)
        return normal;

    float gradientScale = exp2(params.detailNormalGlobal.w);
    float distanceToCamera = distance(inWorldPos, params.waterCameraPosition.xyz);
    vec2 accumulatedSlope = vec2(0.0);

    for (int layerIndex = 0; layerIndex < 4; ++layerIndex)
    {
        if (params.detailLayerEnabled[layerIndex] == 0)
            continue;

        vec4 uvMotion = params.detailLayerUvMotion[layerIndex];
        vec4 strengthFade = params.detailLayerStrengthFade[layerIndex];
        vec2 direction = uvMotion.yz;
        float directionLengthSq = dot(direction, direction);
        direction = directionLengthSq > 1e-8
            ? direction * inversesqrt(directionLengthSq)
            : vec2(1.0, 0.0);
        vec2 perpendicular = vec2(-direction.y, direction.x);
        float inverseTileSize = max(uvMotion.x, 1e-6);

        vec2 uv = vec2(
            dot(inWorldXZ, direction),
            dot(inWorldXZ, perpendicular)) * inverseTileSize;
        uv.x += (uvMotion.w * params.detailNormalGlobal.x) * inverseTileSize
            + strengthFade.y;
        vec2 uvDx = vec2(dot(worldDx, direction), dot(worldDx, perpendicular))
            * inverseTileSize * gradientScale;
        vec2 uvDy = vec2(dot(worldDy, direction), dot(worldDy, perpendicular))
            * inverseTileSize * gradientScale;

        vec2 encodedXY = textureGrad(detailNormalMap, uv, uvDx, uvDy).rg * 2.0 - 1.0;
        if (params.detailNormalFlags.w != 0)
            encodedXY.y = -encodedXY.y;
        float encodedLength = length(encodedXY);
        if (encodedLength > 0.998)
            encodedXY *= 0.998 / encodedLength;
        float reconstructedZ = sqrt(max(1.0 - dot(encodedXY, encodedXY), 1e-5));
        vec2 slope = encodedXY / max(reconstructedZ, 1e-3);
        float distanceFade = 1.0 - smoothstep(
            strengthFade.z,
            max(strengthFade.w, strengthFade.z + 0.01),
            distanceToCamera);
        accumulatedSlope += slope * strengthFade.x
            * params.detailNormalGlobal.y * distanceFade;
    }

    float slopeLength = length(accumulatedSlope);
    float maxSlope = max(params.detailNormalGlobal.z, 0.01);
    if (slopeLength > maxSlope)
        accumulatedSlope *= maxSlope / slopeLength;
    return normalize(
        normal + tangent * accumulatedSlope.x + bitangent * accumulatedSlope.y);
}

vec2 RiverNormalSlope(vec2 uv,vec2 dx,vec2 dy)
{
    vec2 xy=textureGrad(detailNormalMap,uv,dx,dy).rg*2.0-1.0;
    if(params.detailNormalFlags.w!=0)xy.y=-xy.y;
    float magnitude=length(xy);if(magnitude>.998)xy*=.998/magnitude;
    return xy/max(sqrt(max(1.0-dot(xy,xy),1e-5)),1e-3);
}

vec3 BuildRiverDetailNormal(float height,vec2 velocity,vec2 worldDx,vec2 worldDy)
{
    vec2 heightGradient=PcgRiverHeightGradient(inWorldXZ,height);
    vec3 normal=normalize(vec3(-heightGradient.x,1,-heightGradient.y));
    if(params.detailNormalFlags.x==0 || params.detailNormalFlags.y==0)return normal;
    uvec4 page;vec2 local;if(!PcgLocate(inWorldXZ,page,local))return normal;
    float texelSize=uintBitsToFloat(pcgMetadata[0].x)/float(pcgMetadata[0].y);
    float unusedHeight;vec2 vx0,vx1,vz0,vz1;
    PcgRiverSurface(inWorldXZ-vec2(texelSize,0),unusedHeight,vx0);
    PcgRiverSurface(inWorldXZ+vec2(texelSize,0),unusedHeight,vx1);
    PcgRiverSurface(inWorldXZ-vec2(0,texelSize),unusedHeight,vz0);
    PcgRiverSurface(inWorldXZ+vec2(0,texelSize),unusedHeight,vz1);
    vec3 totalPerturbation=vec3(0);float weightSum=0;
    float distanceToCamera=distance(inWorldPos,params.waterCameraPosition.xyz);
    for(uint domain=0u;domain<page.z;++domain)
    {
        uvec4 info=pcgMetadata[page.y+domain];
        vec4 coordinates=PcgCoordinates(info.x,local);float weight=coordinates.z;
        if(weight<=0)continue;
        mat2 J=PcgJacobian(info.x,local);
        if(abs(determinant(J))<1e-6)continue;
        mat2 inverseJ=inverse(J);
        vec2 xu=inverseJ[0],xv=inverseJ[1];
        vec3 tangent=normalize(vec3(xu.x,dot(heightGradient,xu),xu.y));
        vec3 bitangent=normalize(cross(normal,tangent));
        if(dot(bitangent,vec3(xv.x,dot(heightGradient,xv),xv.y))<0)bitangent=-bitangent;
        vec2 qDot=info.z!=0u?J*velocity:vec2(0);
        vec2 qDotX=info.z!=0u?(PcgJacobian(info.x,local+vec2(1,0))*vx1-PcgJacobian(info.x,local-vec2(1,0))*vx0)/(2*texelSize):vec2(0);
        vec2 qDotZ=info.z!=0u?(PcgJacobian(info.x,local+vec2(0,1))*vz1-PcgJacobian(info.x,local-vec2(0,1))*vz0)/(2*texelSize):vec2(0);
        mat2 velocityGradient=mat2(qDotX,qDotZ);
        float cycle=max(uintBitsToFloat(info.y),.05);
        float phase0=fract(params.detailNormalGlobal.x/cycle);
        float phase1=fract(phase0+.5);
        float phaseWeight=pow(sin(3.14159265359*phase0),2.0);
        vec2 times=(vec2(phase0,phase1)-.5)*cycle;
        vec3 perturbation=vec3(0);
        for(int layer=0;layer<4;++layer)
        {
            if(params.detailLayerEnabled[layer]==0)continue;
            vec4 motion=params.detailLayerUvMotion[layer],strength=params.detailLayerStrengthFade[layer];
            vec2 direction=dot(motion.yz,motion.yz)>1e-8?normalize(motion.yz):vec2(1,0);
            vec2 perpendicular=vec2(-direction.y,direction.x);
            mat2 R=mat2(direction.x,perpendicular.x,direction.y,perpendicular.y);
            mat2 M=R*max(motion.x,1e-6);
            vec2 offset=vec2(strength.y,0);
            vec2 uv0=M*(coordinates.xy-qDot*times.x)+offset;
            vec2 uv1=M*(coordinates.xy-qDot*times.y)+offset;
            mat2 gradient0=M*(J-times.x*velocityGradient)*exp2(params.detailNormalGlobal.w);
            mat2 gradient1=M*(J-times.y*velocityGradient)*exp2(params.detailNormalGlobal.w);
            vec2 slope=mix(RiverNormalSlope(uv1,gradient1*worldDx,gradient1*worldDy),
                RiverNormalSlope(uv0,gradient0*worldDx,gradient0*worldDy),phaseWeight);
            vec3 textureTangent=tangent*direction.x+bitangent*direction.y;
            vec3 textureBitangent=tangent*perpendicular.x+bitangent*perpendicular.y;
            float fade=1.0-smoothstep(strength.z,max(strength.w,strength.z+.01),distanceToCamera);
            perturbation+=(textureTangent*slope.x+textureBitangent*slope.y)*strength.x*params.detailNormalGlobal.y*fade;
        }
        totalPerturbation+=weight*perturbation;weightSum+=weight;
    }
    if(weightSum<=0)return normal;
    totalPerturbation/=weightSum;
    float magnitude=length(totalPerturbation),limit=max(params.detailNormalGlobal.z,.01);
    if(magnitude>limit)totalPerturbation*=limit/magnitude;
    return normalize(normal+totalPerturbation);
}

float ComputeEffectiveRoughness()
{
    float baseRoughness = clamp(params.surfaceOptics.x, 0.002, 1.0);
    if (int(params.effectiveRoughnessParams.x + 0.5) == 0)
        return baseRoughness;

    float distanceWeight = smoothstep(
        params.effectiveRoughnessParams.y,
        max(params.effectiveRoughnessParams.z, params.effectiveRoughnessParams.y + 0.01),
        inLinearDepth);
    float distanceRoughness = distanceWeight * params.effectiveRoughnessParams.w;
    return clamp(sqrt(
        baseRoughness * baseRoughness + distanceRoughness * distanceRoughness),
        0.002,
        1.0);
}

void main()
{
    // 在区域分支前求屏幕导数，重叠域循环只使用显式梯度。
    vec2 worldDx=dFdx(inWorldXZ),worldDy=dFdy(inWorldXZ);
    float riverHeight;vec2 riverVelocity;
    bool river=PcgRiverSurface(inWorldXZ,riverHeight,riverVelocity);
    vec3 finalNormal = river?BuildRiverDetailNormal(riverHeight,riverVelocity,worldDx,worldDy):
        BuildDetailNormal(inWorldNormal,worldDx,worldDy);
    float roughness = ComputeEffectiveRoughness();
    float foam = 0.0;
    outWaterNormal = vec4(finalNormal, roughness);
    outWaterScatterThickness = vec4(max(params.scatteringCoeff.rgb, vec3(0.0)), 0.0);
    outWaterAbsorptionFoam = vec4(max(params.absorptionCoeff.rgb, vec3(0.0)), foam);
    outWaterPosDepth = vec4(inWorldPos, inLinearDepth);
}
