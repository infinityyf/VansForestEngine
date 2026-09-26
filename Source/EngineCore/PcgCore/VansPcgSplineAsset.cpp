#include "VansPcgSplineAsset.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Vans
{
float MinimumPcgRiverTransitionWidth(float fieldTexelSize)
{
    return 4.0f * fieldTexelSize;
}

std::vector<VansAssetGuid> VansPcgSplineAsset::Dependencies() const
{
    std::vector<VansAssetGuid> result;
    if (terrain.IsValid()) result.push_back(terrain);
    for (const auto& spline : splines)
    {
        if (spline.material.IsValid() && std::find(result.begin(), result.end(), spline.material) == result.end())
            result.push_back(spline.material);
        if (spline.kind == VansPcgSplineKind::Road && spline.roadDecalMaterial.IsValid() &&
            std::find(result.begin(), result.end(), spline.roadDecalMaterial) == result.end())
            result.push_back(spline.roadDecalMaterial);
    }
    return result;
}

std::vector<std::string> ValidatePcgSplineAsset(const VansPcgSplineAsset& asset, bool requireReady)
{
    std::vector<std::string> errors;
    const auto range = [&](float value, float minimum, float maximum, const std::string& path) {
        if (!std::isfinite(value) || value < minimum || value > maximum)
            errors.push_back(path + " is outside its finite range.");
    };
    range(asset.fieldTexelSize, 0.05f, 32, "fieldTexelSize");
    range(asset.sampleSpacing, 0.05f, 16, "sampleSpacing");
    range(asset.curveTolerance, 0.001f, 1, "curveTolerance");
    range(asset.heightConflictThreshold, 0, 1000, "heightConflictThreshold");
    if (requireReady && !asset.terrain.IsValid()) errors.push_back("A terrain asset must be bound.");
    std::unordered_set<std::string> identities;
    for (const auto& spline : asset.splines)
    {
        const std::string path = "spline[" + spline.id + "]";
        if (spline.id.empty() || !identities.insert(spline.id).second) errors.push_back(path + " requires a unique ID.");
        if (spline.kind != VansPcgSplineKind::Road && spline.kind != VansPcgSplineKind::River)
            errors.push_back(path + " has an invalid kind.");
        if (spline.kind == VansPcgSplineKind::Road && spline.roadRenderMode != VansPcgRoadRenderMode::Mesh &&
            spline.roadRenderMode != VansPcgRoadRenderMode::ProjectedDecal)
            errors.push_back(path + ".roadRenderMode is invalid.");
        if (spline.kind == VansPcgSplineKind::Road && spline.roadRenderMode == VansPcgRoadRenderMode::ProjectedDecal &&
            !spline.roadDecalMaterial.IsValid())
            errors.push_back(path + ".roadDecalMaterial is required for projected decal roads.");
        range(spline.vegetationFade, 0.05f, 1000, path + ".vegetationFade");
        range(spline.shoulder, 0, 1000, path + ".shoulder");
        range(spline.blendWidth, 0.001f, 1000, path + ".blendWidth");
        range(spline.surfaceOffset, 0, 1, path + ".surfaceOffset");
        if (spline.kind == VansPcgSplineKind::Road)
            range(spline.projectedDepth, 0.05f, 100, path + ".projectedDepth");
        range(spline.waterSurfaceDrop, 0, 100, path + ".waterSurfaceDrop");
        if (spline.kind == VansPcgSplineKind::River)
        {
            range(spline.waterBlendWidthMeters, 0, 1000, path + ".waterBlendWidthMeters");
            range(spline.waterBlendStartMeters, 0, 1000000, path + ".waterBlendStartMeters");
            range(spline.waterBlendEndMeters, 0, 1000000, path + ".waterBlendEndMeters");
            range(spline.wetBankWidthMeters, MinimumPcgRiverTransitionWidth(asset.fieldTexelSize), 1000,
                path + ".wetBankWidthMeters");
            range(spline.wetnessStrength, 0, 1, path + ".wetnessStrength");
        }
        range(spline.textureRepeat, 0.01f, 10000, path + ".textureRepeat");
        range(spline.fadeInDistance, 0, 1000000, path + ".fadeInDistance");
        range(spline.fadeOutDistance, 0, 1000000, path + ".fadeOutDistance");
        range(spline.coordinateOffset, -10000000, 10000000, path + ".coordinateOffset");
        range(spline.envelopeOffset, 0, 10000000, path + ".envelopeOffset");
        range(spline.envelopeLength, 0, 10000000, path + ".envelopeLength");
        if (spline.flowSign != 1 && spline.flowSign != -1) errors.push_back(path + ".flowSign must be +1 or -1.");
        if (spline.coordinateSign != 1 && spline.coordinateSign != -1) errors.push_back(path + ".coordinateSign must be +1 or -1.");
        if (spline.continuation && spline.envelopeLength <= 0) errors.push_back(path + " requires a positive continuation envelope.");
        if (requireReady && spline.enabled && spline.points.size()>=2 && spline.kind == VansPcgSplineKind::Road && !spline.material.IsValid())
            errors.push_back(path + " requires a PBR material.");
        for (const auto& point : spline.points)
        {
            const auto pp = path + ".point[" + point.id + "]";
            if (point.id.empty() || !identities.insert(point.id).second) errors.push_back(pp + " requires a unique ID.");
            for (float value : point.position) range(value, -1000000, 1000000, pp + ".position");
            for (float value : point.arrive) range(value, -100000, 100000, pp + ".arrive");
            for (float value : point.leave) range(value, -100000, 100000, pp + ".leave");
            range(point.leftWidth, 0.05f, 1000, pp + ".leftWidth");
            range(point.rightWidth, 0.05f, 1000, pp + ".rightWidth");
            range(point.bankAngleDegrees, -60, 60, pp + ".bankAngleDegrees");
            range(point.depth, 0.01f, 1000, pp + ".depth");
            range(point.bankSteepness, 0, .9f, pp + ".bankSteepness");
            range(point.speed, 0, 100, pp + ".speed");
            if (point.linkedWidth && point.leftWidth != point.rightWidth) errors.push_back(pp + " linked widths differ.");
            if (point.tangentMode < VansPcgSplineTangentMode::Auto || point.tangentMode > VansPcgSplineTangentMode::Broken)
                errors.push_back(pp + " has an invalid tangent mode.");
            if (point.outgoing != VansPcgSplineSegmentMode::Curve && point.outgoing != VansPcgSplineSegmentMode::Line)
                errors.push_back(pp + " has an invalid segment mode.");
            if (spline.kind == VansPcgSplineKind::River && point.bankAngleDegrees != 0)
                errors.push_back(pp + " river cross sections must have a single water level.");
        }
    }
    return errors;
}
}
