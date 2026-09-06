#include "VansUIActionCapability.h"

#include "../VansActionServiceAdapter.h"

namespace Vans
{
const VansActionServiceCapability& VansUIActionCapability()
{
	using V = VansActionCommandValueKind;
	using R = VansActionCommandResourcePolicy;
	const auto payload = []
	{
		return VansActionCommandField("parameters", V::Object, false,
			VansSerializedValue::Object({}));
	};
	static const VansActionServiceCapability capability =
		VansActionServiceCapabilityDescriptor("Service.UI", {
			VansActionCommandCapability("UI.Prompt", R::Create, {
				VansActionCommandField("messageKey", V::String, true),
				VansActionCommandField("binding", V::String, false,
					VansSerializedValue::String({})), payload()
			}),
			VansActionCommandCapability("UI.Indicator", R::Create, {
				VansActionCommandField("indicator", V::String, true),
				VansActionCommandField("target", V::Object, false,
					VansSerializedValue::Object({})), payload()
			}),
			VansActionCommandCapability("UI.ActionStateView", R::Create, {
				VansActionCommandField("viewModel", V::String, false,
					VansSerializedValue::String({})), payload()
			}),
			VansActionCommandCapability("UI.Remove", R::Release,
				{ VansActionCommandResourceField() })
		});
	return capability;
}
}
