#ifndef LOCAL_FOG_MEDIUM_COMMON_GLSL
#define LOCAL_FOG_MEDIUM_COMMON_GLSL

const uint LOCAL_FOG_SHAPE_ENABLED = 1u << 0u;
const uint LOCAL_FOG_DETAIL_ENABLED = 1u << 1u;
const uint LOCAL_FOG_FLOW_ENABLED = 1u << 2u;
const uint LOCAL_FOG_FLOW_HAS_TEXTURE = 1u << 3u;
const uint LOCAL_FOG_SHAPE_INVERT = 1u << 4u;
const uint LOCAL_FOG_DETAIL_INVERT = 1u << 5u;

bool LocalFogFeatureEnabled(uint flags, uint feature)
{
    return (flags & feature) != 0u;
}

float RemapLocalFogDensityLayer(
    float rawValue,
    float inputMinimum,
    float inputMaximum,
    bool invert)
{
    float value = clamp((rawValue - inputMinimum) /
        max(inputMaximum - inputMinimum, 1.0e-5), 0.0, 1.0);
    return invert ? 1.0 - value : value;
}

float EvaluateLocalFogDensityFactor(
    LocalFogVolume volume,
    vec3 localPosition,
    vec2 localXZFootprintMeters)
{
    uint flags = volume.fieldHandlesAndFlags.w;
    vec2 dimensionsMetersXZ = max(volume.dimensionsAndEdgeFade.xz, vec2(1.0e-4));
    float shapeFactor = 1.0;
    if (LocalFogFeatureEnabled(flags, LOCAL_FOG_SHAPE_ENABLED))
    {
        vec2 shapeTiling = max(volume.shapeTilingOffset.xy, vec2(1.0e-3));
        vec2 shapeUv = BuildLocalFogLocalXZUv(
            localPosition, shapeTiling, volume.shapeTilingOffset.zw);
        float shapeLod = ComputeLocalFogFieldLod(
            volume.fieldHandlesAndFlags.x, localXZFootprintMeters,
            dimensionsMetersXZ, shapeTiling, volume.shapeRemapInfluenceLod.w);
        float rawShape = SampleLocalFogScalar2D(
            volume.fieldHandlesAndFlags.x, shapeUv, shapeLod);
        float remappedShape = RemapLocalFogDensityLayer(
            rawShape,
            volume.shapeRemapInfluenceLod.x,
            volume.shapeRemapInfluenceLod.y,
            LocalFogFeatureEnabled(flags, LOCAL_FOG_SHAPE_INVERT));
        shapeFactor = mix(1.0, remappedShape,
            clamp(volume.shapeRemapInfluenceLod.z, 0.0, 1.0));
        if (shapeFactor <= 1.0e-6)
            return 0.0;
    }

    if (!LocalFogFeatureEnabled(flags, LOCAL_FOG_DETAIL_ENABLED))
        return shapeFactor;

    vec2 detailTiling = max(volume.detailTilingOffset.xy, vec2(1.0e-3));
    vec2 detailUv = BuildLocalFogLocalXZUv(
        localPosition, detailTiling, volume.detailTilingOffset.zw);
    float detailLod = ComputeLocalFogFieldLod(
        volume.fieldHandlesAndFlags.y, localXZFootprintMeters,
        dimensionsMetersXZ, detailTiling, volume.detailRemapInfluenceLod.w);
    float rawDetail;
    if (LocalFogFeatureEnabled(flags, LOCAL_FOG_FLOW_ENABLED) &&
        volume.flowSpeedDistancePhaseLod.x > 1.0e-6)
    {
        vec2 flowDirectionOrMagnitude;
        if (LocalFogFeatureEnabled(flags, LOCAL_FOG_FLOW_HAS_TEXTURE))
        {
            vec2 flowTiling = max(volume.flowTilingOffset.xy, vec2(1.0e-3));
            vec2 flowUv = BuildLocalFogLocalXZUv(
                localPosition, flowTiling, volume.flowTilingOffset.zw);
            float flowLod = ComputeLocalFogFieldLod(
                volume.fieldHandlesAndFlags.z, localXZFootprintMeters,
                dimensionsMetersXZ, flowTiling,
                volume.flowSpeedDistancePhaseLod.w);
            flowDirectionOrMagnitude = SampleLocalFogPlanarVector2D(
                volume.fieldHandlesAndFlags.z, flowUv, flowLod);
            flowDirectionOrMagnitude = ApplyFlowDecodeDeadZone(
                flowDirectionOrMagnitude,
                LocalFogFieldDecodeZeroThreshold(volume.fieldHandlesAndFlags.z));
            flowDirectionOrMagnitude =
                ClampLocalFogFlowLength(flowDirectionOrMagnitude);
        }
        else
        {
            flowDirectionOrMagnitude =
                volume.flowFallbackDirectionPadding.xy;
        }

        if (dot(flowDirectionOrMagnitude, flowDirectionOrMagnitude) > 1.0e-8)
        {
            vec2 uvPerMeter = detailTiling / dimensionsMetersXZ;
            if (!LocalFogFeatureEnabled(flags, LOCAL_FOG_FLOW_HAS_TEXTURE))
            {
                // 均匀 fallback direction 是纯平移：单次采样即可得到连续、
                // 可辨识的 noise 流动，不应使用会往复交叉淡化的双相位路径。
                vec2 flowUvOffset = BuildUniformFlowUvOffset(
                    FrameTime,
                    volume.flowSpeedDistancePhaseLod.x,
                    volume.flowSpeedDistancePhaseLod.z,
                    volume.flowSpeedDistancePhaseLod.y,
                    flowDirectionOrMagnitude,
                    uvPerMeter);
                rawDetail = SampleLocalFogScalar2D(
                    volume.fieldHandlesAndFlags.y,
                    detailUv - flowUvOffset,
                    detailLod);
            }
            else
            {
                // 空间变化的 flow map 采用有限距离双相位，避免速度梯度随时间
                // 无限拉伸 noise。第二层必须解相关，才能隐藏回绕且保留流向。
                LocalFogFlowAdvectionPhases phases = BuildFlowAdvectionPhases(
                    FrameTime,
                    volume.flowSpeedDistancePhaseLod.x,
                    volume.flowSpeedDistancePhaseLod.y,
                    volume.flowSpeedDistancePhaseLod.z);
                vec2 uv0 = detailUv - BuildFlowBacktraceOffset(
                    flowDirectionOrMagnitude, phases.phase0,
                    phases.phaseLengthMeters) * uvPerMeter;
                vec2 uv1 = OffsetSecondFlowLayerUv(
                    detailUv - BuildFlowBacktraceOffset(
                        flowDirectionOrMagnitude, phases.phase1,
                        phases.phaseLengthMeters) * uvPerMeter);
                float detail0 = SampleLocalFogScalar2D(
                    volume.fieldHandlesAndFlags.y, uv0, detailLod);
                float detail1 = SampleLocalFogScalar2D(
                    volume.fieldHandlesAndFlags.y, uv1, detailLod);
                rawDetail = mix(detail1, detail0, phases.blend);
            }
        }
        else
        {
            rawDetail = SampleLocalFogScalar2D(
                volume.fieldHandlesAndFlags.y, detailUv, detailLod);
        }
    }
    else
    {
        rawDetail = SampleLocalFogScalar2D(
            volume.fieldHandlesAndFlags.y, detailUv, detailLod);
    }

    float remappedDetail = RemapLocalFogDensityLayer(
        rawDetail,
        volume.detailRemapInfluenceLod.x,
        volume.detailRemapInfluenceLod.y,
        LocalFogFeatureEnabled(flags, LOCAL_FOG_DETAIL_INVERT));
    float detailFactor = mix(1.0, remappedDetail,
        clamp(volume.detailRemapInfluenceLod.z, 0.0, 1.0));
    return shapeFactor * detailFactor;
}

#endif
