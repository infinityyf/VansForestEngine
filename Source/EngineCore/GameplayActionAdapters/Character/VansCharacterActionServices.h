#pragma once

#include "../../GameplayActionCore/VansActionServices.h"
#include "../../RuntimeCore/VansGenerationPool.h"

#include <memory>
#include <string>

namespace VansGraphics
{
class VansAnimationController;
}

namespace VansEngine
{
class VansCharacterControllerNode;
}

namespace Vans
{
class VansRuntimeWorld;

class VansAnimationActionService final : public IVansActionService
{
public:
	static std::shared_ptr<VansAnimationActionService> Create(
		VansRuntimeWorld& world,
		std::string& error);

	const VansActionServiceCapability& Capability() const override { return m_Capability; }
	VansActionCommandResult Execute(const VansActionCommand& command) override;
	bool Release(VansGenerationHandle resource, std::string& error) override;

private:
	struct PlaybackResource
	{
		VansGraphics::VansAnimationController* controller = nullptr;
		std::string previousState;
		float previousRate = 1.0f;
	};

	VansAnimationActionService(
		VansRuntimeWorld& world,
		VansActionServiceCapability capability);

	VansRuntimeWorld& m_World;
	VansActionServiceCapability m_Capability;
	VansGenerationPool<PlaybackResource> m_Playbacks;
};

class VansNavigationActionService final : public IVansActionService
{
public:
	static std::shared_ptr<VansNavigationActionService> Create(
		VansRuntimeWorld& world,
		std::string& error);

	const VansActionServiceCapability& Capability() const override { return m_Capability; }
	VansActionCommandResult Execute(const VansActionCommand& command) override;
	bool Release(VansGenerationHandle resource, std::string& error) override;

private:
	struct MovementBlockResource
	{
		VansEngine::VansCharacterControllerNode* controller = nullptr;
	};

	VansNavigationActionService(
		VansRuntimeWorld& world,
		VansActionServiceCapability capability);

	VansRuntimeWorld& m_World;
	VansActionServiceCapability m_Capability;
	VansGenerationPool<MovementBlockResource> m_Blocks;
};
}
