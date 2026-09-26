#include "VansGameplayPrimitivesContributor.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../GameplayAttributes/VansGameplayAttributes.h"
#include "../GameplayEffects/VansGameplayEffects.h"

#include <cmath>

namespace Vans
{
std::shared_ptr<const IVansGameplayModuleContributor>
VansMakeGameplayPrimitivesGAFContributor()
{
	return VansMakeGAFModuleContributor(
		VansMakeGAFModuleDescriptor("Gameplay.Primitives", "Gameplay Primitives", { "Core" }),
		VansRegisterGameplayPrimitiveGAFTypes,
		VansRegisterGameplayPrimitiveGAFSchemas,
		[](VansGAFRuntimeRegistry& context, std::string& contributionError)
		{
			if (!context.RegisterTargetingHandlers(
				VansRegisterBuiltInTargetingHandlers, contributionError)) return false;
			if (!context.RegisterHostInitializer("Gameplay.Tags.Initialize",
				[](VansActionHost& host, const VansGameplayAssetLibrary& assets,
					VansEntityHandle, std::uint64_t source, const VansSerializedValue& inputs,
					std::string& initializerError)
				{
					const std::string tagName = ReadSerializedStringField(inputs, "tag");
					const std::int64_t count = ReadSerializedIntField(inputs, "count", 1);
					const std::optional<VansGameplayTagId> tag = assets.Tags().FindId(tagName);
					if (!tag || count <= 0 || !host.Tags().Add(*tag, source,
						static_cast<std::uint32_t>(count)))
					{
						initializerError = "ActionHost Tag initializer is invalid: " + tagName;
						return false;
					}
					return true;
				}, contributionError)) return false;
			if (!context.RegisterHostInitializer("Gameplay.Attributes.Initialize",
				[](VansActionHost& host, const VansGameplayAssetLibrary& assets,
					VansEntityHandle, std::uint64_t, const VansSerializedValue& inputs,
					std::string& initializerError)
				{
					const std::string attributeName =
						ReadSerializedStringField(inputs, "attribute");
					const VansSerializedValue* value = FindObjectField(inputs, "value");
					const double number = value ? ReadSerializedNumber(*value) : 0.0;
					const VansAttributeId attribute =
						VansMakeStableId<VansAttributeIdTag>(attributeName);
					const VansAttributeDefinition* definition =
						assets.Attributes().Resolve(attribute);
					if (!definition || !definition->IsValueInRange(number) ||
						!host.Attributes().ApplyBase(attribute,
							VansAttributeBaseOperation::Set, number,
							VansAttributeBoundsPolicy::Reject))
					{
						initializerError = "ActionHost Attribute initializer is invalid: " +
							attributeName;
						return false;
					}
					return true;
				}, contributionError)) return false;
			const VansGameplayAssetLibrary* assets = &context.Assets();
			if (!context.RegisterActionSetInitializer("Gameplay.Effects.Initialize",
				[assets](VansActionHost& host, std::uint64_t source,
					const VansSerializedValue& inputs,
					VansActionSetInitializerCleanup& cleanup,
					std::string& initializerError)
				{
					const VansSerializedValue* asset = FindObjectField(inputs, "asset");
					const std::string effectName = asset
						? ReadSerializedString(*asset) : std::string{};
					const std::string releasePolicy =
						ReadSerializedStringField(inputs, "releasePolicy", "OnRevoke");
					const VansEffectRegistry* effects = assets->Effects();
					const auto definition = effects ? effects->Resolve(
						VansMakeStableId<VansEffectIdTag>(effectName)) : nullptr;
					if (!definition || (releasePolicy != "OnRevoke" && releasePolicy != "Retain"))
					{
						initializerError = "ActionSet Effect initializer is invalid: " + effectName;
						return false;
					}
					VansEffectSpec spec;
					spec.definition = definition;
					spec.source = source;
					if (!VansDecodeEffectSetByCaller(
						FindObjectField(inputs, "setByCaller"),
						spec.setByCaller, initializerError)) return false;
					spec.context.SetEntity(VansActionContextSlots::Owner, host.Owner());
					spec.context.SetEntity(VansActionContextSlots::Instigator, host.Owner());
					spec.context.SetEntity(VansActionContextSlots::PrimaryTarget, host.Owner());
					const VansEffectApplicationResult applied = host.Effects().Apply(spec);
					if (!applied)
					{
						initializerError = applied.message;
						return false;
					}
					if (releasePolicy == "OnRevoke" && applied.active)
						cleanup = [effect = applied.active](VansActionHost& target,
							std::string& cleanupError)
						{
							if (target.Effects().Remove(effect, cleanupError)) return true;
							return cleanupError == "Active Effect handle is stale";
						};
					return true;
				}, contributionError)) return false;
			return context.RegisterActionSetInitializer("Gameplay.Attributes.Initialize",
				[assets](VansActionHost& host, std::uint64_t source,
					const VansSerializedValue& inputs,
					VansActionSetInitializerCleanup& cleanup,
					std::string& initializerError)
				{
					const std::string attributeName =
						ReadSerializedStringField(inputs, "attribute");
					const VansSerializedValue* value = FindObjectField(inputs, "value");
					const double number = value ? ReadSerializedNumber(*value) : 0.0;
					const std::string releasePolicy =
						ReadSerializedStringField(inputs, "releasePolicy", "OnRevoke");
					const VansAttributeId attribute =
						VansMakeStableId<VansAttributeIdTag>(attributeName);
					const VansAttributeDefinition* definition =
						assets->Attributes().Resolve(attribute);
					if (!definition || !definition->IsValueInRange(number) ||
						(releasePolicy != "OnRevoke" && releasePolicy != "Retain"))
					{
						initializerError = "ActionSet Attribute initializer is invalid: " + attributeName;
						return false;
					}
					VansAttributeModifierDesc modifier;
					modifier.attribute = attribute;
					modifier.operation = VansAttributeModifierOperation::Override;
					modifier.magnitude = number;
					modifier.source = source;
					host.Attributes().BeginBatch();
					const VansAttributeModifierHandle applied = host.Attributes().AddModifier(modifier);
					host.Attributes().EndBatch();
					if (!applied)
					{
						initializerError = "ActionSet Attribute initializer could not be applied: " +
							attributeName;
						return false;
					}
					if (releasePolicy == "OnRevoke")
						cleanup = [applied](VansActionHost& target, std::string& cleanupError)
						{
							target.Attributes().BeginBatch();
							const bool removed = target.Attributes().RemoveModifier(applied);
							target.Attributes().EndBatch();
							if (!removed) cleanupError =
								"ActionSet Attribute initializer handle is stale";
							return removed;
						};
					return true;
				}, contributionError);
		}, VansRegisterGameplayPrimitiveAssetCompilers,
		VansRegisterGameplayPrimitiveAssetSchemas);
}
}
