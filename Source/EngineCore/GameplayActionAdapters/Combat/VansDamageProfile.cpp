#include "VansDamageProfile.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include <cmath>

namespace Vans
{
bool VansRegisterCombatGameplayAssetSchemas(VansGameplayAssetSchemaRegistry& registry, std::string& error)
{
    using K = VansGameplayPropertyKind;
    using V = VansSerializedValue;
    VansGameplayAssetSchemaDescriptor schema;
    schema.assetType = VansAssetType::DamageProfile;
    schema.assetKind = "DamageProfile";
    if (const VansAssetTypeDescriptor* assetType = VansAssetDatabase::Describe(schema.assetType))
        schema.extension = assetType->canonicalExtension;
    const auto field = [](std::string path, K kind, V value)
    {
        VansGameplayPropertySchema f;
        f.fieldId = VansMakeStableId<VansActionFieldIdTag>(path);
        f.path = path; f.displayName = path.substr(1); f.group = "Damage";
        f.kind = kind; f.defaultValue = std::move(value); f.required = true;
        return f;
    };
    schema.fields = {
        field("/assetKind", K::String, V::String("DamageProfile")),
        field("/damageId", K::String, V::String("")),
        field("/displayName", K::String, V::String("Damage")),
        field("/healthAttribute", K::String, V::String("Health")),
        field("/baseDamage", K::Float, V::Float(0)),
        field("/rangeModifier", K::Float, V::Float(1)),
        field("/rangeStepMeters", K::Float, V::Float(1)),
        field("/deathImpulse", K::Float, V::Float(0)),
        field("/regions", K::Array, V::Array({}))
    };
    auto& regions = schema.fields.back();
    regions.hasArrayElement = true; regions.arrayElementKind = K::Object;
    regions.arrayElementDefault = V::Object({{"region", V::String("")}, {"multiplier", V::Float(1)}});
    auto name = field("/regions/region", K::String, V::String("")); name.path = "region";
    auto multiplier = field("/regions/multiplier", K::Float, V::Float(1)); multiplier.path = "multiplier";
    regions.children = {name, multiplier};
    return registry.Register(std::move(schema), error);
}

bool VansRegisterCombatGameplayAssetCompilers(VansGameplayAssetCompilerRegistry& registry, std::string& error)
{
    return registry.Register(VansAssetType::DamageProfile, std::string(VansDamageProfileType),
        [](const VansGameplayCookedAsset& cooked, VansCompiledGameplayAssetData& output,
           VansGameplayDiagnostics& diagnostics)
        {
            const auto& doc = cooked.runtimeDocument;
            auto profile = std::make_shared<VansDamageProfile>();
            profile->stableName = ReadSerializedStringField(doc, "damageId");
            profile->healthAttribute = ReadSerializedStringField(doc, "healthAttribute");
            const auto number = [&](const char* key) {
                const auto* value = FindObjectField(doc, key);
                return value ? ReadSerializedNumber(*value, -1) : -1;
            };
            profile->baseDamage = number("baseDamage");
            profile->rangeModifier = number("rangeModifier");
            profile->rangeStepMeters = number("rangeStepMeters");
            profile->deathImpulse = number("deathImpulse");
            bool valid = !profile->stableName.empty() && !profile->healthAttribute.empty() &&
                std::isfinite(profile->baseDamage) && profile->baseDamage >= 0 &&
                std::isfinite(profile->rangeModifier) && profile->rangeModifier > 0 && profile->rangeModifier <= 1 &&
                std::isfinite(profile->rangeStepMeters) && profile->rangeStepMeters > 0 &&
                std::isfinite(profile->deathImpulse) && profile->deathImpulse >= 0 && profile->deathImpulse <= 1000;
            const auto* regions = FindObjectField(doc, "regions");
            valid &= regions && regions->kind == VansSerializedValue::Kind::Array && !regions->arrayItems.empty();
            if (regions) for (const auto& item : regions->arrayItems)
            {
                const auto name = ReadSerializedStringField(item, "region");
                const auto* value = FindObjectField(item, "multiplier");
                const auto multiplier = value ? ReadSerializedNumber(*value, -1) : -1;
                valid &= !name.empty() && std::isfinite(multiplier) && multiplier >= 0 &&
                    profile->regionMultipliers.emplace(name, multiplier).second;
            }
            if (!valid)
            {
                diagnostics.push_back({VansGameplayDiagnosticSeverity::Error,
                    "Combat.DamageProfile.Invalid", "Invalid damage profile or duplicate region", {}, "/"});
                return false;
            }
            output = VansCompiledGameplayExtensionAsset{std::string(VansDamageProfileType), profile->stableName, profile};
            return true;
        }, error);
}
}
