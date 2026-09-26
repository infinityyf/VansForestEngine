#pragma once

#include "../AssetCore/VansAssetGuid.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Vans
{
enum class VansPcgSplineKind { Road, River };
enum class VansPcgRoadRenderMode { Mesh, ProjectedDecal };
enum class VansPcgSplineTangentMode { Auto, Aligned, Mirrored, Broken };
enum class VansPcgSplineSegmentMode { Curve, Line };

struct VansPcgSplinePoint
{
    std::string id;
    std::array<float, 3> position{};
    // 切线是相对点位置的 Bezier 手柄，不再保存重复的旋转属性。
    std::array<float, 3> arrive{ -1, 0, 0 };
    std::array<float, 3> leave{ 1, 0, 0 };
    VansPcgSplineTangentMode tangentMode = VansPcgSplineTangentMode::Auto;
    VansPcgSplineSegmentMode outgoing = VansPcgSplineSegmentMode::Curve;
    float leftWidth = 3;
    float rightWidth = 3;
    bool linkedWidth = true;
    float bankAngleDegrees = 0;
    float depth = 2;
    // 0: full-width smooth bowl; larger values narrow the bank ramp and steepen it.
    float bankSteepness = 0;
    float speed = 2;
};

struct VansPcgSpline
{
    std::string id;
    std::string name;
    VansPcgSplineKind kind = VansPcgSplineKind::Road;
    VansPcgRoadRenderMode roadRenderMode = VansPcgRoadRenderMode::Mesh;
    bool enabled = true;
    bool locked = false;
    int priority = 0;
    VansAssetGuid material;
    // 投影道路独立材质；其 UV/采样语义与普通道路材质分开维护。
    VansAssetGuid roadDecalMaterial;
    bool excludeVegetation = false;
    float vegetationFade = 2;
    float shoulder = 3;
    float blendWidth = 1;
    float surfaceOffset = 0.025f;
    float projectedDepth = 2.0f;
    // 河流实际水位低于作者样条（岸沿）高度；深度从实际水面向下计算。
    float waterSurfaceDrop = 0.15f;
    // 水面与 Water Level 的混合宽度；首尾按点序，0 表示只在埋岸边界过渡。
    float waterBlendWidthMeters = 2;
    float waterBlendStartMeters = 0;
    float waterBlendEndMeters = 0;
    // 已在源高度图雕刻河床时关闭；水位、流速和湿岸仍独立生效。
    bool carveRiverbed = true;
    // 河床内部保持完整湿润，越过河岸后在该世界空间宽度内平滑衰减。
    float wetBankWidthMeters = 3.0f;
    float wetnessStrength = 0.85f;
    float textureRepeat = 4;
    int flowSign = 1;
    float fadeInDistance = 5;
    float fadeOutDistance = 5;
    // 连续段的坐标和端部包络独立于本段长度，拆分不会重新起算。
    float coordinateOffset = 0;
    float coordinateSign = 1;
    bool continuation = false;
    float envelopeOffset = 0;
    float envelopeLength = 0;
    bool normalFlowEnabled = true;
    std::vector<VansPcgSplinePoint> points;
};

struct VansPcgSplineAsset
{
    std::string name;
    VansAssetGuid terrain;
    float fieldTexelSize = 0.5f;
    float sampleSpacing = 0.5f;
    float curveTolerance = 0.025f;
    float heightConflictThreshold = 1;
    std::vector<VansPcgSpline> splines;

    std::vector<VansAssetGuid> Dependencies() const;
};

float MinimumPcgRiverTransitionWidth(float fieldTexelSize);
std::vector<std::string> ValidatePcgSplineAsset(const VansPcgSplineAsset& asset, bool requireReady);
}
