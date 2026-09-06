#include "VansVFXActionCapability.h"

#include "../VansActionServiceAdapter.h"

namespace Vans
{
const VansActionServiceCapability& VansVFXActionCapability()
{
	using V = VansActionCommandValueKind;
	using R = VansActionCommandResourcePolicy;
	const auto optionalString = [](std::string name)
	{
		return VansActionCommandField(std::move(name), V::String, false,
			VansSerializedValue::String({}));
	};
	static const VansActionServiceCapability capability =
		VansActionServiceCapabilityDescriptor("Service.VFX", {
			VansActionCommandCapability("VFX.Spawn", R::Create, {
				VansActionCommandField("effect", V::String, true), optionalString("socket"),
				VansActionCommandField("parameters", V::Object, false,
					VansSerializedValue::Object({}))
			}),
			VansActionCommandCapability("VFX.Attach", R::Update,
				{ VansActionCommandResourceField(), optionalString("socket") }),
			VansActionCommandCapability("VFX.Update", R::Update, {
				VansActionCommandResourceField(),
				VansActionCommandField("parameters", V::Object, true,
					VansSerializedValue::Object({}))
			}),
			VansActionCommandCapability("VFX.Stop", R::Release, {
				VansActionCommandResourceField(),
				VansActionCommandField("immediate", V::Bool, false,
					VansSerializedValue::Bool(false))
			})
		});
	return capability;
}
}
