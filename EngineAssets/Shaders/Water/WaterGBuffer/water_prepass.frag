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
layout(location = 7) in vec2 inRiverGeometryFilter;

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
    vec4 riverRendering;
} params;
layout(set = 1, binding = 6) uniform sampler2D detailNormalMap;
#include "river_wave_particles.glsl"

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

// 单个网格样本内只做刚体旋转和平移，空间变化只参与最终斜率混合。
vec2 RiverFlowCell(vec2 anchor,vec2 worldDx,vec2 worldDy,float distanceToCamera)
{
    float height;vec2 velocity;
    if(!PcgRiverSurface(anchor,height,velocity))return vec2(0);
    vec4 properties=PcgRiverProperties(anchor);
    float speed=length(velocity);
    vec2 along=speed>1e-5?velocity/speed:vec2(1,0);
    vec2 across=vec2(-along.y,along.x);
    // 在停滞点消退有方向的纹理，避免接近零的向量方向放大噪声。
    float directionalWeight=smoothstep(.015,.08,speed);
    vec2 result=vec2(0);
    for(int layer=0;layer<4;++layer)
    {
        if(params.detailLayerEnabled[layer]==0)continue;
        vec4 motion=params.detailLayerUvMotion[layer],strength=params.detailLayerStrengthFade[layer];
        vec2 direction=dot(motion.yz,motion.yz)>1e-8?normalize(motion.yz):vec2(1,0);
        vec2 u=along*direction.x+across*direction.y;
        vec2 v=-along*direction.y+across*direction.x;
        mat2 worldToUv=transpose(mat2(u,v))*max(motion.x,1e-6);
        // 锚点固定到世界网格；相位抖动固定到锚点，跨 PCG 页和镜头移动均连续。
        vec2 jitter=fract(sin(vec2(dot(anchor,vec2(12.9898,78.233)),dot(anchor,vec2(39.346,11.135))))*43758.5453);
        vec2 uv=worldToUv*(inWorldXZ-anchor-velocity*params.detailNormalGlobal.x*properties.y)+jitter+vec2(strength.y,0);
        mat2 gradient=worldToUv*exp2(params.detailNormalGlobal.w);
        vec2 slope=RiverNormalSlope(uv,gradient*worldDx,gradient*worldDy);
        float fade=1-smoothstep(strength.z,max(strength.w,strength.z+.01),distanceToCamera);
        result+=(u*slope.x+v*slope.y)*strength.x*fade;
    }
    return result*directionalWeight;
}

vec2 RiverFlowGrid(vec2 offset,vec2 worldDx,vec2 worldDy,float distanceToCamera)
{
    float spacing=max(params.riverRendering.x,.25);
    vec2 grid=inWorldXZ/spacing-offset;
    vec2 base=floor(grid),f=fract(grid);
    vec2 slope=vec2(0);
    for(int y=0;y<2;++y)for(int x=0;x<2;++x)
    {
        vec2 anchor=(base+vec2(x,y)+offset)*spacing;
        float weight=(x==0?1-f.x:f.x)*(y==0?1-f.y:f.y);
        slope+=weight*RiverFlowCell(anchor,worldDx,worldDy,distanceToCamera);
    }
    return slope;
}

vec3 BuildRiverDetailNormal(vec3 macroNormal,vec2 worldDx,vec2 worldDy)
{
    // 宏观法线来自实际波包位移，不能再覆盖成水位 Mask 的平面法线。
    vec3 normal=normalize(macroNormal);
    if(params.detailNormalFlags.x==0 || params.detailNormalFlags.y==0)return normal;
    float distanceToCamera=distance(inWorldPos,params.waterCameraPosition.xyz);
    vec2 slope=.5*(RiverFlowGrid(vec2(0),worldDx,worldDy,distanceToCamera)+
        RiverFlowGrid(vec2(.5),worldDx,worldDy,distanceToCamera));
    slope*=params.detailNormalGlobal.y*PcgRiverProperties(inWorldXZ).x;
    vec3 perturbation=vec3(slope.x,0,slope.y);
    perturbation-=normal*dot(normal,perturbation);
    float magnitude=length(perturbation),limit=max(params.detailNormalGlobal.z,.01);
    if(magnitude>limit)perturbation*=limit/magnitude;
    return normalize(normal+perturbation);
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
    // 在区域分支前求屏幕导数，网格采样循环只使用显式梯度。
    vec2 worldDx=dFdx(inWorldXZ),worldDy=dFdy(inWorldXZ);
    vec3 blend=PcgWaterBlend(inWorldXZ);
    float riverWeight=blend.x;
    vec3 base=normalize(inWorldNormal);
    if(riverWeight>0 && params.riverRendering.z>0)
    {
        float pixelFootprint=max(max(length(worldDx),length(worldDy)),1e-4);
        vec3 fineWave=RiverWaveHeightGradient(inWorldXZ,pixelFootprint,inRiverGeometryFilter);
        // 未被几何解析的波高也服从同一混合权重，保留对应的乘积法则项。
        vec2 fineSlope=(riverWeight*fineWave.yz+fineWave.x*blend.yz)*params.riverRendering.z;
        fineSlope/=sqrt(1+dot(fineSlope,fineSlope)/.36);
        base=normalize(vec3(base.x-fineSlope.x*base.y,base.y,base.z-fineSlope.y*base.y));
    }
    vec3 finalNormal;
    if(riverWeight>=1)finalNormal=BuildRiverDetailNormal(base,worldDx,worldDy);
    else if(riverWeight<=0)finalNormal=BuildDetailNormal(base,worldDx,worldDy);
    else
    {
        // 两套细节在同一宏观法线切平面内混合斜率，不重复叠加宏观坡度。
        vec3 riverNormal=BuildRiverDetailNormal(base,worldDx,worldDy);
        vec3 oceanNormal=BuildDetailNormal(base,worldDx,worldDy);
        finalNormal=normalize(mix(oceanNormal/max(dot(oceanNormal,base),1e-3),
            riverNormal/max(dot(riverNormal,base),1e-3),riverWeight));
    }
    float roughness = ComputeEffectiveRoughness();
    float foam = 0.0;
    outWaterNormal = vec4(finalNormal, roughness);
    outWaterScatterThickness = vec4(max(params.scatteringCoeff.rgb, vec3(0.0)), 0.0);
    outWaterAbsorptionFoam = vec4(max(params.absorptionCoeff.rgb, vec3(0.0)), foam);
    outWaterPosDepth = vec4(inWorldPos, inLinearDepth);
}
