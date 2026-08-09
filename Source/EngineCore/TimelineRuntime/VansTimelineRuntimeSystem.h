#pragma once

#include "VansTimelinePlayer.h"
#include "VansTimelineTrackAppliers.h"

#include <cstdint>
#include <functional>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace Vans
{
using VansTimelineRuntimeAssetLoader = std::function<bool(
	const VansRuntimeTimelineComponent& component,
	std::shared_ptr<const VansCompiledTimeline>& timeline,
	std::string& error)>;

using VansTimelineRuntimeAssetRevisionQuery = std::function<bool(
	const std::string& assetGuid,
	std::uint64_t& revision)>;

class VansTimelineRuntimeSystem
{
public:
	void RegisterWorld(VansRuntimeWorld* world);
	void SetAssetLoader(VansTimelineRuntimeAssetLoader loader);
	void SetAssetRevisionQuery(VansTimelineRuntimeAssetRevisionQuery query);
	void SetAdapters(VansTimelineRuntimeAdapters adapters);
	void SyncTimelineComponents();
	void UpdateRuntimePostScript(double deltaSeconds);
	void UpdateRuntimeCamera(double deltaSeconds);
	void UpdatePreviewsPostScript(double deltaSeconds);
	void UpdatePreviewsCamera(double deltaSeconds);
	void UpdatePostScript(double deltaSeconds);
	void UpdateCamera(double deltaSeconds);
	bool PlayComponent(VansComponentHandle component, bool restart);
	bool PauseComponent(VansComponentHandle component);
	bool ResumeComponent(VansComponentHandle component);
	bool StopComponent(VansComponentHandle component);
	bool GetComponentState(
		VansComponentHandle component,
		VansTimelinePlayerState& state,
		VansTimelineTick& tick) const;
	void StopAll();
	void Clear();
	bool StartPreview(
		std::string previewId,
		std::shared_ptr<const VansCompiledTimeline> timeline,
		VansEntityHandle owner,
		bool safeEvents,
		bool includeSubTimelines,
		std::string& error);
	bool PlayPreview(const std::string& previewId);
	bool PausePreview(const std::string& previewId);
	bool ConfigurePreview(const std::string& previewId, double playRate, int direction,
		bool loopPlaybackRange);
	bool SeekPreview(const std::string& previewId, VansTimelineTick tick, VansTimelineSeekPolicy policy);
	bool StopPreview(const std::string& previewId);
	VansTimelinePlayer* FindPreview(const std::string& previewId);

	VansTimelinePlayer* FindPlayer(VansComponentHandle component);
	const VansTimelineDiagnostics& Diagnostics() const { return m_Diagnostics; }

private:
	struct Entry
	{
		VansComponentHandle component;
		VansEntityHandle owner;
		VansTimelinePlayer player;
		std::vector<VansTimelineEvaluationOutput> outputScratch;
		std::string loadedAssetGuid;
		std::string attemptedAssetGuid;
		std::uint64_t loadedRevision = 0;
		std::uint64_t attemptedRevision = 0;
		bool wasEnabled = false;
	};
	struct ChildEntry
	{
		std::string key;
		std::string parentWriterId;
		std::string rootWriterId;
		VansEntityHandle owner;
		std::int32_t hierarchicalBias = 0;
		VansTimelinePlayer player;
		std::vector<VansTimelineEvaluationOutput> outputScratch;
		std::string loadedAssetGuid;
		std::string attemptedAssetGuid;
		std::uint64_t loadedRevision = 0;
		std::uint64_t attemptedRevision = 0;
	};
	struct PreviewEntry
	{
		std::string id;
		VansEntityHandle owner;
		bool ownsOwner = false;
		VansTimelinePlayer player;
		std::vector<VansTimelineEvaluationOutput> outputScratch;
	};

	static std::string WriterId(VansComponentHandle component);
	void ApplyOutputs(std::vector<VansTimelineEvaluationOutput>& outputs);
	VansTimelinePlayer* FindPlayerByWriterId(const std::string& writerId);
	VansEntityHandle FindOwnerByWriterId(const std::string& writerId) const;
	void CollectPostScriptOutputs(
		std::vector<VansTimelineEvaluationOutput>& source,
		std::vector<VansTimelineEvaluationOutput>& destination,
		std::size_t depth = 0);
	void RemoveInactiveChildren();
	void ReconcileActiveWriters(
		const std::unordered_set<std::string>& evaluatedRoots,
		const std::vector<VansTimelineEvaluationOutput>& outputs,
		std::unordered_map<std::string, std::unordered_set<std::string>>& activeWriters);
	void ReleaseRootWriters(const std::string& rootWriterId);
	void RefreshEntryAsset(Entry& entry, const VansRuntimeTimelineComponent& component,
		const std::string& diagnosticObjectId);
	void RefreshChildAsset(ChildEntry& entry, const VansRuntimeTimelineComponent& component,
		const std::string& diagnosticObjectId);

	VansRuntimeWorld* m_World = nullptr;
	VansTimelineRuntimeAssetLoader m_AssetLoader;
	VansTimelineRuntimeAssetRevisionQuery m_AssetRevisionQuery;
	VansTimelineRuntimeAdapters m_Adapters;
	VansTimelinePreAnimatedState m_PreAnimatedState;
	std::vector<Entry> m_Entries;
	std::list<ChildEntry> m_Children;
	std::vector<PreviewEntry> m_Previews;
	std::vector<VansTimelineEvaluationOutput> m_OutputScratch;
	VansTimelineDiagnostics m_Diagnostics;
	std::unordered_map<std::string, std::unordered_set<std::string>> m_ActivePostScriptWriters;
	std::unordered_map<std::string, std::unordered_set<std::string>> m_ActiveCameraWriters;
	std::unordered_set<std::string> m_EvaluatedPostScriptRoots;
};
}
