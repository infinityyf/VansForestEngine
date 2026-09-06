#include "VansPhysicsQueryActionCapability.h"

#include "../VansActionServiceAdapter.h"

namespace Vans
{
const VansActionServiceCapability& VansPhysicsQueryActionCapability()
{
	using V = VansActionCommandValueKind;
	using R = VansActionCommandResourcePolicy;
	const auto object = [](std::string name)
	{
		return VansActionCommandField(std::move(name), V::Object, true,
			VansSerializedValue::Object({}));
	};
	const auto mask = []
	{
		return VansActionCommandField("layerMask", V::String, false,
			VansSerializedValue::String({}));
	};
	static const VansActionServiceCapability capability =
		VansActionServiceCapabilityDescriptor("Service.PhysicsQuery", {
			VansActionCommandCapability("PhysicsQuery.Ray", R::None, {
				object("origin"), object("direction"),
				VansActionCommandNumberField("distance", V::Float, true,
					VansSerializedValue::Float(1.0), 0.0, 1000000.0), mask()
			}),
			VansActionCommandCapability("PhysicsQuery.Shape", R::None,
				{ object("shape"), object("transform"), mask() }),
			VansActionCommandCapability("PhysicsQuery.Sweep", R::None,
				{ object("shape"), object("from"), object("to"), mask() })
		});
	return capability;
}
}
