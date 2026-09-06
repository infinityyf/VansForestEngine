#ifndef LOCAL_FOG_FLOW_ADVECTION_GLSL
#define LOCAL_FOG_FLOW_ADVECTION_GLSL

struct LocalFogFlowAdvectionPhases
{
    float phase0;
    float phase1;
    float blend;
    float phaseLengthMeters;
};

// 均匀 fallback flow 不会造成流场拉伸，因此直接连续平流 Repeat 的
// Detail Noise。按 UV 周期回绕时间位移，既保持连续，也避免长时间运行后
// 大数 UV 丢失精度。
vec2 BuildUniformFlowUvOffset(
    float timeSeconds,
    float speedMetersPerSecond,
    float phaseOffset01,
    float phaseLengthMeters,
    vec2 flowDirection,
    vec2 uvPerMeter)
{
    float initialDistanceMeters = fract(phaseOffset01) *
        max(phaseLengthMeters, 0.01);
    float travelledDistanceMeters = max(timeSeconds, 0.0) *
        max(speedMetersPerSecond, 0.0) + initialDistanceMeters;
    return fract(flowDirection * travelledDistanceMeters * uvPerMeter);
}

LocalFogFlowAdvectionPhases BuildFlowAdvectionPhases(
    float timeSeconds,
    float speedMetersPerSecond,
    float loopDistanceMeters,
    float phaseOffset01)
{
    LocalFogFlowAdvectionPhases result;
    result.phaseLengthMeters = max(loopDistanceMeters, 0.01);
    result.phase0 = fract(timeSeconds * max(speedMetersPerSecond, 0.0) /
        result.phaseLengthMeters + phaseOffset01);
    result.phase1 = fract(result.phase0 + 0.5);
    result.blend = abs(result.phase0 * 2.0 - 1.0);
    return result;
}

vec2 BuildFlowBacktraceOffset(
    vec2 flowDirectionOrMagnitude,
    float phase,
    float phaseLengthMeters)
{
    return flowDirectionOrMagnitude * phase * phaseLengthMeters;
}

// 双层流场平流必须让两层使用不同的纹理起点；否则两层在半周期后回到
// 同一个图案，只会产生往复呼吸，无法形成可辨识的定向流动。
vec2 OffsetSecondFlowLayerUv(vec2 uv)
{
    return uv + vec2(0.5);
}

vec2 ApplyFlowDecodeDeadZone(vec2 flowVector, float zeroThreshold)
{
    return vec2(
        abs(flowVector.x) <= zeroThreshold ? 0.0 : flowVector.x,
        abs(flowVector.y) <= zeroThreshold ? 0.0 : flowVector.y);
}

vec2 ClampLocalFogFlowLength(vec2 flowVector)
{
    float magnitude = length(flowVector);
    return magnitude > 1.0 ? flowVector / magnitude : flowVector;
}

#endif
