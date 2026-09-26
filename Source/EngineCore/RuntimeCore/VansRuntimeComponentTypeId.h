#pragma once

#include <cstdint>

namespace Vans
{
constexpr std::uint16_t VansInvalidComponentTypeId = 0;

enum VansRuntimeComponentTypeId : std::uint16_t
{
	VansRuntimeComponentType_Render = 1,
	VansRuntimeComponentType_Physics = 2,
	VansRuntimeComponentType_Cloth = 3,
	VansRuntimeComponentType_CharacterController = 4,
	VansRuntimeComponentType_DirectionalLight = 5,
	VansRuntimeComponentType_PointLight = 6,
	VansRuntimeComponentType_SpotLight = 7,
	VansRuntimeComponentType_RectLight = 8,
	VansRuntimeComponentType_Camera = 9,
	VansRuntimeComponentType_Audio = 10,
	VansRuntimeComponentType_AudioReverbZone = 11,
	VansRuntimeComponentType_AudioVolume = 12,
	VansRuntimeComponentType_Video = 13,
	VansRuntimeComponentType_Particle = 14,
	VansRuntimeComponentType_Animation = 15,
	VansRuntimeComponentType_Ragdoll = 16,
	VansRuntimeComponentType_Vehicle = 17,
	VansRuntimeComponentType_UI = 18,
	VansRuntimeComponentType_Script = 19,
	VansRuntimeComponentType_Transform = 20,
	VansRuntimeComponentType_Timeline = 21,
	VansRuntimeComponentType_ActionHost = 22,
	VansRuntimeComponentType_NavigationAgent = 23,
	VansRuntimeComponentType_AIAgent = 24,
};
}
