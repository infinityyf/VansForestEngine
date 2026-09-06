#include "VansAudioActionCapability.h"

#include "../VansActionServiceAdapter.h"

namespace Vans
{
const VansActionServiceCapability& VansAudioActionCapability()
{
	using V = VansActionCommandValueKind;
	using R = VansActionCommandResourcePolicy;
	const auto asset = [](std::string name)
	{
		return VansActionCommandField(std::move(name), V::String, true);
	};
	const auto normalized = [](std::string name)
	{
		return VansActionCommandNumberField(std::move(name), V::Float, false,
			VansSerializedValue::Float(1.0), 0.0, 1.0);
	};
	const auto pitch = []
	{
		return VansActionCommandNumberField("pitch", V::Float, false,
			VansSerializedValue::Float(1.0), 0.01, 4.0);
	};
	static const VansActionServiceCapability capability =
		VansActionServiceCapabilityDescriptor("Service.Audio", {
			VansActionCommandCapability("Audio.OneShot", R::None, {
				asset("sound"), normalized("volume"), pitch(),
				VansActionCommandField("spatial", V::Bool, false,
					VansSerializedValue::Bool(true))
			}),
			VansActionCommandCapability("Audio.Loop", R::Create,
				{ asset("sound"), normalized("volume"), pitch() }),
			VansActionCommandCapability("Audio.Update", R::Update,
				{ VansActionCommandResourceField(), normalized("volume"), pitch() }),
			VansActionCommandCapability("Audio.Stop", R::Release, {
				VansActionCommandResourceField(),
				VansActionCommandNumberField("fadeOut", V::Float, false,
					VansSerializedValue::Float(0.0), 0.0, 1.0)
			})
		});
	return capability;
}
}
