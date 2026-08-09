#include "VansTimelineRuntimeSystem.h"

#include "../SceneRuntime/VansComponentStorage.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../SceneRuntime/VansRuntimeWorld.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
void VansTimelineRuntimeSystem::RegisterWorld(VansRuntimeWorld* world)
{
	if (m_World == world) return;
	Clear();
	m_World = world;
}

void VansTimelineRuntimeSystem::SetAssetLoader(VansTimelineRuntimeAssetLoader loader)
{
	m_AssetLoader = std::move(loader);
}

void VansTimelineRuntimeSystem::SetAssetRevisionQuery(VansTimelineRuntimeAssetRevisionQuery query)
{
	m_AssetRevisionQuery = std::move(query);
}

void VansTimelineRuntimeSystem::RefreshEntryAsset(
	Entry& entry,
	const VansRuntimeTimelineComponent& component,
	const std::string& diagnosticObjectId)
{
	std::uint64_t revision = 0;
	const bool hasRevision = m_AssetRevisionQuery &&
		m_AssetRevisionQuery(component.assetGuid, revision);
	if (entry.attemptedAssetGuid == component.assetGuid &&
		(!hasRevision || entry.attemptedRevision == revision))
		return;
	entry.attemptedAssetGuid = component.assetGuid;
	entry.attemptedRevision = revision;
	if (!m_AssetLoader) return;

	std::shared_ptr<const VansCompiledTimeline> replacement;
	std::string error;
	if (!m_AssetLoader(component, replacement, error))
	{
		m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, diagnosticObjectId,
			"assetGuid", "Timeline hot reload kept the last valid asset: " + error });
		return;
	}
	ReleaseRootWriters(entry.player.WriterId());
	if (!entry.player.Reload(std::move(replacement), error))
	{
		m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, diagnosticObjectId,
			"assetGuid", "Timeline hot reload could not replace the player: " + error });
		return;
	}
	entry.loadedAssetGuid = component.assetGuid;
	entry.loadedRevision = revision;
	VANS_LOG("[Timeline] Hot reloaded asset='" << component.assetGuid
		<< "' revision=" << revision);
}

void VansTimelineRuntimeSystem::RefreshChildAsset(
	ChildEntry& entry,
	const VansRuntimeTimelineComponent& component,
	const std::string& diagnosticObjectId)
{
	std::uint64_t revision = 0;
	const bool hasRevision = m_AssetRevisionQuery &&
		m_AssetRevisionQuery(component.assetGuid, revision);
	if (entry.attemptedAssetGuid == component.assetGuid &&
		(!hasRevision || entry.attemptedRevision == revision))
		return;
	entry.attemptedAssetGuid = component.assetGuid;
	entry.attemptedRevision = revision;
	if (!m_AssetLoader) return;

	std::shared_ptr<const VansCompiledTimeline> replacement;
	std::string error;
	if (!m_AssetLoader(component, replacement, error))
	{
		m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, diagnosticObjectId,
			"assetGuid", "SubTimeline hot reload kept the last valid asset: " + error });
		return;
	}
	ReleaseRootWriters(entry.player.WriterId());
	if (!entry.player.Reload(std::move(replacement), error))
	{
		m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, diagnosticObjectId,
			"assetGuid", "SubTimeline hot reload could not replace the player: " + error });
		return;
	}
	entry.loadedAssetGuid = component.assetGuid;
	entry.loadedRevision = revision;
}

void VansTimelineRuntimeSystem::SetAdapters(VansTimelineRuntimeAdapters adapters)
{
	m_Adapters = std::move(adapters);
}

bool VansTimelineRuntimeSystem::StartPreview(
	std::string previewId,
	std::shared_ptr<const VansCompiledTimeline> timeline,
	VansEntityHandle owner,
	bool safeEvents,
	bool includeSubTimelines,
	std::string& error)
{
	error.clear();
	if (!m_World || previewId.empty() || !timeline)
	{
		error = "Timeline preview requires a compiled asset and registered RuntimeWorld";
		return false;
	}
	StopPreview(previewId);
	bool ownsOwner = false;
	if (!owner.IsValid())
	{
		owner = m_World->CreateEntity({ previewId, "Timeline Offline Preview" });
		ownsOwner = owner.IsValid();
	}
	if (!owner.IsValid() || !m_World->IsAlive(owner))
	{
		error = "Timeline preview owner is unavailable";
		return false;
	}
	VansRuntimeTimelineComponent component;
	component.instance.playOn = VansTimelinePlayOn::Manual;
	component.instance.updateMode = VansTimelineUpdateMode::GameTime;
	component.instance.restoreStateOnStop = true;
	PreviewEntry entry;
	entry.id = std::move(previewId);
	entry.owner = owner;
	entry.ownsOwner = ownsOwner;
	if (!entry.player.Load(std::move(timeline), component, m_World, owner,
		"timeline-preview:" + entry.id, error))
	{
		if (entry.ownsOwner) m_World->DestroyEntity(entry.owner);
		return false;
	}
	entry.player.SetEditorPreviewPolicy(true, safeEvents, includeSubTimelines);
	m_Previews.push_back(std::move(entry));
	return true;
}

VansTimelinePlayer* VansTimelineRuntimeSystem::FindPreview(const std::string& previewId)
{
	const auto found = std::find_if(m_Previews.begin(), m_Previews.end(),
		[&](const PreviewEntry& entry) { return entry.id == previewId; });
	return found == m_Previews.end() ? nullptr : &found->player;
}

bool VansTimelineRuntimeSystem::PlayPreview(const std::string& previewId)
{
	VansTimelinePlayer* player = FindPreview(previewId);
	if (!player) return false;
	player->Play();
	return true;
}

bool VansTimelineRuntimeSystem::PausePreview(const std::string& previewId)
{
	VansTimelinePlayer* player = FindPreview(previewId);
	if (!player) return false;
	player->Pause();
	return true;
}

bool VansTimelineRuntimeSystem::ConfigurePreview(
	const std::string& previewId,
	double playRate,
	int direction,
	bool loopPlaybackRange)
{
	VansTimelinePlayer* player = FindPreview(previewId);
	if (!player || !std::isfinite(playRate) || playRate <= 0.0)
		return false;
	player->SetPlayRate(playRate);
	player->SetDirection(direction);
	player->SetLoop(loopPlaybackRange ? VansTimelineLoopMode::Loop : VansTimelineLoopMode::None,
		loopPlaybackRange ? 0 : 1);
	return true;
}

bool VansTimelineRuntimeSystem::SeekPreview(
	const std::string& previewId,
	VansTimelineTick tick,
	VansTimelineSeekPolicy policy)
{
	VansTimelinePlayer* player = FindPreview(previewId);
	if (!player) return false;
	player->SeekTicks(tick, policy, VansTimelineEvaluationReason::Scrub);
	return true;
}

bool VansTimelineRuntimeSystem::StopPreview(const std::string& previewId)
{
	const auto found = std::find_if(m_Previews.begin(), m_Previews.end(),
		[&](const PreviewEntry& entry) { return entry.id == previewId; });
	if (found == m_Previews.end()) return false;
	found->player.Stop();
	ReleaseRootWriters(found->player.WriterId());
	if (found->ownsOwner && m_World && m_World->IsAlive(found->owner))
		m_World->DestroyEntity(found->owner);
	m_Previews.erase(found);
	return true;
}

std::string VansTimelineRuntimeSystem::WriterId(VansComponentHandle component)
{
	return "timeline:" + std::to_string(component.index) + ":" + std::to_string(component.generation);
}

void VansTimelineRuntimeSystem::SyncTimelineComponents()
{
	if (!m_World) return;
	auto* storage = static_cast<VansComponentStorage<VansRuntimeTimelineComponent>*>(
		m_World->FindStorage(VansRuntimeComponentType_Timeline));
	if (!storage)
	{
		for (Entry& entry : m_Entries)
		{
			entry.player.Stop();
			ReleaseRootWriters(entry.player.WriterId());
		}
		m_Entries.clear();
		return;
	}

	for (auto iterator = m_Entries.begin(); iterator != m_Entries.end();)
	{
		if (!storage->Contains(iterator->component))
		{
			iterator->player.Stop();
			ReleaseRootWriters(iterator->player.WriterId());
			iterator = m_Entries.erase(iterator);
		}
		else ++iterator;
	}

	const auto& components = storage->DenseData();
	const auto& headers = storage->Headers();
	for (std::size_t index = 0; index < components.size(); ++index)
	{
		const auto& component = components[index];
		const auto& header = headers[index];
		auto entry = std::find_if(m_Entries.begin(), m_Entries.end(), [&](const Entry& item)
		{
			return item.component == header.self;
		});
		if (entry == m_Entries.end())
		{
			if (!m_AssetLoader)
			{
				m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, header.stableGuid,
					"assetGuid", "Timeline runtime asset loader is not configured" });
				continue;
			}
			std::shared_ptr<const VansCompiledTimeline> timeline;
			std::string error;
			if (!m_AssetLoader(component, timeline, error))
			{
				m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, header.stableGuid,
					"assetGuid", "Failed to load Timeline asset: " + error });
				continue;
			}
			Entry created;
			created.component = header.self;
			created.owner = header.owner;
			if (!created.player.Load(timeline, component, m_World, header.owner, WriterId(header.self), error))
			{
				m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, header.stableGuid,
					"assetGuid", std::move(error) });
				continue;
			}
			created.loadedAssetGuid = component.assetGuid;
			created.attemptedAssetGuid = component.assetGuid;
			if (m_AssetRevisionQuery)
				m_AssetRevisionQuery(component.assetGuid, created.loadedRevision);
			created.attemptedRevision = created.loadedRevision;
			created.wasEnabled = header.effectiveEnabled;
			if (header.effectiveEnabled && (component.instance.playOn == VansTimelinePlayOn::Awake ||
				component.instance.playOn == VansTimelinePlayOn::Enable))
			{
				created.player.Play();
				VANS_LOG("[Timeline] Auto-play started component='" << header.stableGuid
					<< "' asset='" << component.assetGuid << "' trigger="
					<< (component.instance.playOn == VansTimelinePlayOn::Awake ? "Awake" : "Enable"));
			}
			m_Entries.push_back(std::move(created));
			continue;
		}

		RefreshEntryAsset(*entry, component, header.stableGuid);

		if (header.effectiveEnabled != entry->wasEnabled)
		{
			if (header.effectiveEnabled && component.instance.playOn == VansTimelinePlayOn::Enable)
				entry->player.Play();
			else if (!header.effectiveEnabled)
				entry->player.Stop();
			entry->wasEnabled = header.effectiveEnabled;
		}
		if (entry->player.ConsumeRestoreRequest())
			ReleaseRootWriters(entry->player.WriterId());
	}
	std::stable_sort(m_Entries.begin(), m_Entries.end(), [](const Entry& left, const Entry& right)
	{
		if (left.component.index != right.component.index) return left.component.index < right.component.index;
		return left.component.generation < right.component.generation;
	});
}

void VansTimelineRuntimeSystem::ApplyOutputs(std::vector<VansTimelineEvaluationOutput>& outputs)
{
	VansTimelineTrackAppliers::Apply(outputs, m_Adapters, m_PreAnimatedState, m_Diagnostics);
}

VansTimelinePlayer* VansTimelineRuntimeSystem::FindPlayerByWriterId(const std::string& writerId)
{
	for (Entry& entry : m_Entries)
		if (entry.player.WriterId() == writerId) return &entry.player;
	for (ChildEntry& child : m_Children)
		if (child.player.WriterId() == writerId) return &child.player;
	for (PreviewEntry& preview : m_Previews)
		if (preview.player.WriterId() == writerId) return &preview.player;
	return nullptr;
}

VansEntityHandle VansTimelineRuntimeSystem::FindOwnerByWriterId(const std::string& writerId) const
{
	for (const Entry& entry : m_Entries)
		if (entry.player.WriterId() == writerId) return entry.owner;
	for (const ChildEntry& child : m_Children)
		if (child.player.WriterId() == writerId) return child.owner;
	for (const PreviewEntry& preview : m_Previews)
		if (preview.player.WriterId() == writerId) return preview.owner;
	return {};
}

void VansTimelineRuntimeSystem::CollectPostScriptOutputs(
	std::vector<VansTimelineEvaluationOutput>& source,
	std::vector<VansTimelineEvaluationOutput>& destination,
	std::size_t depth)
{
	if (depth > 64)
	{
		m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, {}, "subTimeline",
			"Nested Timeline runtime depth exceeded 64" });
		return;
	}
	for (VansTimelineEvaluationOutput& output : source)
	{
		if (const auto* timeScale = std::get_if<VansTimelineTimeScaleOutput>(&output.value))
		{
			(void)timeScale;
			continue;
		}
		const auto* subTimeline = std::get_if<VansTimelineSubTimelineOutput>(&output.value);
		if (!subTimeline)
		{
			destination.push_back(std::move(output));
			continue;
		}

		const std::string key = output.writerId + ":" + output.propertyKey;
		auto child = std::find_if(m_Children.begin(), m_Children.end(), [&](const ChildEntry& entry)
		{
			return entry.key == key;
		});
		if (!subTimeline->active || subTimeline->exited)
		{
			if (child != m_Children.end())
			{
				child->player.Stop();
				ReleaseRootWriters(child->player.WriterId());
				m_Children.erase(child);
			}
			continue;
		}
		VansRuntimeTimelineComponent component;
		component.assetGuid = subTimeline->assetGuid;
		component.assetPath = subTimeline->assetPath;
		component.instance.playOn = VansTimelinePlayOn::Manual;
		component.instance.updateMode = VansTimelineUpdateMode::Manual;
		component.instance.bindingOverrides = subTimeline->config.bindingRemap;
		component.instance.parameters = subTimeline->config.parameterOverrides;
		component.instance.restoreStateOnStop = true;
		if (child == m_Children.end())
		{
			if (!m_AssetLoader || subTimeline->assetGuid.empty())
			{
				m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, output.sourceTrackId,
					"assetGuid", "SubTimeline requires an indexed Timeline asset GUID" });
				continue;
			}
			std::shared_ptr<const VansCompiledTimeline> compiled;
			std::string error;
			if (!m_AssetLoader(component, compiled, error))
			{
				m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, output.sourceTrackId,
					"assetGuid", "Failed to load SubTimeline: " + error });
				continue;
			}
			ChildEntry created;
			created.key = key;
			created.parentWriterId = output.rootWriterId;
			created.rootWriterId = output.rootWriterId;
			const auto parentChild = std::find_if(m_Children.begin(), m_Children.end(),
				[&](const ChildEntry& candidate)
				{
					return candidate.player.WriterId() == output.rootWriterId;
				});
			if (parentChild != m_Children.end())
				created.rootWriterId = parentChild->rootWriterId;
			created.owner = FindOwnerByWriterId(output.rootWriterId);
			created.hierarchicalBias = output.hierarchicalBias;
			created.loadedAssetGuid = component.assetGuid;
			created.attemptedAssetGuid = component.assetGuid;
			if (m_AssetRevisionQuery)
				m_AssetRevisionQuery(component.assetGuid, created.loadedRevision);
			created.attemptedRevision = created.loadedRevision;
			if (!created.owner.IsValid() || !created.player.Load(
				compiled, component, m_World, created.owner, output.writerId + "/" + output.propertyKey, error))
			{
				m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, output.sourceTrackId,
					"binding", error.empty() ? "SubTimeline owner is unavailable" : error });
				continue;
			}
			created.player.Play();
			m_Children.push_back(std::move(created));
			child = std::prev(m_Children.end());
		}
		else RefreshChildAsset(*child, component, output.sourceTrackId);
		child->hierarchicalBias = output.hierarchicalBias;

		child->player.SeekTicks(subTimeline->localTick, VansTimelineSeekPolicy::AllEdges,
			VansTimelineEvaluationReason::Playback);
		child->outputScratch.clear();
		if (child->player.UpdatePostScript(0.0, child->outputScratch))
			m_EvaluatedPostScriptRoots.insert(child->player.WriterId());
		for (VansTimelineEvaluationOutput& childOutput : child->outputScratch)
			childOutput.hierarchicalBias += output.hierarchicalBias;
		CollectPostScriptOutputs(child->outputScratch, destination, depth + 1);
	}
}

void VansTimelineRuntimeSystem::RemoveInactiveChildren()
{
	bool removed = true;
	while (removed)
	{
		removed = false;
		for (auto child = m_Children.begin(); child != m_Children.end(); ++child)
		{
			VansTimelinePlayer* parent = FindPlayerByWriterId(child->parentWriterId);
			if (parent && (parent->State() == VansTimelinePlayerState::Playing ||
				parent->State() == VansTimelinePlayerState::Paused))
				continue;
			child->player.Stop();
			ReleaseRootWriters(child->player.WriterId());
			m_Children.erase(child);
			removed = true;
			break;
		}
	}
}

void VansTimelineRuntimeSystem::UpdateRuntimePostScript(double deltaSeconds)
{
	SyncTimelineComponents();
	RemoveInactiveChildren();
	m_OutputScratch.clear();
	m_EvaluatedPostScriptRoots.clear();
	for (Entry& entry : m_Entries)
	{
		entry.outputScratch.clear();
		if (entry.player.UpdatePostScript(deltaSeconds, entry.outputScratch))
			m_EvaluatedPostScriptRoots.insert(entry.player.WriterId());
		CollectPostScriptOutputs(entry.outputScratch, m_OutputScratch);
		if (entry.player.ConsumeRestoreRequest())
			ReleaseRootWriters(entry.player.WriterId());
	}
	ApplyOutputs(m_OutputScratch);
	ReconcileActiveWriters(m_EvaluatedPostScriptRoots, m_OutputScratch, m_ActivePostScriptWriters);
}

void VansTimelineRuntimeSystem::UpdatePreviewsPostScript(double deltaSeconds)
{
	RemoveInactiveChildren();
	m_OutputScratch.clear();
	m_EvaluatedPostScriptRoots.clear();
	for (PreviewEntry& preview : m_Previews)
	{
		preview.outputScratch.clear();
		if (preview.player.UpdatePostScript(deltaSeconds, preview.outputScratch))
			m_EvaluatedPostScriptRoots.insert(preview.player.WriterId());
		CollectPostScriptOutputs(preview.outputScratch, m_OutputScratch);
		if (preview.player.ConsumeRestoreRequest())
			ReleaseRootWriters(preview.player.WriterId());
	}
	ApplyOutputs(m_OutputScratch);
	ReconcileActiveWriters(m_EvaluatedPostScriptRoots, m_OutputScratch, m_ActivePostScriptWriters);
}

void VansTimelineRuntimeSystem::UpdateRuntimeCamera(double deltaSeconds)
{
	(void)deltaSeconds;
	m_OutputScratch.clear();
	std::unordered_set<std::string> evaluatedRoots;
	std::unordered_set<std::string> runtimeRoots;
	for (Entry& entry : m_Entries)
	{
		runtimeRoots.insert(entry.player.WriterId());
		entry.outputScratch.clear();
		if (entry.player.UpdateCamera(entry.outputScratch))
			evaluatedRoots.insert(entry.player.WriterId());
		m_OutputScratch.insert(m_OutputScratch.end(),
			std::make_move_iterator(entry.outputScratch.begin()), std::make_move_iterator(entry.outputScratch.end()));
	}
	for (ChildEntry& child : m_Children)
	{
		if (runtimeRoots.find(child.rootWriterId) == runtimeRoots.end())
			continue;
		child.outputScratch.clear();
		if (child.player.UpdateCamera(child.outputScratch))
			evaluatedRoots.insert(child.player.WriterId());
		for (VansTimelineEvaluationOutput& childOutput : child.outputScratch)
			childOutput.hierarchicalBias += child.hierarchicalBias;
		m_OutputScratch.insert(m_OutputScratch.end(),
			std::make_move_iterator(child.outputScratch.begin()), std::make_move_iterator(child.outputScratch.end()));
	}
	ApplyOutputs(m_OutputScratch);
	ReconcileActiveWriters(evaluatedRoots, m_OutputScratch, m_ActiveCameraWriters);
}

void VansTimelineRuntimeSystem::UpdatePreviewsCamera(double deltaSeconds)
{
	(void)deltaSeconds;
	m_OutputScratch.clear();
	std::unordered_set<std::string> evaluatedRoots;
	std::unordered_set<std::string> previewRoots;
	for (PreviewEntry& preview : m_Previews)
	{
		previewRoots.insert(preview.player.WriterId());
		preview.outputScratch.clear();
		if (preview.player.UpdateCamera(preview.outputScratch))
			evaluatedRoots.insert(preview.player.WriterId());
		m_OutputScratch.insert(m_OutputScratch.end(),
			std::make_move_iterator(preview.outputScratch.begin()),
			std::make_move_iterator(preview.outputScratch.end()));
	}
	for (ChildEntry& child : m_Children)
	{
		if (previewRoots.find(child.rootWriterId) == previewRoots.end())
			continue;
		child.outputScratch.clear();
		if (child.player.UpdateCamera(child.outputScratch))
			evaluatedRoots.insert(child.player.WriterId());
		for (VansTimelineEvaluationOutput& childOutput : child.outputScratch)
			childOutput.hierarchicalBias += child.hierarchicalBias;
		m_OutputScratch.insert(m_OutputScratch.end(),
			std::make_move_iterator(child.outputScratch.begin()),
			std::make_move_iterator(child.outputScratch.end()));
	}
	ApplyOutputs(m_OutputScratch);
	ReconcileActiveWriters(evaluatedRoots, m_OutputScratch, m_ActiveCameraWriters);
}

void VansTimelineRuntimeSystem::UpdatePostScript(double deltaSeconds)
{
	UpdateRuntimePostScript(deltaSeconds);
	UpdatePreviewsPostScript(deltaSeconds);
}

void VansTimelineRuntimeSystem::UpdateCamera(double deltaSeconds)
{
	UpdateRuntimeCamera(deltaSeconds);
	UpdatePreviewsCamera(deltaSeconds);
}

void VansTimelineRuntimeSystem::ReconcileActiveWriters(
	const std::unordered_set<std::string>& evaluatedRoots,
	const std::vector<VansTimelineEvaluationOutput>& outputs,
	std::unordered_map<std::string, std::unordered_set<std::string>>& activeWriters)
{
	for (const std::string& root : evaluatedRoots)
	{
		std::unordered_set<std::string> current;
		for (const VansTimelineEvaluationOutput& output : outputs)
		{
			if (output.rootWriterId == root && output.retainsPreAnimatedState &&
				output.completionMode == VansTimelineCompletionMode::RestoreState)
				current.insert(output.writerId);
		}

		auto previous = activeWriters.find(root);
		if (previous != activeWriters.end())
		{
			for (const std::string& writer : previous->second)
				if (current.find(writer) == current.end())
					m_PreAnimatedState.ReleaseWriter(writer);
		}
		if (current.empty()) activeWriters.erase(root);
		else activeWriters[root] = std::move(current);
	}
}

void VansTimelineRuntimeSystem::ReleaseRootWriters(const std::string& rootWriterId)
{
	auto release = [&](auto& activeWriters)
	{
		const auto found = activeWriters.find(rootWriterId);
		if (found == activeWriters.end()) return;
		for (const std::string& writer : found->second)
			m_PreAnimatedState.ReleaseWriter(writer);
		activeWriters.erase(found);
	};
	release(m_ActivePostScriptWriters);
	release(m_ActiveCameraWriters);
}

void VansTimelineRuntimeSystem::StopAll()
{
	for (Entry& entry : m_Entries) entry.player.Stop();
	for (ChildEntry& child : m_Children) child.player.Stop();
	for (PreviewEntry& preview : m_Previews) preview.player.Stop();
	m_PreAnimatedState.RestoreAll();
	if (m_World)
		for (const PreviewEntry& preview : m_Previews)
			if (preview.ownsOwner && m_World->IsAlive(preview.owner)) m_World->DestroyEntity(preview.owner);
	m_Children.clear();
	m_Previews.clear();
}

void VansTimelineRuntimeSystem::Clear()
{
	StopAll();
	m_Entries.clear();
	m_Children.clear();
	m_Previews.clear();
	m_OutputScratch.clear();
	m_Diagnostics.clear();
	m_ActivePostScriptWriters.clear();
	m_ActiveCameraWriters.clear();
	m_EvaluatedPostScriptRoots.clear();
	m_World = nullptr;
}

VansTimelinePlayer* VansTimelineRuntimeSystem::FindPlayer(VansComponentHandle component)
{
	const auto found = std::find_if(m_Entries.begin(), m_Entries.end(), [&](const Entry& entry)
	{
		return entry.component == component;
	});
	return found == m_Entries.end() ? nullptr : &found->player;
}

bool VansTimelineRuntimeSystem::PlayComponent(VansComponentHandle component, bool restart)
{
	SyncTimelineComponents();
	VansTimelinePlayer* player = FindPlayer(component);
	if (!player || player->State() == VansTimelinePlayerState::Unloaded ||
		player->State() == VansTimelinePlayerState::Error)
		return false;
	if (restart)
	{
		player->Stop();
		ReleaseRootWriters(player->WriterId());
		player->Rewind();
	}
	if (player->State() == VansTimelinePlayerState::Paused)
		player->Resume();
	else
		player->Play();
	return true;
}

bool VansTimelineRuntimeSystem::PauseComponent(VansComponentHandle component)
{
	VansTimelinePlayer* player = FindPlayer(component);
	if (!player || player->State() != VansTimelinePlayerState::Playing) return false;
	player->Pause();
	return true;
}

bool VansTimelineRuntimeSystem::ResumeComponent(VansComponentHandle component)
{
	VansTimelinePlayer* player = FindPlayer(component);
	if (!player || player->State() != VansTimelinePlayerState::Paused) return false;
	player->Resume();
	return true;
}

bool VansTimelineRuntimeSystem::StopComponent(VansComponentHandle component)
{
	VansTimelinePlayer* player = FindPlayer(component);
	if (!player) return false;
	player->Stop();
	ReleaseRootWriters(player->WriterId());
	player->Rewind();
	return true;
}

bool VansTimelineRuntimeSystem::GetComponentState(
	VansComponentHandle component,
	VansTimelinePlayerState& state,
	VansTimelineTick& tick) const
{
	const auto found = std::find_if(m_Entries.begin(), m_Entries.end(), [&](const Entry& entry)
	{
		return entry.component == component;
	});
	if (found == m_Entries.end()) return false;
	state = found->player.State();
	tick = found->player.CurrentTick();
	return true;
}
}
