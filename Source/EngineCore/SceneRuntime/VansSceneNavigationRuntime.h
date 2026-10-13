#pragma once

#include "../NavigationCore/VansNavigationWorld.h"
#include "../NavigationCore/VansNavigationSourceSnapshot.h"
#include "../NavigationCore/VansNavigationTileUpdater.h"
#include "../AssetCore/VansAssetObjectRepository.h"

namespace Vans
{
class VansRuntimeWorld;
struct VansNavigationSourceSnapshot;

// Scene 的组件/资源装配与 CCT motor 适配；不拥有另一份路径或调度状态。
class VansSceneNavigationRuntime
{
public:
	bool Initialize(VansRuntimeWorld& runtime, const VansAssetObjectRepository& assets,
		const VansNavigationSettings& settings, std::string& error);
	bool Synchronize(VansNavigationSourceSnapshot sources, std::uint64_t observation, std::string& error);
    bool Reconfigure(const VansNavigationSettings& settings,std::string& error);
	void WithdrawSources();
	void Shutdown();
	VansNavigationWorld& World() { return m_World; }
	const VansNavigationWorld& World() const { return m_World; }
    VansNavigationTileUpdateStats CaptureTileUpdateStats() const { return m_TileUpdater.CaptureStats(); }

private:
	struct Registration
	{
		VansEntityHandle entity;
		VansComponentHandle component;
		VansRuntimeNavigationAgentComponent config;
		std::shared_ptr<const VansNavigationMap> map;
	};
	VansRuntimeWorld* m_Runtime = nullptr;
	const VansAssetObjectRepository* m_Assets = nullptr;
	VansNavigationWorld m_World;
    VansNavigationTileUpdater m_TileUpdater;
    VansNavigationSettings m_Settings;
    // Scene owns the writable runtime instances. Registrations and World expose
    // read-only shared handles; no reader may cast away their const boundary.
    std::map<std::pair<VansAssetGuid, std::string>, std::shared_ptr<VansNavigationMap>> m_Maps;
	std::map<std::uint64_t, Registration> m_Registrations;
	std::uint64_t m_SourceObservation = 0;
    std::uint64_t m_CarveObstacleObservation = 0;
    struct ProfileInput
    {
        std::uint64_t hash=0;
        std::vector<VansNavigationContribution> contributions;
        std::shared_ptr<const VansNavigationProfileBuildInput> frozen;
        const std::vector<VansNavigationContribution>& Contributions() const
        { return frozen?frozen->contributions:contributions; }
    };
	std::map<std::string,std::map<std::string,ProfileInput>> m_Inputs;
	std::map<std::string,std::string> m_SourceErrors;
};
}
