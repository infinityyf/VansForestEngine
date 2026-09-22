#include "VansPcgSplineAssetCodec.h"
#include "VansPcgValueCodec.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include <nlohmann/json.hpp>

namespace Vans
{
namespace
{
using Value = VansSerializedValue;
const char* TangentName(VansPcgSplineTangentMode mode)
{
    switch(mode) {
    case VansPcgSplineTangentMode::Auto:return "auto";
    case VansPcgSplineTangentMode::Aligned:return "aligned";
    case VansPcgSplineTangentMode::Mirrored:return "mirrored";
    case VansPcgSplineTangentMode::Broken:return "broken";
    }
    return "";
}
const char* RoadRenderModeName(VansPcgRoadRenderMode mode)
{
    return mode==VansPcgRoadRenderMode::ProjectedDecal?"projectedDecal":"mesh";
}
}
bool VansPcgSplineAssetCodec::Decode(const Value& root, VansPcgSplineAsset& asset, std::string& error)
{
    error.clear(); VansPcgSplineAsset decoded;
    PcgValue::Reader reader(&root,"splineAsset",error);
    reader.String("name",decoded.name); reader.Reference("terrain","terrain",decoded.terrain);
    reader.Float("fieldTexelSize",decoded.fieldTexelSize); reader.Float("sampleSpacing",decoded.sampleSpacing);
    reader.Float("curveTolerance",decoded.curveTolerance); reader.Float("heightConflictThreshold",decoded.heightConflictThreshold);
    if (const auto* list=reader.Array("splines")) for (const auto& item:*list)
    {
        VansPcgSpline s; PcgValue::Reader r(&item,"spline",error);
        r.String("id",s.id); r.String("name",s.name);
        r.EnumField("kind",s.kind,{{"road",VansPcgSplineKind::Road},{"river",VansPcgSplineKind::River}});
        if (s.kind==VansPcgSplineKind::Road)
            r.OptionalEnumField("roadRenderMode",s.roadRenderMode,{{"mesh",VansPcgRoadRenderMode::Mesh},{"projectedDecal",VansPcgRoadRenderMode::ProjectedDecal}});
        r.Bool("enabled",s.enabled); r.Bool("locked",s.locked); r.IntegerField("priority",s.priority);
        r.Reference("material","material",s.material);
        if (s.kind==VansPcgSplineKind::Road) r.OptionalReference("roadDecalMaterial","material",s.roadDecalMaterial);
        r.Bool("excludeVegetation",s.excludeVegetation); r.Float("vegetationFade",s.vegetationFade);
        r.Float("shoulder",s.shoulder); r.Float("blendWidth",s.blendWidth);
        r.Float("waterSurfaceDrop",s.waterSurfaceDrop);
        if (s.kind==VansPcgSplineKind::River)
        {
            r.Float("waterBlendWidthMeters",s.waterBlendWidthMeters);
            r.Float("waterBlendStartMeters",s.waterBlendStartMeters);
            r.Float("waterBlendEndMeters",s.waterBlendEndMeters);
            r.Bool("carveRiverbed",s.carveRiverbed);
            r.Float("wetBankWidthMeters",s.wetBankWidthMeters);
            r.Float("wetnessStrength",s.wetnessStrength);
        }
        r.Float("surfaceOffset",s.surfaceOffset); r.Float("textureRepeat",s.textureRepeat);
        if (s.kind==VansPcgSplineKind::Road) r.OptionalFloat("projectedDepth",s.projectedDepth);
        r.IntegerField("flowSign",s.flowSign); r.Float("fadeInDistance",s.fadeInDistance); r.Float("fadeOutDistance",s.fadeOutDistance);
        r.Float("coordinateOffset",s.coordinateOffset); r.Float("coordinateSign",s.coordinateSign);
        r.Bool("continuation",s.continuation); r.Float("envelopeOffset",s.envelopeOffset); r.Float("envelopeLength",s.envelopeLength);
        r.Bool("normalFlowEnabled",s.normalFlowEnabled);
        if (const auto* points=r.Array("points")) for (const auto& value:*points)
        {
            VansPcgSplinePoint p; PcgValue::Reader k(&value,"spline["+s.id+"].point",error);
            k.String("id",p.id); k.Vector("position",p.position); k.Vector("arrive",p.arrive); k.Vector("leave",p.leave);
            k.EnumField("tangentMode",p.tangentMode,{{"auto",VansPcgSplineTangentMode::Auto},
                {"aligned",VansPcgSplineTangentMode::Aligned},{"mirrored",VansPcgSplineTangentMode::Mirrored},{"broken",VansPcgSplineTangentMode::Broken}});
            k.EnumField("outgoing",p.outgoing,{{"curve",VansPcgSplineSegmentMode::Curve},{"line",VansPcgSplineSegmentMode::Line}});
            k.Float("leftWidth",p.leftWidth); k.Float("rightWidth",p.rightWidth); k.Bool("linkedWidth",p.linkedWidth);
            k.Float("bankAngleDegrees",p.bankAngleDegrees); k.Float("depth",p.depth); k.Float("speed",p.speed);
            k.Float("bankSteepness",p.bankSteepness);
            k.Finish(); s.points.push_back(std::move(p));
        }
        r.Finish(); decoded.splines.push_back(std::move(s));
    }
    if (!reader.Finish()) return false;
    const auto errors=ValidatePcgSplineAsset(decoded,false);
    if (!errors.empty()) {error=errors.front();return false;}
    asset=std::move(decoded); return true;
}

bool VansPcgSplineAssetCodec::Encode(const VansPcgSplineAsset& asset, Value& root, std::string& error)
{
    error.clear(); const auto errors=ValidatePcgSplineAsset(asset,false);
    if (!errors.empty()) {error=errors.front();return false;}
    std::vector<Value> splines;
    for (const auto& s:asset.splines)
    {
        std::vector<Value> points;
        for (const auto& p:s.points) points.push_back(Value::Object({
            {"id",Value::String(p.id)},{"position",PcgValue::Vector(p.position)},
            {"arrive",PcgValue::Vector(p.arrive)},{"leave",PcgValue::Vector(p.leave)},
            {"tangentMode",Value::String(TangentName(p.tangentMode))},
            {"outgoing",Value::String(p.outgoing==VansPcgSplineSegmentMode::Curve?"curve":"line")},
            {"leftWidth",Value::Float(p.leftWidth)},{"rightWidth",Value::Float(p.rightWidth)},
            {"linkedWidth",Value::Bool(p.linkedWidth)},{"bankAngleDegrees",Value::Float(p.bankAngleDegrees)},
            {"depth",Value::Float(p.depth)},{"bankSteepness",Value::Float(p.bankSteepness)},{"speed",Value::Float(p.speed)}}));
        std::vector<std::pair<std::string,Value>> splineFields={
            {"id",Value::String(s.id)},{"name",Value::String(s.name)},
            {"kind",Value::String(s.kind==VansPcgSplineKind::Road?"road":"river")},
            {"enabled",Value::Bool(s.enabled)},{"locked",Value::Bool(s.locked)},{"priority",Value::Int(s.priority)},
            {"material",PcgValue::Reference(s.material,"material")},
            {"excludeVegetation",Value::Bool(s.excludeVegetation)},{"vegetationFade",Value::Float(s.vegetationFade)},
            {"shoulder",Value::Float(s.shoulder)},{"blendWidth",Value::Float(s.blendWidth)},
            {"waterSurfaceDrop",Value::Float(s.waterSurfaceDrop)}};
        if (s.kind==VansPcgSplineKind::Road)
        {
            splineFields.emplace_back("roadDecalMaterial",PcgValue::Reference(s.roadDecalMaterial,"material"));
            splineFields.emplace_back("roadRenderMode",Value::String(RoadRenderModeName(s.roadRenderMode)));
            splineFields.emplace_back("projectedDepth",Value::Float(s.projectedDepth));
        }
        if (s.kind==VansPcgSplineKind::River)
        {
            splineFields.emplace_back("waterBlendWidthMeters",Value::Float(s.waterBlendWidthMeters));
            splineFields.emplace_back("waterBlendStartMeters",Value::Float(s.waterBlendStartMeters));
            splineFields.emplace_back("waterBlendEndMeters",Value::Float(s.waterBlendEndMeters));
            splineFields.emplace_back("carveRiverbed",Value::Bool(s.carveRiverbed));
            splineFields.emplace_back("wetBankWidthMeters",Value::Float(s.wetBankWidthMeters));
            splineFields.emplace_back("wetnessStrength",Value::Float(s.wetnessStrength));
        }
        splineFields.insert(splineFields.end(),{
            {"surfaceOffset",Value::Float(s.surfaceOffset)},{"textureRepeat",Value::Float(s.textureRepeat)},
            {"flowSign",Value::Int(s.flowSign)},{"fadeInDistance",Value::Float(s.fadeInDistance)},{"fadeOutDistance",Value::Float(s.fadeOutDistance)},
            {"coordinateOffset",Value::Float(s.coordinateOffset)},{"coordinateSign",Value::Float(s.coordinateSign)},
            {"continuation",Value::Bool(s.continuation)},{"envelopeOffset",Value::Float(s.envelopeOffset)},{"envelopeLength",Value::Float(s.envelopeLength)},
            {"normalFlowEnabled",Value::Bool(s.normalFlowEnabled)},
            {"points",Value::Array(std::move(points))}});
        splines.push_back(Value::Object(std::move(splineFields)));
    }
    root=Value::Object({{"name",Value::String(asset.name)},{"terrain",PcgValue::Reference(asset.terrain,"terrain")},
        {"fieldTexelSize",Value::Float(asset.fieldTexelSize)},{"sampleSpacing",Value::Float(asset.sampleSpacing)},
        {"curveTolerance",Value::Float(asset.curveTolerance)},{"heightConflictThreshold",Value::Float(asset.heightConflictThreshold)},
        {"splines",Value::Array(std::move(splines))}});
    return true;
}

std::uint64_t VansPcgSplineAssetCodec::ContentHash(const VansPcgSplineAsset& asset)
{
    Value root; std::string error;
    if (!Encode(asset,root,error)) return 0;
    const auto text=EncodeSerializedValueJson<nlohmann::ordered_json>(root).dump();
    std::uint64_t hash=14695981039346656037ull;
    for (const unsigned char c:text) {hash^=c;hash*=1099511628211ull;}
    return hash;
}
}
