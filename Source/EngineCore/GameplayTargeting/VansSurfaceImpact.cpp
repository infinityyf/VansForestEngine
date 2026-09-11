#include "VansSurfaceImpact.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include <cmath>

namespace Vans
{
VansSerializedValue VansEncodeSurfaceImpact(const VansSurfaceImpact& impact)
{
    const auto vector = [](const std::array<double, 3>& v) {
        return VansSerializedValue::Array({VansSerializedValue::Float(v[0]),
            VansSerializedValue::Float(v[1]), VansSerializedValue::Float(v[2])});
    };
    return VansSerializedValue::Object({
        {"kind", VansSerializedValue::Int(static_cast<int>(impact.kind))},
        {"entityIndex", VansSerializedValue::Int(impact.hit.hitEntity.index)},
        {"entityGeneration", VansSerializedValue::Int(impact.hit.hitEntity.generation)},
        {"componentGuid", VansSerializedValue::String(impact.hit.componentGuid)},
        {"position", vector(impact.hit.position)}, {"normal", vector(impact.hit.normal)},
        {"distance", VansSerializedValue::Float(impact.hit.distance)}
    });
}

bool VansDecodeSurfaceImpact(const VansSerializedValue& value, VansSurfaceImpact& impact, std::string& error)
{
    impact = {};
    const auto kind = ReadSerializedIntField(value, "kind", -1);
    if (kind < 0 || kind > static_cast<int>(VansSurfaceImpactKind::Unmapped))
    { error = "Surface impact kind is invalid"; return false; }
    impact.kind = static_cast<VansSurfaceImpactKind>(kind);
    if (impact.kind == VansSurfaceImpactKind::None) return true;
    const auto readVector = [&](const char* name, std::array<double, 3>& v) {
        const auto* field = FindObjectField(value, name);
        if (!field || field->kind != VansSerializedValue::Kind::Array || field->arrayItems.size() != 3) return false;
        for (size_t i = 0; i < 3; ++i)
        {
            const auto& item = field->arrayItems[i];
            if (item.kind != VansSerializedValue::Kind::Float && item.kind != VansSerializedValue::Kind::Int) return false;
            v[i] = ReadSerializedNumber(item);
            if (!std::isfinite(v[i]) || std::abs(v[i]) > 10000000.0) return false;
        }
        return true;
    };
    const auto index = ReadSerializedIntField(value, "entityIndex", -1);
    const auto generation = ReadSerializedIntField(value, "entityGeneration", -1);
    if (index < 0 || index > UINT32_MAX || generation < 0 || generation > UINT32_MAX ||
        !readVector("position", impact.hit.position) || !readVector("normal", impact.hit.normal))
    { error = "Surface impact identity or vectors are invalid"; return false; }
    impact.hit.hitEntity = {static_cast<uint32_t>(index), static_cast<uint32_t>(generation)};
    impact.hit.entity = impact.hit.hitEntity;
    impact.hit.componentGuid = ReadSerializedStringField(value, "componentGuid");
    const auto* distance = FindObjectField(value, "distance");
    impact.hit.distance = distance ? ReadSerializedNumber(*distance, -1) : -1;
    double normalLength = 0;
    for (double c : impact.hit.normal) normalLength += c*c;
    if (!std::isfinite(impact.hit.distance) || impact.hit.distance < 0 || impact.hit.distance > 1000000 ||
        normalLength < 0.99 || normalLength > 1.01 ||
        ((impact.kind == VansSurfaceImpactKind::Rigid || impact.kind == VansSurfaceImpactKind::Regional) &&
         (!impact.hit.hitEntity.IsValid() || impact.hit.componentGuid.empty())))
    { error = "Surface impact distance, normal or collider identity is invalid"; return false; }
    return true;
}
}
