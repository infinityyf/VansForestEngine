#pragma once

#include "VansAudioActionCapability.h"
#include "../../AudioCore/VansAudioSourceBinding.h"
#include "../../RuntimeCore/VansGenerationPool.h"

#include <functional>
#include <glm/vec3.hpp>

namespace Vans
{
class VansRuntimeWorld;

class VansAudioActionService final : public IVansActionService
{
public:
	using PositionResolver = std::function<bool(VansEntityHandle, glm::vec3&)>;
	VansAudioActionService(VansRuntimeWorld& world, VansEngine::VansAudioManager& audio,
		PositionResolver resolvePosition);
	const VansActionServiceCapability& Capability() const override { return VansAudioActionCapability(); }
	VansActionCommandResult Execute(const VansActionCommand& command) override;
	bool Release(VansGenerationHandle resource, std::string& error) override;
	void Tick(double deltaSeconds) override;

private:
	struct Loop
	{
		std::unique_ptr<VansEngine::VansAudioSourceBinding> source;
		float fadeDuration = 0.0f;
		float fadeRemaining = 0.0f;
		float fadeVolume = 0.0f;
	};
	VansRuntimeWorld& m_World;
	VansEngine::VansAudioManager& m_Audio;
	PositionResolver m_ResolvePosition;
	VansGenerationPool<Loop> m_Loops;
};
}
