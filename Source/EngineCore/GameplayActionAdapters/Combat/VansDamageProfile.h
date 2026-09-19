#pragma once
#include "../../GameplayActionSchema/VansGameplayAssetCompiler.h"
#include <unordered_map>

namespace Vans
{
inline constexpr std::string_view VansDamageProfileType = "Gameplay.Combat.DamageProfile";
struct VansDamageProfile
{
    std::string stableName;
    std::string healthAttribute;
    double baseDamage = 0.0;
    double rangeModifier = 1.0;
    double rangeStepMeters = 1.0;
    double deathImpulse = 0.0;
    std::unordered_map<std::string, double> regionMultipliers;
};
bool VansRegisterCombatGameplayAssetSchemas(VansGameplayAssetSchemaRegistry&, std::string&);
bool VansRegisterCombatGameplayAssetCompilers(VansGameplayAssetCompilerRegistry&, std::string&);
}
