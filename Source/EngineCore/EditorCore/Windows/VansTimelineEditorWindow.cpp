#include "VansTimelineEditorWindow.h"

#include "../Timeline/VansTimelineTrackDescriptorRegistry.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../TimelineCore/VansTimelineSerialization.h"
#include "../../TimelineCore/VansTimelineSourceSchema.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace VansGraphics
{
namespace
{
void CopyText(char* destination, std::size_t capacity, const std::string& source)
{
	if (!capacity) return;
	const std::size_t count = std::min(capacity - 1, source.size());
	std::memcpy(destination, source.data(), count);
	destination[count] = '\0';
}

void RegenerateSectionIds(Vans::VansTimelineSection& section)
{
	section.id = Vans::VansTimelineEditService::NewStableId();
	for (Vans::VansTimelineChannel& channel : section.channels)
	{
		channel.id = Vans::VansTimelineEditService::NewStableId();
		for (Vans::VansTimelineKey& key : channel.keys)
			key.id = Vans::VansTimelineEditService::NewStableId();
	}
	for (Vans::VansTimelineRange& range : section.ranges)
		range.id = Vans::VansTimelineEditService::NewStableId();
}

Vans::VansTimelineValue DefaultValue(Vans::VansTimelineValueType type)
{
	switch (type)
	{
	case Vans::VansTimelineValueType::Bool: return false;
	case Vans::VansTimelineValueType::Int32: return std::int32_t{ 0 };
	case Vans::VansTimelineValueType::Int64: return std::int64_t{ 0 };
	case Vans::VansTimelineValueType::Float: return 0.0f;
	case Vans::VansTimelineValueType::Double: return 0.0;
	case Vans::VansTimelineValueType::Enum:
	case Vans::VansTimelineValueType::String: return std::string{};
	case Vans::VansTimelineValueType::Vec2: return Vans::VansTimelineVec2{};
	case Vans::VansTimelineValueType::Vec3: return Vans::VansTimelineVec3{};
	case Vans::VansTimelineValueType::Vec4: return Vans::VansTimelineVec4{};
	case Vans::VansTimelineValueType::Quaternion: return Vans::VansTimelineQuaternion{};
	case Vans::VansTimelineValueType::ColorLinear: return Vans::VansTimelineColorLinear{};
	case Vans::VansTimelineValueType::ColorSrgb: return Vans::VansTimelineColorSrgb{};
	case Vans::VansTimelineValueType::ObjectReference: return Vans::VansTimelineObjectReference{};
	case Vans::VansTimelineValueType::Struct:
		return Vans::VansTimelineStructValue{};
	default: return std::monostate{};
	}
}

const char* InterpolationName(Vans::VansTimelineInterpolation interpolation)
{
	switch (interpolation)
	{
	case Vans::VansTimelineInterpolation::Constant: return "Constant";
	case Vans::VansTimelineInterpolation::Linear: return "Linear";
	case Vans::VansTimelineInterpolation::Auto: return "Auto";
	case Vans::VansTimelineInterpolation::ClampedAuto: return "Clamped Auto";
	case Vans::VansTimelineInterpolation::Cubic: return "Cubic";
	case Vans::VansTimelineInterpolation::Bezier: return "Bezier";
	case Vans::VansTimelineInterpolation::Slerp: return "Slerp";
	default: return "Auto";
	}
}

ImU32 TrackColor(const Vans::VansTimelineTrack& track, bool selected, bool muted = false)
{
	ImVec4 color(track.display.color[0], track.display.color[1], track.display.color[2],
		muted ? 0.32f : track.display.color[3]);
	if (selected)
	{
		color.x = std::min(1.0f, color.x * 1.30f + 0.08f);
		color.y = std::min(1.0f, color.y * 1.30f + 0.08f);
		color.z = std::min(1.0f, color.z * 1.30f + 0.08f);
	}
	return ImGui::ColorConvertFloat4ToU32(color);
}

ImU32 ChannelColor(const Vans::VansTimelineTrack& track, bool selected)
{
	ImVec4 color(track.display.color[0], track.display.color[1], track.display.color[2], 0.75f);
	if (selected) color = ImVec4(0.96f, 0.78f, 0.26f, 1.0f);
	return ImGui::ColorConvertFloat4ToU32(color);
}
}

void VansTimelineEditorWindow::Open(const std::string& timelinePath, std::string ownerEntityGuid)
{
	Close();
	m_Path = timelinePath;
	m_InstanceOwnerGuid = std::move(ownerEntityGuid);
	const Vans::TimelineEditResult result = m_Edit.Open(m_Path);
	m_IsOpen = result.success;
	m_Selection = {};
	m_CanvasDrag = {};
	m_Playhead = 0;
	m_ViewStart = 0;
	m_VerticalScroll = 0.0f;
	m_CollapsedTracks.clear();
	if (m_IsOpen)
	{
		const auto duration = std::max<Vans::VansTimelineTick>(1, m_Edit.Asset().durationTicks);
		m_PixelsPerTick = std::clamp(640.0 / static_cast<double>(duration), 0.0002, 8.0);
	}
	if (!result) SetError(result.message);
}

void VansTimelineEditorWindow::Close()
{
	m_Preview.RestoreAndDetach();
	m_Edit.CancelInteraction();
	m_IsOpen = false;
	m_ActiveAPI = nullptr;
	m_Path.clear();
	m_CollapsedTracks.clear();
	m_TrackClipboard.reset();
	m_SectionClipboard.reset();
	m_CanvasDrag = {};
}

void VansTimelineEditorWindow::SetError(std::string message) { m_LastError = std::move(message); }

void VansTimelineEditorWindow::EnsurePreview()
{
	if (!m_ActiveAPI || m_ActiveAPI->GetPlayState() != Vans::EditorAPI::EnginePlayState::Edit ||
		m_Preview.State() != Vans::VansTimelinePreviewState::Detached) return;
	std::string error;
	if (!m_Preview.Attach(*m_ActiveAPI, m_Edit.Asset(), m_Path, m_InstanceOwnerGuid,
		m_PreviewSafeEvents, m_IncludeSubTimelines, 1, false, error)) SetError(std::move(error));
}

void VansTimelineEditorWindow::RefreshPreview()
{
	if (m_Preview.State() == Vans::VansTimelinePreviewState::Detached) return;
	std::string error;
	if (!m_Preview.Refresh(m_Edit.Asset(), error)) SetError(std::move(error));
}

void VansTimelineEditorWindow::SeekPreview(Vans::VansTimelineTick tick)
{
	m_Playhead = std::clamp(tick, m_Edit.Asset().playbackRange.startTick,
		m_Edit.Asset().playbackRange.endTick);
	EnsurePreview();
	if (m_Preview.State() == Vans::VansTimelinePreviewState::Detached) return;
	std::string error;
	if (!m_Preview.Seek(m_Playhead, error)) SetError(std::move(error));
}

bool VansTimelineEditorWindow::ApplyAsset(Vans::VansTimelineAsset asset)
{
	const Vans::TimelineEditResult result = m_Edit.ReplaceAsset(std::move(asset));
	if (!result) { SetError(result.message); return false; }
	RefreshPreview();
	return true;
}

Vans::VansTimelineTrack* VansTimelineEditorWindow::FindTrack(Vans::VansTimelineAsset& asset) const
{
	const Vans::VansTimelineId id = m_Selection.kind == SelectionKind::Track
		? m_Selection.id : m_Selection.trackId;
	const auto found = std::find_if(asset.tracks.begin(), asset.tracks.end(),
		[&](const auto& track) { return track.id == id; });
	return found == asset.tracks.end() ? nullptr : &*found;
}

Vans::VansTimelineSection* VansTimelineEditorWindow::FindSection(Vans::VansTimelineAsset& asset) const
{
	Vans::VansTimelineTrack* track = FindTrack(asset);
	if (!track) return nullptr;
	const Vans::VansTimelineId id = m_Selection.kind == SelectionKind::Section
		? m_Selection.id : m_Selection.sectionId;
	const auto found = std::find_if(track->sections.begin(), track->sections.end(),
		[&](const auto& section) { return section.id == id; });
	return found == track->sections.end() ? nullptr : &*found;
}

Vans::VansTimelineChannel* VansTimelineEditorWindow::FindChannel(Vans::VansTimelineAsset& asset) const
{
	Vans::VansTimelineSection* section = FindSection(asset);
	if (!section || m_Selection.channelIndex >= section->channels.size()) return nullptr;
	return &section->channels[m_Selection.channelIndex];
}

Vans::VansTimelineKey* VansTimelineEditorWindow::FindKey(Vans::VansTimelineAsset& asset) const
{
	Vans::VansTimelineChannel* channel = FindChannel(asset);
	if (!channel) return nullptr;
	const auto found = std::find_if(channel->keys.begin(), channel->keys.end(),
		[&](const auto& key) { return key.id == m_Selection.id; });
	return found == channel->keys.end() ? nullptr : &*found;
}

std::vector<VansTimelineEditorWindow::TimelineRow> VansTimelineEditorWindow::BuildRows() const
{
	std::vector<TimelineRow> rows;
	const auto& tracks = m_Edit.Asset().tracks;
	for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex)
	{
		rows.push_back({ TimelineRowKind::Track, trackIndex, 0, 0 });
		if (m_CollapsedTracks.find(tracks[trackIndex].id) != m_CollapsedTracks.end()) continue;
		for (std::size_t sectionIndex = 0; sectionIndex < tracks[trackIndex].sections.size(); ++sectionIndex)
		{
			rows.push_back({ TimelineRowKind::Section, trackIndex, sectionIndex, 0 });
			for (std::size_t channelIndex = 0;
				channelIndex < tracks[trackIndex].sections[sectionIndex].channels.size(); ++channelIndex)
				rows.push_back({ TimelineRowKind::Channel, trackIndex, sectionIndex, channelIndex });
		}
	}
	return rows;
}

double VansTimelineEditorWindow::TickToX(Vans::VansTimelineTick tick, double originX) const
{
	return originX + static_cast<double>(tick - m_ViewStart) * m_PixelsPerTick;
}

Vans::VansTimelineTick VansTimelineEditorWindow::XToTick(double x, double originX) const
{
	return m_ViewStart + static_cast<Vans::VansTimelineTick>(std::llround((x - originX) / m_PixelsPerTick));
}

std::string VansTimelineEditorWindow::FormatTick(Vans::VansTimelineTick tick) const
{
	return Vans::VansTimelineTime::FormatTimecode(tick, m_Edit.Asset().timebase, false);
}

Vans::VansTimelineTick VansTimelineEditorWindow::SnapTick(Vans::VansTimelineTick tick) const
{
	if (!m_SnapEnabled || ImGui::GetIO().KeyAlt) return tick;
	Vans::VansTimelineTick best = tick;
	Vans::VansTimelineTick distance = std::numeric_limits<Vans::VansTimelineTick>::max();
	const auto consider = [&](Vans::VansTimelineTick candidate)
	{
		const auto candidateDistance = std::llabs(candidate - tick);
		if (candidateDistance < distance) { best = candidate; distance = candidateDistance; }
	};
	const auto frameTicks = std::max<Vans::VansTimelineTick>(1,
		Vans::VansTimelineTime::FrameToTick(1, m_Edit.Asset().timebase));
	consider(Vans::VansTimelineTime::FrameToTick(
		Vans::VansTimelineTime::TickToFrame(tick, m_Edit.Asset().timebase), m_Edit.Asset().timebase));
	consider(m_Playhead);
	for (const auto& marker : m_Edit.Asset().markers) consider(marker.tick);
	for (const auto& track : m_Edit.Asset().tracks)
		for (const auto& section : track.sections)
		{
			consider(section.startTick);
			consider(section.startTick + section.durationTicks);
			const auto local = tick - section.startTick;
			for (const auto& channel : section.channels)
				for (const auto& key : channel.keys) consider(section.startTick + key.tick);
		}
	consider(m_Edit.Asset().playbackRange.startTick);
	consider(m_Edit.Asset().playbackRange.endTick);
	const auto threshold = std::max<Vans::VansTimelineTick>(1,
		static_cast<Vans::VansTimelineTick>(8.0 / std::max(0.0002, m_PixelsPerTick)));
	return distance <= threshold ? best : tick;
}

void VansTimelineEditorWindow::DrawToolbar()
{
	if (m_ActiveAPI && m_ActiveAPI->GetPlayState() != Vans::EditorAPI::EnginePlayState::Edit &&
		m_Preview.State() != Vans::VansTimelinePreviewState::Detached)
	{
		m_Preview.RestoreAndDetach();
	}
	if (ImGui::Button("Save"))
	{
		const auto result = m_Edit.Save(*m_ActiveAPI);
		if (!result) SetError(result.message);
	}
	ImGui::SameLine();
	if (ImGui::Button("Undo")) { const auto result = m_Edit.Undo(); if (!result) SetError(result.message); else RefreshPreview(); }
	ImGui::SameLine();
	if (ImGui::Button("Redo")) { const auto result = m_Edit.Redo(); if (!result) SetError(result.message); else RefreshPreview(); }
	ImGui::SameLine();
	if (ImGui::Button("Add Track")) ImGui::OpenPopup("Timeline.AddTrack");
	DrawAddTrackMenu();
	ImGui::SameLine();
	const bool canAddKey = m_Selection.kind == SelectionKind::Channel || m_Selection.kind == SelectionKind::Key;
	ImGui::BeginDisabled(!canAddKey);
	if (ImGui::Button("Add Key")) AddKeyAtPlayhead();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Add Marker"))
	{
		Vans::VansTimelineMarker marker;
		marker.tick = m_Playhead;
		marker.label = "Marker";
		const auto result = m_Edit.AddMarker(std::move(marker));
		if (!result) SetError(result.message);
		else { m_Selection = { SelectionKind::Marker, result.objectId }; RefreshPreview(); }
	}
	ImGui::SameLine();
	if (ImGui::Button("Frame All"))
	{
		m_ViewStart = m_Edit.Asset().playbackRange.startTick;
		m_PixelsPerTick = std::clamp(static_cast<double>(m_LastCanvasWidth - 12.0f) /
			std::max<Vans::VansTimelineTick>(1, m_Edit.Asset().playbackRange.endTick -
			m_Edit.Asset().playbackRange.startTick), 0.0002, 8.0);
	}
	ImGui::SameLine();
	ImGui::Checkbox("Snap", &m_SnapEnabled);
	ImGui::SameLine();
	const bool playing = m_Preview.State() == Vans::VansTimelinePreviewState::Playing;
	if (ImGui::Button(playing ? "Pause" : "Play"))
	{
		EnsurePreview();
		std::string error;
		const bool success = playing ? m_Preview.Pause(error) : m_Preview.Play(error);
		if (!success) SetError(std::move(error));
	}
	ImGui::SameLine();
	if (ImGui::Button("Restore")) m_Preview.RestoreAndDetach();
	ImGui::SameLine();
	ImGui::Checkbox("Safe events", &m_PreviewSafeEvents);
	ImGui::SameLine();
	ImGui::Checkbox("SubTimelines", &m_IncludeSubTimelines);
}

void VansTimelineEditorWindow::DrawAddTrackMenu()
{
	if (!ImGui::BeginPopup("Timeline.AddTrack")) return;
	for (const Vans::VansTimelineTrackDescriptor& descriptor : Vans::VansTimelineTrackDescriptorRegistry::All())
	{
		if (descriptor.bindingRequired)
		{
			if (!ImGui::BeginMenu(descriptor.displayName.c_str())) continue;
			if (m_Edit.Asset().bindings.empty()) ImGui::TextDisabled("Create a binding first");
			for (const Vans::VansTimelineBinding& binding : m_Edit.Asset().bindings)
				if (ImGui::MenuItem(binding.displayName.empty() ? binding.id.c_str() : binding.displayName.c_str()))
				{
					const auto result = m_Edit.AddTrack(descriptor.typeId, binding.id);
					if (!result) SetError(result.message);
					else { m_Selection = { SelectionKind::Track, result.objectId }; RefreshPreview(); }
				}
			ImGui::EndMenu();
		}
		else if (ImGui::MenuItem(descriptor.displayName.c_str()))
		{
			const auto result = m_Edit.AddTrack(descriptor.typeId, {});
			if (!result) SetError(result.message);
			else { m_Selection = { SelectionKind::Track, result.objectId }; RefreshPreview(); }
		}
	}
	ImGui::EndPopup();
}

void VansTimelineEditorWindow::AddKey(const Vans::VansTimelineId& trackId,
	const Vans::VansTimelineId& sectionId, std::size_t channelIndex, Vans::VansTimelineTick globalTick)
{
	Vans::VansTimelineTrack* track = nullptr;
	for (auto& candidate : m_Edit.PreviewAsset().tracks) if (candidate.id == trackId) { track = &candidate; break; }
	if (!track) return;
	Vans::VansTimelineSection* section = nullptr;
	for (auto& candidate : track->sections) if (candidate.id == sectionId) { section = &candidate; break; }
	if (!section || channelIndex >= section->channels.size()) return;
	Vans::VansTimelineChannel& channel = section->channels[channelIndex];
	const auto localTick = std::clamp(globalTick - section->startTick, Vans::VansTimelineTick{ 0 },
		std::max<Vans::VansTimelineTick>(0, section->durationTicks - 1));
	const auto existing = std::find_if(channel.keys.begin(), channel.keys.end(),
		[&](const auto& key) { return key.tick == localTick; });
	if (existing != channel.keys.end())
	{
		m_Selection = { SelectionKind::Key, existing->id, trackId, sectionId, channelIndex };
		return;
	}
	Vans::VansTimelineKey key;
	key.tick = localTick;
	key.value = DefaultValue(channel.type);
	if (!channel.keys.empty())
	{
		const auto next = std::lower_bound(channel.keys.begin(), channel.keys.end(), localTick,
			[](const auto& item, auto tick) { return item.tick < tick; });
		key.value = next == channel.keys.begin() ? next->value : std::prev(next)->value;
	}
	const auto result = m_Edit.AddKey(trackId, sectionId, channelIndex, std::move(key));
	if (!result) SetError(result.message);
	else { m_Selection = { SelectionKind::Key, result.objectId, trackId, sectionId, channelIndex }; RefreshPreview(); }
}

void VansTimelineEditorWindow::AddKeyAtPlayhead()
{
	if (m_Selection.kind != SelectionKind::Channel && m_Selection.kind != SelectionKind::Key) return;
	AddKey(m_Selection.trackId, m_Selection.sectionId, m_Selection.channelIndex, m_Playhead);
}

void VansTimelineEditorWindow::DrawOutliner()
{
	if (ImGui::Selectable("Timeline", m_Selection.kind == SelectionKind::Asset)) m_Selection = {};
	ImGui::Separator();
	const auto rows = BuildRows();
	if (ImGui::BeginChild("Timeline.Outliner.Rows", ImVec2(0, 0), false))
	{
		ImGui::SetScrollY(m_VerticalScroll);
		const float rowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		std::optional<std::pair<Vans::VansTimelineId, bool>> pendingLock;
		std::optional<Vans::VansTimelineId> pendingSectionTrack;
		for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
		{
			const TimelineRow& row = rows[rowIndex];
			const auto& track = m_Edit.Asset().tracks[row.trackIndex];
			Vans::VansTimelineId rowId = track.id;
			if (row.kind == TimelineRowKind::Section) rowId = track.sections[row.sectionIndex].id;
			if (row.kind == TimelineRowKind::Channel) rowId = track.sections[row.sectionIndex].channels[row.channelIndex].id;
			const bool selected = row.kind == TimelineRowKind::Track
				? m_Selection.kind == SelectionKind::Track && m_Selection.id == rowId
				: row.kind == TimelineRowKind::Section
				? m_Selection.kind == SelectionKind::Section && m_Selection.id == rowId
				: (m_Selection.kind == SelectionKind::Channel || m_Selection.kind == SelectionKind::Key) &&
				m_Selection.sectionId == track.sections[row.sectionIndex].id &&
				m_Selection.channelIndex == row.channelIndex;
			const ImVec2 position = ImGui::GetCursorScreenPos();
			ImGui::PushID(static_cast<int>(rowIndex));
			ImGui::InvisibleButton("##TimelineRow", ImVec2(rowWidth, m_RowHeight));
			const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
			const bool hovered = ImGui::IsItemHovered();
			ImDrawList* draw = ImGui::GetWindowDrawList();
			if (selected) draw->AddRectFilled(position, ImVec2(position.x + rowWidth, position.y + m_RowHeight),
				IM_COL32(62, 77, 94, 180));
			draw->AddLine(ImVec2(position.x, position.y + m_RowHeight),
				ImVec2(position.x + rowWidth, position.y + m_RowHeight), IM_COL32(47, 51, 58, 200));
			float textX = position.x + (row.kind == TimelineRowKind::Track ? 7.0f : row.kind == TimelineRowKind::Section ? 27.0f : 48.0f);
			if (row.kind == TimelineRowKind::Track)
			{
				const bool collapsed = m_CollapsedTracks.find(track.id) != m_CollapsedTracks.end();
				const ImVec2 triangle[3] = { ImVec2(position.x + 8.0f, position.y + 10.0f),
					ImVec2(position.x + 8.0f, position.y + 18.0f),
					ImVec2(position.x + (collapsed ? 15.0f : 18.0f), position.y + 14.0f) };
				draw->AddTriangleFilled(triangle[0], triangle[1], triangle[2], IM_COL32(190, 198, 208, 255));
				draw->AddRectFilled(ImVec2(position.x + 21.0f, position.y + 6.0f),
					ImVec2(position.x + 24.0f, position.y + m_RowHeight - 6.0f), TrackColor(track, selected));
			}
			const std::string& label = row.kind == TimelineRowKind::Track ?
				(track.name.empty() ? track.type.stableName : track.name) :
				row.kind == TimelineRowKind::Section ? track.sections[row.sectionIndex].name :
				track.sections[row.sectionIndex].channels[row.channelIndex].name;
			draw->AddText(ImVec2(textX, position.y + 6.0f), row.kind == TimelineRowKind::Channel
				? IM_COL32(176, 184, 194, 255) : IM_COL32(236, 239, 244, 255), label.c_str());
			if (row.kind == TimelineRowKind::Track)
			{
				if (track.locked) draw->AddText(ImVec2(position.x + rowWidth - 60.0f, position.y + 6.0f),
					IM_COL32(236, 191, 112, 255), "L");
				draw->AddText(ImVec2(position.x + rowWidth - 39.0f, position.y + 5.0f),
					IM_COL32(167, 205, 235, 255), "+");
				if (clicked)
				{
					const float localX = ImGui::GetIO().MousePos.x - position.x;
					if (localX < 23.0f)
					{
						if (m_CollapsedTracks.erase(track.id) == 0) m_CollapsedTracks.insert(track.id);
					}
					else if (localX > rowWidth - 66.0f && localX < rowWidth - 20.0f)
						pendingLock = std::make_pair(track.id, !track.locked);
					else if (localX >= rowWidth - 20.0f)
						pendingSectionTrack = track.id;
					else m_Selection = { SelectionKind::Track, track.id };
				}
			}
			else if (clicked)
			{
				if (row.kind == TimelineRowKind::Section)
					m_Selection = { SelectionKind::Section, rowId, track.id };
				else
				{
					m_Selection = { SelectionKind::Channel, rowId, track.id,
						track.sections[row.sectionIndex].id, row.channelIndex };
				}
			}
			if (row.kind == TimelineRowKind::Channel)
				draw->AddText(ImVec2(position.x + rowWidth - 39.0f, position.y + 5.0f),
					IM_COL32(167, 205, 235, 255), "+");
			if (row.kind == TimelineRowKind::Channel && clicked &&
				ImGui::GetIO().MousePos.x - position.x >= rowWidth - 20.0f)
				pendingSectionTrack = track.id + "\n" + track.sections[row.sectionIndex].id + "\n" +
					std::to_string(row.channelIndex);
			if (hovered && ImGui::BeginDragDropTarget())
			{
				if (row.kind == TimelineRowKind::Track)
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Timeline.Track"))
					{
						const std::string source(static_cast<const char*>(payload->Data));
						if (source != track.id)
						{
							const auto result = m_Edit.MoveTrack(source, track.groupId, track.id);
							if (!result) SetError(result.message); else RefreshPreview();
						}
					}
				ImGui::EndDragDropTarget();
			}
			if (row.kind == TimelineRowKind::Track && ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("Timeline.Track", track.id.c_str(), track.id.size() + 1);
				ImGui::TextUnformatted(label.c_str());
				ImGui::EndDragDropSource();
			}
			ImGui::PopID();
		}
		m_VerticalScroll = ImGui::GetScrollY();
		ImGui::EndChild();
		if (pendingLock)
		{
			const auto result = m_Edit.SetTrackLocked(pendingLock->first, pendingLock->second);
			if (!result) SetError(result.message); else RefreshPreview();
		}
		if (pendingSectionTrack && pendingSectionTrack->find('\n') == std::string::npos)
		{
			Vans::VansTimelineSection section;
			section.startTick = std::clamp(m_Playhead, Vans::VansTimelineTick{ 0 },
				std::max<Vans::VansTimelineTick>(0, m_Edit.Asset().durationTicks - 1));
			section.durationTicks = std::min<Vans::VansTimelineTick>(
				std::max<Vans::VansTimelineTick>(1, m_Edit.Asset().durationTicks / 10),
				m_Edit.Asset().durationTicks - section.startTick);
			const auto result = m_Edit.AddSection(*pendingSectionTrack, std::move(section));
			if (!result) SetError(result.message);
			else { m_Selection = { SelectionKind::Section, result.objectId, *pendingSectionTrack }; RefreshPreview(); }
		}
		else if (pendingSectionTrack)
		{
			std::stringstream stream(*pendingSectionTrack);
			std::string trackId, sectionId, channelIndexText;
			std::getline(stream, trackId); std::getline(stream, sectionId); std::getline(stream, channelIndexText);
			AddKey(trackId, sectionId, static_cast<std::size_t>(std::stoull(channelIndexText)), m_Playhead);
		}
	}
	if (!m_Edit.Asset().markers.empty() && ImGui::TreeNode("Markers"))
	{
		for (const auto& marker : m_Edit.Asset().markers)
			if (ImGui::Selectable(marker.label.empty() ? marker.id.c_str() : marker.label.c_str(),
				m_Selection.kind == SelectionKind::Marker && m_Selection.id == marker.id))
				m_Selection = { SelectionKind::Marker, marker.id };
		ImGui::TreePop();
	}
}

void VansTimelineEditorWindow::HandleCanvasDrag()
{
	if (m_CanvasDrag.kind == CanvasDragKind::None) return;
	if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		const auto delta = static_cast<Vans::VansTimelineTick>(std::llround(
			(ImGui::GetIO().MousePos.x - m_CanvasDrag.initialMouseX) / std::max(0.0002, m_PixelsPerTick)));
		Vans::TimelineEditResult result{ true, {}, {} };
		if (m_CanvasDrag.kind == CanvasDragKind::MoveSection)
			result = m_Edit.MoveSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId,
				SnapTick(m_CanvasDrag.initialStartTick + delta));
		else if (m_CanvasDrag.kind == CanvasDragKind::TrimSectionStart)
		{
			const auto end = m_CanvasDrag.initialStartTick + m_CanvasDrag.initialDurationTicks;
			const auto start = std::min(SnapTick(m_CanvasDrag.initialStartTick + delta), end - 1);
			result = m_Edit.TrimSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId, start, end - start);
		}
		else if (m_CanvasDrag.kind == CanvasDragKind::TrimSectionEnd)
		{
			const auto end = std::max(SnapTick(m_CanvasDrag.initialStartTick +
				m_CanvasDrag.initialDurationTicks + delta), m_CanvasDrag.initialStartTick + 1);
			result = m_Edit.TrimSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId,
				m_CanvasDrag.initialStartTick, end - m_CanvasDrag.initialStartTick);
		}
		else if (m_CanvasDrag.kind == CanvasDragKind::MoveKey)
		{
			const auto global = SnapTick(m_CanvasDrag.initialStartTick + m_CanvasDrag.initialLocalTick + delta);
			result = m_Edit.MoveKey(m_CanvasDrag.trackId, m_CanvasDrag.sectionId,
				m_CanvasDrag.channelIndex, m_CanvasDrag.objectId, global - m_CanvasDrag.initialStartTick);
		}
		else if (m_CanvasDrag.kind == CanvasDragKind::MoveMarker)
			result = m_Edit.MoveMarker(m_CanvasDrag.objectId, SnapTick(m_CanvasDrag.initialLocalTick + delta));
		if (!result)
		{
			SetError(result.message);
			m_Edit.CancelInteraction();
			m_CanvasDrag = {};
		}
		else RefreshPreview();
	}
	else
	{
		const auto result = m_Edit.CommitInteraction();
		if (!result) { SetError(result.message); m_Edit.CancelInteraction(); }
		else { m_LastError.clear(); RefreshPreview(); }
		m_CanvasDrag = {};
	}
}

void VansTimelineEditorWindow::DrawTimeline()
{
	HandleCanvasDrag();
	const auto rows = BuildRows();
	const ImVec2 rulerPosition = ImGui::GetCursorScreenPos();
	const ImVec2 rulerSize(std::max(1.0f, ImGui::GetContentRegionAvail().x), 29.0f);
	m_LastCanvasWidth = rulerSize.x;
	ImGui::InvisibleButton("Timeline.Ruler", rulerSize);
	const bool rulerHovered = ImGui::IsItemHovered();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(rulerPosition, ImVec2(rulerPosition.x + rulerSize.x, rulerPosition.y + rulerSize.y), IM_COL32(35, 39, 46, 255));
	const auto frameTicks = std::max<Vans::VansTimelineTick>(1,
		Vans::VansTimelineTime::FrameToTick(1, m_Edit.Asset().timebase));
	Vans::VansTimelineTick gridStep = frameTicks;
	while (gridStep * m_PixelsPerTick < 58.0) gridStep *= 5;
	const auto firstGrid = static_cast<Vans::VansTimelineTick>(std::floor(
		static_cast<double>(m_ViewStart) / gridStep) * gridStep);
	for (Vans::VansTimelineTick tick = firstGrid; tick <= m_Edit.Asset().durationTicks + gridStep; tick += gridStep)
	{
		const float x = static_cast<float>(TickToX(tick, rulerPosition.x));
		if (x < rulerPosition.x - 1.0f || x > rulerPosition.x + rulerSize.x + 1.0f) continue;
		draw->AddLine(ImVec2(x, rulerPosition.y), ImVec2(x, rulerPosition.y + rulerSize.y), IM_COL32(101, 108, 119, 145));
		draw->AddText(ImVec2(x + 4.0f, rulerPosition.y + 6.0f), IM_COL32(205, 210, 219, 255), FormatTick(tick).c_str());
	}
	const float rulerPlayheadX = static_cast<float>(TickToX(m_Playhead, rulerPosition.x));
	draw->AddLine(ImVec2(rulerPlayheadX, rulerPosition.y), ImVec2(rulerPlayheadX, rulerPosition.y + rulerSize.y), IM_COL32(242, 91, 82, 255), 2.0f);
	if (rulerHovered)
	{
		if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f)
		{
			const auto anchor = XToTick(ImGui::GetIO().MousePos.x, rulerPosition.x);
			m_PixelsPerTick = std::clamp(m_PixelsPerTick * (ImGui::GetIO().MouseWheel > 0.0f ? 1.20 : 0.80), 0.0002, 8.0);
			m_ViewStart = anchor - static_cast<Vans::VansTimelineTick>((ImGui::GetIO().MousePos.x - rulerPosition.x) / m_PixelsPerTick);
		}
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			bool markerHit = false;
			for (const auto& marker : m_Edit.Asset().markers)
			{
				const float x = static_cast<float>(TickToX(marker.tick, rulerPosition.x));
				if (std::abs(ImGui::GetIO().MousePos.x - x) > 7.0f) continue;
				markerHit = true;
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					m_Selection = { SelectionKind::Marker, marker.id };
					if (m_Edit.BeginInteraction()) m_CanvasDrag = { CanvasDragKind::MoveMarker, {}, {}, marker.id, 0, 0, 0, marker.tick, ImGui::GetIO().MousePos.x };
				}
				break;
			}
			if (!markerHit) SeekPreview(SnapTick(XToTick(ImGui::GetIO().MousePos.x, rulerPosition.x)));
		}
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))
			m_ViewStart -= static_cast<Vans::VansTimelineTick>(ImGui::GetIO().MouseDelta.x / m_PixelsPerTick);
	}
	ImGui::Separator();
	if (ImGui::BeginChild("Timeline.Canvas.Rows", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::SetScrollY(m_VerticalScroll);
		const ImVec2 canvasPosition = ImGui::GetCursorScreenPos();
		const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
		const float totalHeight = std::max(canvasSize.y, m_RowHeight * static_cast<float>(rows.size()) + 6.0f);
		ImGui::InvisibleButton("Timeline.Canvas.Interaction", ImVec2(std::max(1.0f, canvasSize.x), totalHeight));
		const bool hovered = ImGui::IsItemHovered();
		draw = ImGui::GetWindowDrawList();
		const ImVec2 clipMin = ImGui::GetWindowPos();
		const ImVec2 clipMax = ImVec2(clipMin.x + ImGui::GetWindowWidth(), clipMin.y + ImGui::GetWindowHeight());
		draw->PushClipRect(clipMin, clipMax, true);
		const auto rangeX = [&](Vans::VansTimelineTick tick)
		{ return static_cast<float>(TickToX(tick, canvasPosition.x)); };
		const float workStartX = rangeX(m_Edit.Asset().workRange.startTick);
		const float workEndX = rangeX(m_Edit.Asset().workRange.endTick);
		if (workEndX > workStartX) draw->AddRectFilled(ImVec2(workStartX, clipMin.y), ImVec2(workEndX, clipMax.y), IM_COL32(42, 46, 53, 120));
		for (Vans::VansTimelineTick tick = firstGrid; tick <= m_Edit.Asset().durationTicks + gridStep; tick += gridStep)
		{
			const float x = rangeX(tick);
			if (x >= clipMin.x && x <= clipMax.x) draw->AddLine(ImVec2(x, clipMin.y), ImVec2(x, clipMax.y), IM_COL32(75, 81, 91, 115));
		}
		bool canvasObjectClicked = false;
		std::optional<std::tuple<std::string, std::string, std::size_t, Vans::VansTimelineTick>> pendingKey;
		for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
		{
			const TimelineRow& row = rows[rowIndex];
			const auto& track = m_Edit.Asset().tracks[row.trackIndex];
			const float y = canvasPosition.y + static_cast<float>(rowIndex) * m_RowHeight;
			if (y + m_RowHeight < clipMin.y || y > clipMax.y) continue;
			const bool selectedTrack = m_Selection.kind == SelectionKind::Track && m_Selection.id == track.id;
			if (row.kind == TimelineRowKind::Track)
			{
				draw->AddRectFilled(ImVec2(clipMin.x, y), ImVec2(clipMax.x, y + m_RowHeight), IM_COL32(31, 34, 40, 220));
				for (const auto& section : track.sections)
				{
					const float x0 = rangeX(section.startTick), x1 = rangeX(section.startTick + section.durationTicks);
					const bool selected = m_Selection.kind == SelectionKind::Section && m_Selection.id == section.id;
					const ImVec2 minimum(std::max(clipMin.x, x0), y + 4.0f), maximum(std::min(clipMax.x, x1), y + m_RowHeight - 4.0f);
					if (maximum.x <= minimum.x) continue;
					draw->AddRectFilled(minimum, maximum, TrackColor(track, selected || selectedTrack, track.locked), 3.0f);
					draw->AddRect(minimum, maximum, selected ? IM_COL32(249, 241, 185, 255) : IM_COL32(20, 22, 26, 255), 3.0f, 0, selected ? 2.0f : 1.0f);
					draw->PushClipRect(minimum, maximum, true);
					draw->AddText(ImVec2(minimum.x + 6.0f, minimum.y + 4.0f), IM_COL32(245, 247, 250, 255), section.name.c_str());
					draw->PopClipRect();
					if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
						ImGui::GetIO().MousePos.x >= minimum.x && ImGui::GetIO().MousePos.x <= maximum.x &&
						ImGui::GetIO().MousePos.y >= minimum.y && ImGui::GetIO().MousePos.y <= maximum.y)
					{
						m_Selection = { SelectionKind::Section, section.id, track.id };
						canvasObjectClicked = true;
					}
				}
			}
			else if (row.kind == TimelineRowKind::Section)
			{
				const auto& section = track.sections[row.sectionIndex];
				const float x0 = rangeX(section.startTick), x1 = rangeX(section.startTick + section.durationTicks);
				const bool selected = m_Selection.kind == SelectionKind::Section && m_Selection.id == section.id;
				draw->AddRectFilled(ImVec2(clipMin.x, y), ImVec2(clipMax.x, y + m_RowHeight), IM_COL32(35, 38, 45, 220));
				const ImVec2 minimum(std::max(clipMin.x, x0), y + 3.0f), maximum(std::min(clipMax.x, x1), y + m_RowHeight - 3.0f);
				if (maximum.x > minimum.x)
				{
					draw->AddRectFilled(minimum, maximum, TrackColor(track, selected, track.locked), 3.0f);
					draw->AddRect(minimum, maximum, selected ? IM_COL32(249, 241, 185, 255) : IM_COL32(20, 22, 26, 255), 3.0f, 0, selected ? 2.0f : 1.0f);
					draw->AddText(ImVec2(minimum.x + 6.0f, minimum.y + 4.0f), IM_COL32(245, 247, 250, 255), section.name.c_str());
					if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
						ImGui::GetIO().MousePos.x >= minimum.x && ImGui::GetIO().MousePos.x <= maximum.x &&
						ImGui::GetIO().MousePos.y >= minimum.y && ImGui::GetIO().MousePos.y <= maximum.y)
					{
						m_Selection = { SelectionKind::Section, section.id, track.id };
						canvasObjectClicked = true;
						if (!track.locked && !section.locked && m_Edit.BeginInteraction())
						{
							CanvasDragKind kind = CanvasDragKind::MoveSection;
							if (std::abs(ImGui::GetIO().MousePos.x - x0) <= 7.0f) kind = CanvasDragKind::TrimSectionStart;
							else if (std::abs(ImGui::GetIO().MousePos.x - x1) <= 7.0f) kind = CanvasDragKind::TrimSectionEnd;
							m_CanvasDrag = { kind, track.id, section.id, {}, 0, section.startTick,
								section.durationTicks, 0, ImGui::GetIO().MousePos.x };
						}
					}
				}
			}
			else
			{
				const auto& section = track.sections[row.sectionIndex];
				const auto& channel = section.channels[row.channelIndex];
				const float x0 = rangeX(section.startTick), x1 = rangeX(section.startTick + section.durationTicks);
				const bool selected = (m_Selection.kind == SelectionKind::Channel || m_Selection.kind == SelectionKind::Key) &&
					m_Selection.sectionId == section.id && m_Selection.channelIndex == row.channelIndex;
				draw->AddRectFilled(ImVec2(clipMin.x, y), ImVec2(clipMax.x, y + m_RowHeight), IM_COL32(28, 31, 36, 185));
				draw->AddLine(ImVec2(std::max(clipMin.x, x0), y + m_RowHeight * 0.5f),
					ImVec2(std::min(clipMax.x, x1), y + m_RowHeight * 0.5f), ChannelColor(track, selected), 2.0f);
				for (const auto& key : channel.keys)
				{
					const float keyX = rangeX(section.startTick + key.tick);
					if (keyX < clipMin.x - 8.0f || keyX > clipMax.x + 8.0f) continue;
					const bool keySelected = m_Selection.kind == SelectionKind::Key && m_Selection.id == key.id;
					const ImVec2 center(keyX, y + m_RowHeight * 0.5f);
					const ImVec2 diamond[4] = { ImVec2(center.x, center.y - 6.0f), ImVec2(center.x + 6.0f, center.y),
						ImVec2(center.x, center.y + 6.0f), ImVec2(center.x - 6.0f, center.y) };
					draw->AddQuadFilled(diamond[0], diamond[1], diamond[2], diamond[3],
						keySelected ? IM_COL32(255, 237, 152, 255) : IM_COL32(242, 198, 77, 255));
					if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
						std::abs(ImGui::GetIO().MousePos.x - keyX) <= 8.0f &&
						std::abs(ImGui::GetIO().MousePos.y - center.y) <= 8.0f)
					{
						m_Selection = { SelectionKind::Key, key.id, track.id, section.id, row.channelIndex };
						canvasObjectClicked = true;
						if (!track.locked && !section.locked && m_Edit.BeginInteraction())
							m_CanvasDrag = { CanvasDragKind::MoveKey, track.id, section.id, key.id, row.channelIndex,
								section.startTick, section.durationTicks, key.tick, ImGui::GetIO().MousePos.x };
					}
				}
				if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !canvasObjectClicked &&
					ImGui::GetIO().MousePos.y >= y && ImGui::GetIO().MousePos.y <= y + m_RowHeight)
				{
					m_Selection = { SelectionKind::Channel, channel.id, track.id, section.id, row.channelIndex };
					const auto globalTick = SnapTick(XToTick(ImGui::GetIO().MousePos.x, canvasPosition.x));
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && globalTick >= section.startTick &&
						globalTick < section.startTick + section.durationTicks)
						pendingKey = std::make_tuple(track.id, section.id, row.channelIndex, globalTick);
					else SeekPreview(globalTick);
				}
			}
		}
		for (const auto& marker : m_Edit.Asset().markers)
		{
			const float x = rangeX(marker.tick);
			if (x < clipMin.x - 8.0f || x > clipMax.x + 8.0f) continue;
			draw->AddLine(ImVec2(x, clipMin.y), ImVec2(x, clipMax.y),
				marker.id == m_Selection.id ? IM_COL32(245, 217, 115, 255) : IM_COL32(231, 156, 61, 210), 1.5f);
		}
		const float playheadX = rangeX(m_Playhead);
		draw->AddLine(ImVec2(playheadX, clipMin.y), ImVec2(playheadX, clipMax.y), IM_COL32(242, 91, 82, 255), 2.0f);
		draw->PopClipRect();
		if (hovered && ImGui::GetIO().KeyShift && ImGui::GetIO().MouseWheel != 0.0f)
			m_ViewStart -= static_cast<Vans::VansTimelineTick>(ImGui::GetIO().MouseWheel * 96.0 / m_PixelsPerTick);
		m_VerticalScroll = ImGui::GetScrollY();
		ImGui::EndChild();
		if (pendingKey) AddKey(std::get<0>(*pendingKey), std::get<1>(*pendingKey), std::get<2>(*pendingKey), std::get<3>(*pendingKey));
	}
}

bool VansTimelineEditorWindow::DrawSerializedValue(const char* label, Vans::VansSerializedValue& value, bool readOnly)
{
	bool changed = false;
	ImGui::BeginDisabled(readOnly);
	switch (value.kind)
	{
	case Vans::VansSerializedValue::Kind::Null: ImGui::TextDisabled("%s: null", label); break;
	case Vans::VansSerializedValue::Kind::Bool: changed = ImGui::Checkbox(label, &value.boolValue); break;
	case Vans::VansSerializedValue::Kind::Int: changed = ImGui::InputScalar(label, ImGuiDataType_S64, &value.intValue); break;
	case Vans::VansSerializedValue::Kind::Float: changed = ImGui::InputDouble(label, &value.floatValue); break;
	case Vans::VansSerializedValue::Kind::String:
	{
		std::array<char, 512> buffer{}; CopyText(buffer.data(), buffer.size(), value.stringValue);
		if (ImGui::InputText(label, buffer.data(), buffer.size())) { value.stringValue = buffer.data(); changed = true; }
		break;
	}
	case Vans::VansSerializedValue::Kind::Array:
		if (ImGui::TreeNode(label))
		{
			for (std::size_t index = 0; index < value.arrayItems.size(); ++index)
			{
				ImGui::PushID(static_cast<int>(index));
				changed = DrawSerializedValue("value", value.arrayItems[index], readOnly) || changed;
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
		break;
	case Vans::VansSerializedValue::Kind::Object:
		if (ImGui::TreeNode(label))
		{
			for (auto& [name, field] : value.objectFields)
			{
				ImGui::PushID(name.c_str());
				changed = DrawSerializedValue(name.c_str(), field, readOnly) || changed;
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
		break;
	}
	ImGui::EndDisabled();
	return changed;
}

void VansTimelineEditorWindow::DrawInspector()
{
	Vans::VansTimelineAsset asset = m_Edit.Asset();
	bool changed = false;
	if (m_Selection.kind == SelectionKind::Asset)
	{
		ImGui::TextUnformatted("Timeline");
		changed |= ImGui::InputScalar("Duration", ImGuiDataType_S64, &asset.durationTicks);
		changed |= ImGui::InputScalar("Playback start", ImGuiDataType_S64, &asset.playbackRange.startTick);
		changed |= ImGui::InputScalar("Playback end", ImGuiDataType_S64, &asset.playbackRange.endTick);
	}
	else if (m_Selection.kind == SelectionKind::Track)
	{
		Vans::VansTimelineTrack* track = FindTrack(asset);
		if (!track) return;
		const auto* descriptor = Vans::VansTimelineTrackDescriptorRegistry::Find(track->type);
		ImGui::Text("%s", descriptor ? descriptor->displayName.c_str() : track->type.stableName.c_str());
		std::array<char, 256> name{}; CopyText(name.data(), name.size(), track->name);
		if (ImGui::InputText("Name", name.data(), name.size())) { track->name = name.data(); changed = true; }
		changed |= ImGui::Checkbox("Enabled", &track->enabled);
		changed |= ImGui::InputInt("Priority", &track->priority);
		changed |= DrawSerializedValue("Extension data", track->extensionData, descriptor == nullptr);
		if (descriptor && descriptor->supportsSections && ImGui::Button("Add Section at Playhead"))
		{
			Vans::VansTimelineSection section;
			section.startTick = std::clamp(m_Playhead, Vans::VansTimelineTick{ 0 },
				std::max<Vans::VansTimelineTick>(0, asset.durationTicks - 1));
			section.durationTicks = std::min<Vans::VansTimelineTick>(
				std::max<Vans::VansTimelineTick>(1, asset.durationTicks / 10), asset.durationTicks - section.startTick);
			const auto result = m_Edit.AddSection(track->id, std::move(section));
			if (!result) SetError(result.message);
			else { m_Selection = { SelectionKind::Section, result.objectId, track->id }; RefreshPreview(); }
			return;
		}
	}
	else if (m_Selection.kind == SelectionKind::Section)
	{
		Vans::VansTimelineSection* section = FindSection(asset);
		if (!section) return;
		std::array<char, 256> name{}; CopyText(name.data(), name.size(), section->name);
		if (ImGui::InputText("Name", name.data(), name.size())) { section->name = name.data(); changed = true; }
		changed |= ImGui::InputScalar("Start", ImGuiDataType_S64, &section->startTick);
		changed |= ImGui::InputScalar("Duration", ImGuiDataType_S64, &section->durationTicks);
		changed |= ImGui::Checkbox("Active", &section->active);
		if (section->extensionData) changed |= DrawSerializedValue("Extension data", *section->extensionData);
		for (auto& channel : section->channels)
			ImGui::Text("%s  (%zu keys)", channel.name.c_str(), channel.keys.size());
	}
	else if (m_Selection.kind == SelectionKind::Channel)
	{
		Vans::VansTimelineChannel* channel = FindChannel(asset);
		if (!channel) return;
		std::array<char, 256> name{}; CopyText(name.data(), name.size(), channel->name);
		if (ImGui::InputText("Name", name.data(), name.size())) { channel->name = name.data(); changed = true; }
		ImGui::Text("Type: %s", Vans::VansTimelineSerialization::ValueTypeName(channel->type));
		if (ImGui::Button("Add Key at Playhead")) AddKeyAtPlayhead();
		for (const auto& key : channel->keys) ImGui::Text("%s  %lld", key.id.c_str(), static_cast<long long>(key.tick));
	}
	else if (m_Selection.kind == SelectionKind::Key)
	{
		Vans::VansTimelineKey* key = FindKey(asset);
		Vans::VansTimelineChannel* channel = FindChannel(asset);
		if (!key || !channel) return;
		const auto beforeTick = key->tick;
		changed |= ImGui::InputScalar("Tick", ImGuiDataType_S64, &key->tick);
		if (beforeTick != key->tick) key->tick = std::clamp(key->tick, Vans::VansTimelineTick{ 0 },
			std::max<Vans::VansTimelineTick>(0, FindSection(asset)->durationTicks - 1));
		const char* interpolation = InterpolationName(key->interpolation);
		if (ImGui::BeginCombo("Interpolation", interpolation))
		{
			for (const auto candidate : { Vans::VansTimelineInterpolation::Constant, Vans::VansTimelineInterpolation::Linear,
				Vans::VansTimelineInterpolation::Auto, Vans::VansTimelineInterpolation::ClampedAuto,
				Vans::VansTimelineInterpolation::Cubic, Vans::VansTimelineInterpolation::Bezier, Vans::VansTimelineInterpolation::Slerp })
			{
				const bool selected = key->interpolation == candidate;
				if (ImGui::Selectable(InterpolationName(candidate), selected)) { key->interpolation = candidate; changed = true; }
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		Vans::VansSerializedValue encoded = Vans::VansTimelineEncodeSourceValue(key->value);
		if (DrawSerializedValue("Value", encoded))
		{
			Vans::VansTimelineValue decoded;
			if (Vans::VansTimelineDecodeSourceValue(encoded, channel->type, decoded)) { key->value = std::move(decoded); changed = true; }
		}
	}
	else if (m_Selection.kind == SelectionKind::Marker)
	{
		const auto found = std::find_if(asset.markers.begin(), asset.markers.end(),
			[&](const auto& marker) { return marker.id == m_Selection.id; });
		if (found == asset.markers.end()) return;
		changed |= ImGui::InputScalar("Tick", ImGuiDataType_S64, &found->tick);
		changed |= ImGui::Checkbox("Runtime observable", &found->runtimeObservable);
		changed |= ImGui::Checkbox("Editor safe", &found->editorSafe);
		changed |= DrawSerializedValue("Payload", found->payload);
	}
	if (changed) ApplyAsset(std::move(asset));
}

void VansTimelineEditorWindow::DeleteSelection()
{
	if (m_Selection.kind == SelectionKind::Asset || m_Selection.id.empty()) return;
	const auto result = m_Edit.RemoveObject(m_Selection.id);
	if (!result) SetError(result.message);
	else { m_Selection = {}; RefreshPreview(); }
}

void VansTimelineEditorWindow::CopySelection()
{
	Vans::VansTimelineAsset asset = m_Edit.Asset();
	if (m_Selection.kind == SelectionKind::Track)
		if (auto* track = FindTrack(asset)) { m_TrackClipboard = *track; m_SectionClipboard.reset(); }
	if (m_Selection.kind == SelectionKind::Section)
		if (auto* section = FindSection(asset)) { m_SectionClipboard = *section; m_TrackClipboard.reset(); }
}

void VansTimelineEditorWindow::PasteSelection()
{
	Vans::VansTimelineAsset asset = m_Edit.Asset();
	if (m_TrackClipboard)
	{
		Vans::VansTimelineTrack track = *m_TrackClipboard;
		track.id = Vans::VansTimelineEditService::NewStableId();
		track.order = static_cast<std::int32_t>(asset.tracks.size());
		for (auto& section : track.sections) RegenerateSectionIds(section);
		const auto id = track.id;
		asset.tracks.push_back(std::move(track));
		m_Selection = { SelectionKind::Track, id };
		ApplyAsset(std::move(asset));
	}
	else if (m_SectionClipboard)
	{
		Vans::VansTimelineTrack* track = FindTrack(asset);
		if (!track) return;
		Vans::VansTimelineSection section = *m_SectionClipboard;
		RegenerateSectionIds(section);
		section.startTick = m_Playhead;
		const auto id = section.id;
		track->sections.push_back(std::move(section));
		m_Selection = { SelectionKind::Section, id, track->id };
		ApplyAsset(std::move(asset));
	}
}

void VansTimelineEditorWindow::DuplicateSelection()
{
	if (m_Selection.kind == SelectionKind::Key)
	{
		const auto result = m_Edit.DuplicateKeys({ m_Selection.id },
			Vans::VansTimelineTime::FrameToTick(1, m_Edit.Asset().timebase));
		if (!result) SetError(result.message); else RefreshPreview();
	}
	else { CopySelection(); PasteSelection(); }
}

void VansTimelineEditorWindow::HandleShortcuts()
{
	if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
	if (m_CommandMap.IsTriggered(Vans::VansTimelineCommand::Save))
	{
		const auto result = m_Edit.Save(*m_ActiveAPI); if (!result) SetError(result.message);
	}
	if (m_CommandMap.IsTriggered(Vans::VansTimelineCommand::Undo))
	{
		const auto result = m_Edit.Undo(); if (!result) SetError(result.message); else RefreshPreview();
	}
	if (m_CommandMap.IsTriggered(Vans::VansTimelineCommand::Redo))
	{
		const auto result = m_Edit.Redo(); if (!result) SetError(result.message); else RefreshPreview();
	}
	if (m_CommandMap.IsTriggered(Vans::VansTimelineCommand::Copy)) CopySelection();
	if (m_CommandMap.IsTriggered(Vans::VansTimelineCommand::Paste)) PasteSelection();
	if (m_CommandMap.IsTriggered(Vans::VansTimelineCommand::Duplicate)) DuplicateSelection();
	if (m_CommandMap.IsTriggered(Vans::VansTimelineCommand::DeleteSelection)) DeleteSelection();
}

void VansTimelineEditorWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!m_IsOpen) return;
	m_ActiveAPI = &editorAPI;
	m_Preview.Poll();
	if (m_Preview.State() == Vans::VansTimelinePreviewState::Playing)
		m_Playhead = m_Preview.CurrentTick();
	bool open = true;
	if (!ImGui::Begin("Timeline Editor", &open, ImGuiWindowFlags_MenuBar))
	{
		ImGui::End(); if (!open) Close(); return;
	}
	DrawToolbar();
	HandleShortcuts();
	ImGui::Separator();
	const float width = ImGui::GetContentRegionAvail().x;
	const float outlinerWidth = std::max(220.0f, width * 0.23f);
	const float inspectorWidth = std::max(260.0f, width * 0.25f);
	const float canvasWidth = std::max(240.0f, width - outlinerWidth - inspectorWidth - 16.0f);
	if (ImGui::BeginChild("Timeline.Outliner", ImVec2(outlinerWidth, -28.0f), true)) DrawOutliner();
	ImGui::EndChild(); ImGui::SameLine();
	if (ImGui::BeginChild("Timeline.Canvas", ImVec2(canvasWidth, -28.0f), true)) DrawTimeline();
	ImGui::EndChild(); ImGui::SameLine();
	if (ImGui::BeginChild("Timeline.Inspector", ImVec2(0.0f, -28.0f), true)) DrawInspector();
	ImGui::EndChild();
	if (!m_LastError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", m_LastError.c_str());
	else ImGui::Text("%s%s | %s | tick %lld", m_Edit.IsDirty() ? "Modified | " : "",
		m_Path.c_str(), FormatTick(m_Playhead).c_str(), static_cast<long long>(m_Playhead));
	ImGui::End();
	if (!open) Close();
}
}
