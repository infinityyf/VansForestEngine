#pragma once

#include "VansTimelineSessionService.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Vans
{
struct VansComponentHeader;

using VansTimelineRuntimeAssetLoader = std::function<bool(
	const VansRuntimeTimelineComponent& component,
	std::shared_ptr<const VansCompiledTimeline>& timeline,
	std::string& error)>;

using VansTimelineRuntimeAssetGenerationQuery = std::function<bool(
	const std::string& assetGuid,
	std::uint64_t& generation)>;

class VansTimelineRuntimeSystem
{
public:
	VansTimelineRuntimeSystem();
	void RegisterWorld(VansRuntimeWorld* world);
	void SetAssetLoader(VansTimelineRuntimeAssetLoader loader);
	void SetAssetGenerationQuery(VansTimelineRuntimeAssetGenerationQuery query);
	void SetApplierRegistry(std::shared_ptr<VansTimelineApplierRegistry> appliers);
	void SetPayloadSchemaRegistry(std::shared_ptr<VansPayloadSchemaRegistry> payloads);
	bool HasOutputApplier(VansTimelineOutputTypeId type) const;
	bool HasPayloadSchema(VansTimelinePayloadTypeId type) const;
	bool ValidatePayload(VansTimelinePayloadTypeId type,
		const VansSerializedValue& payload, std::string& error) const;
	void SyncTimelineComponents();
	void UpdateRuntimePostScript(double deltaSeconds);
	void UpdateRuntimeCamera(double deltaSeconds);
	void UpdatePreviewsPostScript(double deltaSeconds);
	void UpdatePreviewsCamera(double deltaSeconds);
	bool PlayComponent(VansComponentHandle component, bool restart);
	bool PauseComponent(VansComponentHandle component);
	bool ResumeComponent(VansComponentHandle component);
	bool StopComponent(VansComponentHandle component);
	bool GetComponentState(VansComponentHandle component,
		VansTimelinePlayerState& state, VansTimelineTick& tick) const;
	void StopAll();
	void Clear();
	bool StartPreview(std::string previewId,
		std::shared_ptr<const VansCompiledTimeline> timeline, VansEntityHandle owner,
		bool safeEvents, bool includeSubTimelines, std::string& error);
	bool PlayPreview(const std::string& previewId);
	bool PausePreview(const std::string& previewId);
	bool ConfigurePreview(const std::string& previewId, double playRate, int direction,
		bool loopPlaybackRange);
	bool SeekPreview(const std::string& previewId, VansTimelineTick tick, VansTimelineSeekPolicy policy);
	bool StopPreview(const std::string& previewId);
	bool GetPreviewState(const std::string& previewId,
		VansTimelinePlayerState& state, VansTimelineTick& tick) const;
	VansTimelineSessionService& Sessions() { return *m_Sessions; }
	const VansTimelineDiagnostics& Diagnostics() const;

private:
	struct ComponentFacade
	{
		VansComponentHandle component;
		VansTimelineSessionHandle session;
		std::string assetGuid;
		std::uint64_t assetGeneration = 0;
		bool wasEnabled = false;
	};
	struct ComponentKey
	{
		std::uint16_t type = 0; std::uint32_t index = UINT32_MAX; std::uint32_t generation = 0;
		friend bool operator==(const ComponentKey& a, const ComponentKey& b)
		{ return a.type == b.type && a.index == b.index && a.generation == b.generation; }
	};
	struct ComponentKeyHash
	{
		std::size_t operator()(const ComponentKey& key) const noexcept
		{ return (static_cast<std::size_t>(key.type) << 48) ^ (static_cast<std::size_t>(key.index) << 16) ^ key.generation; }
	};
	static ComponentKey Key(VansComponentHandle handle) { return { handle.typeId, handle.index, handle.generation }; }
	VansTimelineSessionHandle CreateComponentSession(
		const VansRuntimeTimelineComponent& component,
		const VansComponentHeader& header,
		std::string& error);

	VansRuntimeWorld* m_World = nullptr;
	VansTimelineRuntimeAssetLoader m_AssetLoader;
	VansTimelineRuntimeAssetGenerationQuery m_AssetGenerationQuery;
	VansTimelineClockRegistry& m_Clocks;
	std::shared_ptr<VansTimelineApplierRegistry> m_Appliers;
	std::shared_ptr<VansPayloadSchemaRegistry> m_Payloads;
	std::unique_ptr<VansTimelineSessionService> m_Sessions;
	std::unordered_map<ComponentKey, ComponentFacade, ComponentKeyHash> m_Components;
	std::unordered_map<std::string, VansTimelineSessionHandle> m_Previews;
	VansTimelineDiagnostics m_Diagnostics;
};
}
