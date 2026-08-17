#pragma once

#include "../../CameraCore/VansCameraCore.h"
#include "../../GameplayActionCore/VansActionServices.h"
#include "../../GameplayActionSchema/VansGameplayAssetLibrary.h"
#include "../../RuntimeCore/VansGenerationPool.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Vans
{
class VansCameraActionService final : public IVansActionService
{
public:
	using TargetPositionResolver = std::function<bool(VansEntityHandle, glm::vec3&)>;

	static std::shared_ptr<VansCameraActionService> Create(
		VansCameraRuntime& runtime,
		const VansGameplayAssetLibrary& assets,
		std::string& error,
		TargetPositionResolver targetResolver = {});
	~VansCameraActionService() override;

	const VansActionServiceCapability& Capability() const override { return m_Capability; }
	VansActionCommandResult Execute(const VansActionCommand& command) override;
	bool Release(VansGenerationHandle resource, std::string& error) override;

	static VansCameraContributionDomainId Domain();

private:
	struct OwnerToken {};

	explicit VansCameraActionService(
		VansCameraRuntime& runtime,
		TargetPositionResolver targetResolver);
	bool InitializeProfiles(const VansGameplayAssetLibrary& assets, std::string& error);
	VansActionCommandResult CreateContribution(VansCameraContribution contribution);
	VansActionCommandResult ExecuteShot(const VansActionCommand& command);
	VansActionCommandResult ExecuteLens(const VansActionCommand& command);
	VansActionCommandResult ExecuteShake(const VansActionCommand& command);
	VansActionCommandResult ExecuteImpulse(const VansActionCommand& command);
	VansActionCommandResult ExecuteLockOn(const VansActionCommand& command);
	VansActionCommandResult ExecuteUpdateLockOn(const VansActionCommand& command);
	VansActionCommandResult ExecuteRelease(const VansActionCommand& command);

	static std::uint64_t ResourceKey(VansGenerationHandle resource);
	static VansSerializedValue ResourceValue(VansGenerationHandle resource);

	VansCameraRuntime* m_Runtime = nullptr;
	const VansGameplayAssetLibrary* m_Assets = nullptr;
	VansActionServiceCapability m_Capability;
	TargetPositionResolver m_TargetResolver;
	VansGenerationPool<OwnerToken> m_OwnerTokens;
	std::unordered_map<std::uint64_t, VansGenerationHandle> m_ResourceOwners;
	std::unordered_map<VansCameraRigId, VansCameraRigHandle> m_Rigs;
	std::unordered_map<VansCameraShakeId, VansCameraShakeHandle> m_Shakes;
	std::uint32_t m_NextTransientWriter = 1;
};
}
