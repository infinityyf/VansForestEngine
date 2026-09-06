#include "VansProjectileActionCapability.h"

#include "../VansActionServiceAdapter.h"

namespace Vans
{
const VansActionServiceCapability& VansProjectileActionCapability()
{
	using V = VansActionCommandValueKind;
	using R = VansActionCommandResourcePolicy;
	static const VansActionServiceCapability capability =
		VansActionServiceCapabilityDescriptor("Service.Projectile", {
			VansActionCommandCapability("Projectile.Spawn", R::Create, {
				VansActionCommandField("source", V::String, true),
				VansActionCommandNumberField("speed", V::Float, false, VansSerializedValue::Float(8.0), 0, 1000),
				VansActionCommandNumberField("lift", V::Float, false, VansSerializedValue::Float(3.0), -1000, 1000),
				VansActionCommandNumberField("mass", V::Float, false, VansSerializedValue::Float(0.4), 0.001, 100000),
				// 0 表示不自动回收；正数指定实体寿命（秒）。
				VansActionCommandNumberField("lifetime", V::Float, false, VansSerializedValue::Float(10), 0, 3600),
				VansActionCommandNumberField("restitution", V::Float, false, VansSerializedValue::Float(0.25), 0, 1),
				VansActionCommandNumberField("friction", V::Float, false, VansSerializedValue::Float(0.6), 0, 10),
				VansActionCommandField("restitutionCombine", V::String, false, VansSerializedValue::String("Average")),
				VansActionCommandField("frictionCombine", V::String, false, VansSerializedValue::String("Average")),
				VansActionCommandField("particle", V::Object, false, VansSerializedValue::Object({})),
				VansActionCommandField("collisionLayer", V::String, false, VansSerializedValue::String("Default")),
				VansActionCommandField("angularVelocity", V::Object, false, VansSerializedValue::Object({})),
				VansActionCommandField("velocity", V::Object, false,
					VansSerializedValue::Object({}))
			}),
			VansActionCommandCapability("Projectile.Destroy", R::Release, {
				VansActionCommandResourceField(),
				VansActionCommandField("reason", V::String, false,
					VansSerializedValue::String({}))
			})
		});
	return capability;
}
}
