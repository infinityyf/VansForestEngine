#include "VansTimelineEditorWindow.h"

#include "../Timeline/VansTimelineTrackDescriptorRegistry.h"
#include "../VansAssetDocumentRegistry.h"
#include "../VansEditorObjectReference.h"
#include "../VansEditorPropertyDescriptorRegistry.h"
#include "../VansEditorSelection.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../TimelineCore/VansTimelineSerialization.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace VansGraphics
{
namespace
{
std::string Lower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
	{
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

bool MatchesSearch(const std::string& value, const char* search)
{
	return !search || !*search || Lower(value).find(Lower(search)) != std::string::npos;
}

float TrackRowHeight(const Vans::VansTimelineTrack& track, float baseHeight)
{
	const std::string mode = Lower(track.display.rowHeight);
	if (mode == "compact") return std::max(20.0f, baseHeight * 0.8f);
	if (mode == "tall") return std::min(72.0f, baseHeight * 1.4f);
	return baseHeight;
}

double BlendAlpha(const Vans::VansTimelineBlendCurve& curve, double alpha)
{
	alpha = std::clamp(alpha, 0.0, 1.0);
	const std::string shape = Lower(curve.shape);
	if (shape == "easein") return std::pow(alpha, std::max(0.0001, curve.exponent));
	if (shape == "easeout") return 1.0 - std::pow(1.0 - alpha, std::max(0.0001, curve.exponent));
	if (shape == "easeinout" || shape == "smoothstep") return alpha * alpha * (3.0 - 2.0 * alpha);
	return alpha;
}

Vans::VansSerializedValue* Field(Vans::VansSerializedValue& object, const std::string& name)
{
	if (object.kind != Vans::VansSerializedValue::Kind::Object) return nullptr;
	for (auto& [fieldName, value] : object.objectFields)
		if (fieldName == name) return &value;
	return nullptr;
}

const char* PreviewStateName(Vans::VansTimelinePreviewState state)
{
	switch (state)
	{
	case Vans::VansTimelinePreviewState::Detached: return "Detached";
	case Vans::VansTimelinePreviewState::Ready: return "Ready";
	case Vans::VansTimelinePreviewState::Playing: return "Playing";
	case Vans::VansTimelinePreviewState::Paused: return "Paused";
	case Vans::VansTimelinePreviewState::Scrubbing: return "Scrubbing";
	case Vans::VansTimelinePreviewState::Restoring: return "Restoring";
	case Vans::VansTimelinePreviewState::Faulted: return "Faulted";
	}
	return "Detached";
}

ImU32 TrackColor(const Vans::VansTimelineTrack& track, bool selected)
{
	const auto& color = track.display.color;
	const float boost = selected ? 1.18f : 1.0f;
	return ImGui::ColorConvertFloat4ToU32(ImVec4(
		std::min(1.0f, color[0] * boost), std::min(1.0f, color[1] * boost),
		std::min(1.0f, color[2] * boost), selected ? 0.92f : 0.72f));
}

Vans::VansTimelineKeyValue DefaultKeyValue(Vans::VansTimelineChannelType type)
{
	switch (type)
	{
	case Vans::VansTimelineChannelType::Bool: return false;
	case Vans::VansTimelineChannelType::Int32: return std::int32_t{};
	case Vans::VansTimelineChannelType::Int64: return std::int64_t{};
	case Vans::VansTimelineChannelType::Float: return 0.0f;
	case Vans::VansTimelineChannelType::Double: return 0.0;
	case Vans::VansTimelineChannelType::Enum:
	case Vans::VansTimelineChannelType::String: return std::string{};
	case Vans::VansTimelineChannelType::Vec2: return Vans::VansTimelineVec2{};
	case Vans::VansTimelineChannelType::Vec3: return Vans::VansTimelineVec3{};
	case Vans::VansTimelineChannelType::Vec4: return Vans::VansTimelineVec4{};
	case Vans::VansTimelineChannelType::Quaternion: return Vans::VansTimelineQuaternion{};
	case Vans::VansTimelineChannelType::ColorLinear: return Vans::VansTimelineColorLinear{};
	case Vans::VansTimelineChannelType::ColorSrgb: return Vans::VansTimelineColorSrgb{};
	case Vans::VansTimelineChannelType::ObjectReference: return Vans::VansTimelineObjectReference{};
	case Vans::VansTimelineChannelType::EventPayload: return Vans::VansTimelineEventPayload{};
	}
	return {};
}

bool NumericKeyValue(const Vans::VansTimelineKeyValue& value, double& number)
{
	if (const auto* current = std::get_if<float>(&value)) { number = *current; return true; }
	if (const auto* current = std::get_if<double>(&value)) { number = *current; return true; }
	if (const auto* current = std::get_if<std::int32_t>(&value)) { number = *current; return true; }
	if (const auto* current = std::get_if<std::int64_t>(&value)) { number = static_cast<double>(*current); return true; }
	return false;
}

bool SetNumericKeyValue(Vans::VansTimelineKeyValue& value, double number)
{
	if (auto* current = std::get_if<float>(&value)) { *current = static_cast<float>(number); return true; }
	if (auto* current = std::get_if<double>(&value)) { *current = number; return true; }
	if (auto* current = std::get_if<std::int32_t>(&value))
	{
		*current = static_cast<std::int32_t>(std::llround(number)); return true;
	}
	if (auto* current = std::get_if<std::int64_t>(&value))
	{
		*current = static_cast<std::int64_t>(std::llround(number)); return true;
	}
	return false;
}

bool DrawTimelineKeyValue(const char* label, Vans::VansTimelineKeyValue& value)
{
	ImGui::PushID(label);
	const bool changed = std::visit([&](auto& item) -> bool
	{
		using T = std::decay_t<decltype(item)>;
		if constexpr (std::is_same_v<T, std::monostate>)
		{
			ImGui::TextDisabled("%s: No value", label);
			return false;
		}
		else if constexpr (std::is_same_v<T, bool>) return ImGui::Checkbox(label, &item);
		else if constexpr (std::is_same_v<T, std::int32_t>)
			return ImGui::DragScalar(label, ImGuiDataType_S32, &item, 1.0f);
		else if constexpr (std::is_same_v<T, std::int64_t>)
			return ImGui::DragScalar(label, ImGuiDataType_S64, &item, 1.0f);
		else if constexpr (std::is_same_v<T, float>) return ImGui::DragFloat(label, &item, 0.01f);
		else if constexpr (std::is_same_v<T, double>)
			return ImGui::DragScalar(label, ImGuiDataType_Double, &item, 0.01f);
		else if constexpr (std::is_same_v<T, std::string>)
		{
			char buffer[512]{}; std::strncpy(buffer, item.c_str(), sizeof(buffer) - 1);
			if (!ImGui::InputText(label, buffer, sizeof(buffer))) return false;
			item = buffer;
			return true;
		}
		else if constexpr (std::is_same_v<T, Vans::VansTimelineVec2> ||
			std::is_same_v<T, Vans::VansTimelineVec3> || std::is_same_v<T, Vans::VansTimelineVec4> ||
			std::is_same_v<T, Vans::VansTimelineQuaternion>)
		{
			return ImGui::DragScalarN(label, ImGuiDataType_Double, item.value.data(),
				static_cast<int>(item.value.size()), 0.01f);
		}
		else if constexpr (std::is_same_v<T, Vans::VansTimelineColorLinear> ||
			std::is_same_v<T, Vans::VansTimelineColorSrgb>)
		{
			float color[4]{ static_cast<float>(item.value[0]), static_cast<float>(item.value[1]),
				static_cast<float>(item.value[2]), static_cast<float>(item.value[3]) };
			if (!ImGui::ColorEdit4(label, color, ImGuiColorEditFlags_Float)) return false;
			for (std::size_t index = 0; index < 4; ++index) item.value[index] = color[index];
			return true;
		}
		else if constexpr (std::is_same_v<T, Vans::VansTimelineObjectReference>)
		{
			bool edited = false;
			char guid[128]{}; std::strncpy(guid, item.guid.c_str(), sizeof(guid) - 1);
			if (ImGui::InputText("GUID", guid, sizeof(guid))) { item.guid = guid; edited = true; }
			char kind[64]{}; std::strncpy(kind, item.objectKind.c_str(), sizeof(kind) - 1);
			if (ImGui::InputText("Object Kind", kind, sizeof(kind))) { item.objectKind = kind; edited = true; }
			return edited;
		}
		else if constexpr (std::is_same_v<T, Vans::VansTimelineEventPayload>)
		{
			char type[128]{}; std::strncpy(type, item.payloadType.c_str(), sizeof(type) - 1);
			if (!ImGui::InputText("Payload Type", type, sizeof(type))) return false;
			item.payloadType = type;
			return true;
		}
		return false;
	}, value);
	ImGui::PopID();
	return changed;
}

const std::vector<std::string>* InspectorEnumOptions(
	const std::string& label,
	Vans::VansTimelineTrackType trackType)
{
	static const std::vector<std::pair<std::string, std::vector<std::string>>> common{
		{ "kind", { "SceneEntity", "SceneComponent", "RuntimeObject", "Asset", "Spawnable", "UIComponent", "External" } },
		{ "defaultCompletionMode", { "ProjectDefault", "RestoreState", "KeepState" } },
		{ "completionMode", { "ProjectDefault", "RestoreState", "KeepState" } },
		{ "defaultEvaluationMode", { "WithSubTimelines", "Isolated" } },
		{ "blendMode", { "Override", "Additive", "Multiply", "Relative" } },
		{ "loopMode", { "None", "Loop", "PingPong" } },
		{ "preExtrapolation", { "None", "Hold", "Linear", "Loop", "PingPong" } },
		{ "postExtrapolation", { "None", "Hold", "Linear", "Loop", "PingPong" } },
		{ "interpolation", { "Constant", "Linear", "Auto", "ClampedAuto", "Cubic", "Bezier", "Slerp" } },
		{ "tangentMode", { "Unified", "Broken", "Weighted" } },
		{ "rotationMode", { "EulerShortestPath", "QuaternionSlerp" } },
		{ "trajectoryDisplay", { "Never", "Selected", "Always" } },
		{ "physicsPolicy", { "RejectDynamicBody" } },
		{ "stateBefore", { "Restore", "Active", "Inactive", "DoNothing" } },
		{ "stateAfter", { "Restore", "Active", "Inactive", "DoNothing" } },
		{ "constraintType", { "Parent", "Position", "Point", "Rotation", "Orient", "Scale", "Aim", "LookAt" } },
		{ "upAxis", { "X", "Y", "Z" } },
		{ "aimAxis", { "X", "Y", "Z" } },
		{ "rootMotionPolicy", { "Ignore", "ApplyToOwner", "ExtractOnly" } },
		{ "missingParameterPolicy", { "Error", "WarningAndSkip" } },
		{ "syncMode", { "TimelineClock", "AudioClock", "FreeRun" } },
		{ "colorSpace", { "Linear", "Srgb" } },
		{ "cutMode", { "Cut", "Blend" } },
		{ "aspectPolicy", { "Preserve", "MatchViewport", "Letterbox" } },
		{ "instancePolicy", { "PerEntityRuntimeInstance", "ExistingInstance" } },
		{ "eventLane", { "MainThread", "Script", "GameLogic", "Diagnostics" } },
		{ "firePolicy", { "Forward", "Reverse", "Both" } },
		{ "loopPolicy", { "EveryLoop", "FirstLoopOnly" } },
		{ "spawnPolicy", { "OnEnter", "PreSpawn" } },
		{ "destroyPolicy", { "OnExit", "KeepAlive" } },
		{ "asyncPolicy", { "NonBlocking", "BlockUntilReady" } },
		{ "rowHeight", { "Compact", "Normal", "Tall" } }
	};
	for (const auto& entry : common) if (entry.first == label) return &entry.second;

	static const std::vector<std::string> channelTypes{
		"Bool", "Int32", "Int64", "Float", "Double", "Enum", "String", "Vec2", "Vec3", "Vec4",
		"Quaternion", "ColorLinear", "ColorSrgb", "ObjectReference", "EventPayload"
	};
	static const std::vector<std::string> animatorParameterTypes{ "Float", "Int", "Bool", "Trigger", "Vector3", "Quaternion" };
	static const std::vector<std::string> transformSpaces{ "Local", "World", "OwnerRelative" };
	static const std::vector<std::string> localSpace{ "Local" };
	static const std::vector<std::string> activationScopes{ "EntityActive", "ComponentEnabled", "RenderVisibility" };
	static const std::vector<std::string> timeScopes{ "LocalTimeWarp" };
	static const std::vector<std::string> triggerSeek{ "Never", "Crossed", "ExactTick" };
	static const std::vector<std::string> audioSeek{ "Exact", "NearestSupported", "RestartAndFastForward", "Disabled" };
	static const std::vector<std::string> particleSeek{ "DeterministicResimulate", "RestartOnly" };
	static const std::vector<std::string> audioEnd{ "Stop", "PlayToCompletion", "FadeOut" };
	static const std::vector<std::string> mediaEnd{ "Stop", "PauseLastFrame", "Clear" };
	static const std::vector<std::string> mediaTargets{ "VideoComponent", "MaterialSlot", "UIElement" };
	static const std::vector<std::string> uiTargets{ "Screen", "Element", "ViewModel", "Action" };
	static const std::vector<std::string> particleActions{ "Play", "Stop", "Restart", "Pause", "Burst" };
	static const std::vector<std::string> sceneActions{ "Load", "Unload", "Activate" };
	static const std::vector<std::string> fadeModes{ "Fade", "PostProcess" };
	if (label == "valueType") return &channelTypes;
	if (label == "parameterType") return trackType == Vans::VansTimelineTrackType::AnimatorParameter
		? &animatorParameterTypes : &channelTypes;
	if (label == "space") return trackType == Vans::VansTimelineTrackType::BoneOverride ? &localSpace : &transformSpaces;
	if (label == "scope") return trackType == Vans::VansTimelineTrackType::TimeScale ? &timeScopes : &activationScopes;
	if (label == "seekPolicy")
	{
		if (trackType == Vans::VansTimelineTrackType::Audio) return &audioSeek;
		if (trackType == Vans::VansTimelineTrackType::Particle) return &particleSeek;
		return &triggerSeek;
	}
	if (label == "onSectionEnd") return trackType == Vans::VansTimelineTrackType::Audio ? &audioEnd : &mediaEnd;
	if (label == "targetKind") return trackType == Vans::VansTimelineTrackType::UIState ? &uiTargets : &mediaTargets;
	if (label == "action")
	{
		if (trackType == Vans::VansTimelineTrackType::Particle) return &particleActions;
		if (trackType == Vans::VansTimelineTrackType::SceneState) return &sceneActions;
	}
	if (label == "mode") return &fadeModes;
	return nullptr;
}

Vans::EditorAPI::AssetType InspectorAssetType(
	const std::string& label,
	const Vans::VansTimelineTrackDescriptor* descriptor)
{
	if (label == "assetGuid") return descriptor ? descriptor->sectionAssetType : Vans::EditorAPI::AssetType::Unknown;
	if (label == "avatarMaskGuid") return Vans::EditorAPI::AssetType::BoneMask;
	if (label == "profileGuid") return Vans::EditorAPI::AssetType::PostProcessProfile;
	if (label == "sceneGuid") return Vans::EditorAPI::AssetType::Scene;
	if (label == "spawnTemplateGuid") return Vans::EditorAPI::AssetType::Unknown;
	return Vans::EditorAPI::AssetType::Unknown;
}

const char* InspectorPathField(const std::string& label)
{
	if (label == "assetGuid") return "assetPath";
	if (label == "avatarMaskGuid") return "avatarMaskPath";
	if (label == "profileGuid") return "profilePath";
	if (label == "sceneGuid") return "scenePath";
	if (label == "spawnTemplateGuid") return "spawnTemplatePath";
	return nullptr;
}

}

void VansTimelineEditorWindow::Open(const std::string& timelinePath, std::string ownerEntityGuid)
{
	if (m_IsOpen && m_Edit.IsDirty())
	{
		SetError("Save or discard the current Timeline before opening another asset");
		return;
	}
	m_Preview.RestoreAndDetach();
	m_Path = timelinePath;
	m_InstanceOwnerGuid = std::move(ownerEntityGuid);
	const Vans::TimelineEditResult result = m_Edit.Open(timelinePath);
	if (!result)
	{
		m_Path.clear();
		m_InstanceOwnerGuid.clear();
		SetError(result.message);
		return;
	}
	m_IsOpen = true;
	m_CloseRequested = false;
	m_Selection = {};
	m_SelectedIds.clear();
	m_Playhead = m_Edit.Asset().playbackRange.startTick;
	m_ViewStart = m_Edit.Asset().workRange.startTick;
	m_SelectionRangeStart.reset();
	m_SelectionRangeEnd.reset();
	m_OpenRenamePopup = false;
	m_LastError.clear();
	m_RecoveryAsset.reset();
	m_ShowRecoveryPrompt = false;
	LoadUserState();
	Vans::VansTimelineAsset recovery;
	bool recoveryAvailable = false;
	std::string error;
	if (!Vans::VansTimelineEditorStateStore::LoadRecoveryIfNewer(m_Path, recovery, recoveryAvailable, error))
		SetError(error);
	else if (recoveryAvailable)
	{
		m_RecoveryAsset = std::move(recovery);
		m_ShowRecoveryPrompt = true;
	}
	m_LastRecoveryWrite = std::chrono::steady_clock::now();
	m_LastRecoveryStateId = m_Edit.Document() ? m_Edit.Document()->sourceDocument.CurrentStateId() : 0;
}

void VansTimelineEditorWindow::Close()
{
	SaveUserState();
	if (m_IsOpen && m_Edit.IsDirty()) { m_CloseRequested = true; return; }
	if (!m_Path.empty()) Vans::VansTimelineEditorStateStore::RemoveRecovery(m_Path);
	m_Preview.RestoreAndDetach();
	m_IsOpen = false;
	m_CloseRequested = false;
	m_Path.clear();
	m_InstanceOwnerGuid.clear();
	m_ActiveAPI = nullptr;
	m_Selection = {};
	m_SelectedIds.clear();
	m_SelectionRangeStart.reset();
	m_SelectionRangeEnd.reset();
}

void VansTimelineEditorWindow::SetError(std::string message)
{
	m_LastError = std::move(message);
}

void VansTimelineEditorWindow::LoadUserState()
{
	Vans::VansTimelineEditorUserState state;
	std::string error;
	if (!Vans::VansTimelineEditorStateStore::LoadUserState(m_Path, state, error))
	{
		SetError(std::move(error));
		return;
	}
	m_Playhead = std::clamp(state.playhead, m_Edit.Asset().playbackRange.startTick,
		m_Edit.Asset().playbackRange.endTick);
	m_ViewStart = state.viewStart;
	m_PixelsPerTick = std::clamp(state.pixelsPerTick, 0.0002, 8.0);
	m_TrackScroll = std::max(0.0f, state.trackScroll);
	m_RowHeight = std::clamp(state.rowHeight, 24.0f, 48.0f);
	m_CurveHeight = std::clamp(state.curveHeight, 120.0f, 500.0f);
	m_CurveValueZoom = std::clamp(state.curveValueZoom, 0.1, 100.0);
	m_CurveValuePan = std::isfinite(state.curveValuePan) ? state.curveValuePan : 0.0;
	m_ShowCurves = state.showCurves;
	m_ShowWaveforms = state.showWaveforms;
	m_ShowThumbnails = state.showThumbnails;
	m_SnapEnabled = state.snapEnabled;
	m_SnapFrames = state.snapFrames;
	m_SnapKeys = state.snapKeys;
	m_SnapMarkers = state.snapMarkers;
	m_SnapSections = state.snapSections;
	m_SnapRanges = state.snapRanges;
	m_SnapPlayhead = state.snapPlayhead;
	m_PreviewSafeEvents = state.previewSafeEvents;
	m_IncludeSubTimelines = state.includeSubTimelines;
	m_PlaybackDirection = state.reversePlayback ? -1 : 1;
	m_LoopPlaybackRange = state.loopPlaybackRange;
	m_AutoKeyMode = static_cast<AutoKeyMode>(std::clamp(state.autoKeyMode, 0, 2));
	m_TimeDisplayMode = static_cast<TimeDisplayMode>(std::clamp(state.timeDisplayMode, 0, 2));
	m_SectionEditMode = static_cast<SectionEditMode>(std::clamp(state.sectionEditMode, 0, 4));
	m_SessionMutedTracks = std::move(state.mutedTracks);
	m_SessionSoloTracks = std::move(state.soloTracks);
	m_HiddenTracks = std::move(state.hiddenTracks);
	m_PinnedTracks = std::move(state.pinnedTracks);
	std::memset(m_Search, 0, sizeof(m_Search));
	std::strncpy(m_Search, state.search.c_str(), sizeof(m_Search) - 1);
}

void VansTimelineEditorWindow::SaveUserState()
{
	if (m_Path.empty()) return;
	Vans::VansTimelineEditorUserState state;
	state.playhead = m_Playhead;
	state.viewStart = m_ViewStart;
	state.pixelsPerTick = m_PixelsPerTick;
	state.trackScroll = m_TrackScroll;
	state.rowHeight = m_RowHeight;
	state.curveHeight = m_CurveHeight;
	state.curveValueZoom = m_CurveValueZoom;
	state.curveValuePan = m_CurveValuePan;
	state.showCurves = m_ShowCurves;
	state.showWaveforms = m_ShowWaveforms;
	state.showThumbnails = m_ShowThumbnails;
	state.snapEnabled = m_SnapEnabled;
	state.snapFrames = m_SnapFrames;
	state.snapKeys = m_SnapKeys;
	state.snapMarkers = m_SnapMarkers;
	state.snapSections = m_SnapSections;
	state.snapRanges = m_SnapRanges;
	state.snapPlayhead = m_SnapPlayhead;
	state.previewSafeEvents = m_PreviewSafeEvents;
	state.includeSubTimelines = m_IncludeSubTimelines;
	state.reversePlayback = m_PlaybackDirection < 0;
	state.loopPlaybackRange = m_LoopPlaybackRange;
	state.autoKeyMode = static_cast<int>(m_AutoKeyMode);
	state.timeDisplayMode = static_cast<int>(m_TimeDisplayMode);
	state.sectionEditMode = static_cast<int>(m_SectionEditMode);
	state.search = m_Search;
	state.mutedTracks = m_SessionMutedTracks;
	state.soloTracks = m_SessionSoloTracks;
	state.hiddenTracks = m_HiddenTracks;
	state.pinnedTracks = m_PinnedTracks;
	std::string error;
	if (!Vans::VansTimelineEditorStateStore::SaveUserState(m_Path, state, error))
		SetError(std::move(error));
}

void VansTimelineEditorWindow::UpdateRecovery()
{
	if (m_Path.empty()) return;
	if (!m_Edit.IsDirty())
	{
		Vans::VansTimelineEditorStateStore::RemoveRecovery(m_Path);
		m_LastRecoveryStateId = 0;
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	if (now - m_LastRecoveryWrite < std::chrono::seconds(10)) return;
	const std::uint64_t stateId = m_Edit.Document()
		? m_Edit.Document()->sourceDocument.CurrentStateId() : 0;
	if (stateId == m_LastRecoveryStateId && !m_Edit.IsInteracting()) return;
	std::string error;
	if (!Vans::VansTimelineEditorStateStore::SaveRecovery(m_Path, m_Edit.PreviewAsset(), error))
		SetError(std::move(error));
	else
	{
		m_LastRecoveryStateId = stateId;
		m_LastRecoveryWrite = now;
	}
}

void VansTimelineEditorWindow::Select(Selection selection, bool additive)
{
	if (!additive) m_SelectedIds.clear();
	m_Selection = std::move(selection);
	if (!m_Selection.id.empty())
	{
		if (additive && m_SelectedIds.find(m_Selection.id) != m_SelectedIds.end())
			m_SelectedIds.erase(m_Selection.id);
		else
			m_SelectedIds.insert(m_Selection.id);
	}
}

void VansTimelineEditorWindow::SelectKeyRange(Selection selection)
{
	if (selection.kind != SelectionKind::Key || m_Selection.kind != SelectionKind::Key ||
		selection.trackId != m_Selection.trackId || selection.sectionId != m_Selection.sectionId ||
		selection.channelIndex != m_Selection.channelIndex)
	{
		Select(std::move(selection));
		return;
	}
	const auto& asset = m_Edit.PreviewAsset();
	const auto track = std::find_if(asset.tracks.begin(), asset.tracks.end(),
		[&](const auto& value) { return value.id == selection.trackId; });
	if (track == asset.tracks.end()) { Select(std::move(selection)); return; }
	const auto section = std::find_if(track->sections.begin(), track->sections.end(),
		[&](const auto& value) { return value.id == selection.sectionId; });
	if (section == track->sections.end() || selection.channelIndex >= section->channels.size())
	{
		Select(std::move(selection));
		return;
	}
	const auto& keys = section->channels[selection.channelIndex].keys;
	const auto first = std::find_if(keys.begin(), keys.end(),
		[&](const auto& key) { return key.id == m_Selection.id; });
	const auto last = std::find_if(keys.begin(), keys.end(),
		[&](const auto& key) { return key.id == selection.id; });
	if (first == keys.end() || last == keys.end()) { Select(std::move(selection)); return; }
	const auto minimum = std::min(first->tick, last->tick);
	const auto maximum = std::max(first->tick, last->tick);
	m_SelectedIds.clear();
	for (const auto& key : keys)
		if (key.tick >= minimum && key.tick <= maximum) m_SelectedIds.insert(key.id);
	m_Selection = std::move(selection);
}

bool VansTimelineEditorWindow::IsSelected(const Vans::VansTimelineId& id) const
{
	return !id.empty() && m_SelectedIds.find(id) != m_SelectedIds.end();
}

Vans::VansTimelineAsset VansTimelineEditorWindow::BuildPreviewAsset() const
{
	Vans::VansTimelineAsset preview = m_Edit.Asset();
	const bool hasSolo = !m_SessionSoloTracks.empty();
	for (auto& track : preview.tracks)
		track.runtimeMuted = track.runtimeMuted ||
			m_SessionMutedTracks.find(track.id) != m_SessionMutedTracks.end() ||
			(hasSolo && m_SessionSoloTracks.find(track.id) == m_SessionSoloTracks.end());
	return preview;
}

void VansTimelineEditorWindow::DeleteSelection()
{
	std::vector<std::pair<int, Vans::VansTimelineId>> ordered;
	auto add = [&](int rank, const Vans::VansTimelineId& id)
	{
		if (IsSelected(id) || (m_SelectedIds.empty() && m_Selection.id == id)) ordered.emplace_back(rank, id);
	};
	for (const auto& track : m_Edit.Asset().tracks)
	{
		for (const auto& section : track.sections)
		{
			for (const auto& channel : section.channels) for (const auto& key : channel.keys) add(0, key.id);
			add(1, section.id);
		}
		add(2, track.id);
	}
	for (const auto& group : m_Edit.Asset().groups) add(3, group.id);
	for (const auto& binding : m_Edit.Asset().bindings) add(4, binding.id);
	for (const auto& marker : m_Edit.Asset().markers) add(0, marker.id);
	if (ordered.empty()) return;
	std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
	{
		return left.first < right.first;
	});
	if (const auto begin = m_Edit.BeginInteraction(); !begin) { SetError(begin.message); return; }
	for (const auto& [rank, id] : ordered)
	{
		(void)rank;
		const auto result = m_Edit.RemoveObject(id);
		if (!result)
		{
			m_Edit.CancelInteraction();
			SetError(result.message);
			return;
		}
	}
	const auto committed = m_Edit.CommitInteraction();
	if (!committed) { m_Edit.CancelInteraction(); SetError(committed.message); return; }
	m_Selection = {};
	m_SelectedIds.clear();
	RefreshPreview();
}

void VansTimelineEditorWindow::SplitSelection()
{
	Vans::VansTimelineSection* section = SelectedSection();
	if (!section) return;
	const auto result = m_Edit.SplitSection(m_Selection.trackId, section->id, m_Playhead);
	if (!result) SetError(result.message);
	else { Select({ SelectionKind::Section, result.objectId, m_Selection.trackId, result.objectId }); RefreshPreview(); }
}

void VansTimelineEditorWindow::CopySelection()
{
	const Vans::VansTimelineTrack* track = SelectedTrack();
	const Vans::VansTimelineSection* section = SelectedSection();
	if (!track || !section) return;
	m_SectionClipboard = *section;
	m_ClipboardTrackType = track->type;
}

void VansTimelineEditorWindow::PasteSelection()
{
	Vans::VansTimelineTrack* track = SelectedTrack();
	if (!track || !m_SectionClipboard) return;
	if (track->type != m_ClipboardTrackType)
	{
		SetError("Pasted Timeline sections require a track with the same type");
		return;
	}
	const auto result = m_Edit.PasteSection(track->id, *m_SectionClipboard, m_Playhead);
	if (!result) SetError(result.message);
	else { Select({ SelectionKind::Section, result.objectId, track->id, result.objectId }); RefreshPreview(); }
}

void VansTimelineEditorWindow::DuplicateSelection()
{
	if (m_Selection.kind == SelectionKind::Key)
	{
		const auto offset = std::max<Vans::VansTimelineTick>(1,
			Vans::VansTimelineTime::FrameToTick(1, m_Edit.Asset().timebase));
		const auto result = m_Edit.DuplicateKeys(m_SelectedIds, offset);
		if (!result) SetError(result.message);
		else
		{
			Select({ SelectionKind::Key, result.objectId, m_Selection.trackId,
				m_Selection.sectionId, m_Selection.channelIndex });
			RefreshPreview();
		}
		return;
	}
	Vans::VansTimelineSection* section = SelectedSection();
	if (!section) return;
	const auto offset = std::max<Vans::VansTimelineTick>(1,
		Vans::VansTimelineTime::FrameToTick(1, m_Edit.Asset().timebase));
	const auto result = m_Edit.DuplicateSection(m_Selection.trackId, section->id, offset);
	if (!result) SetError(result.message);
	else { Select({ SelectionKind::Section, result.objectId, m_Selection.trackId, result.objectId }); RefreshPreview(); }
}

void VansTimelineEditorWindow::AddMarkerAtPlayhead()
{
	Vans::VansTimelineMarker marker;
	marker.tick = std::clamp(m_Playhead, Vans::VansTimelineTick{ 0 }, m_Edit.Asset().durationTicks);
	const auto result = m_Edit.AddMarker(std::move(marker));
	if (!result) SetError(result.message);
	else
	{
		Select({ SelectionKind::Marker, result.objectId });
		RefreshPreview();
	}
}

void VansTimelineEditorWindow::FrameSelection()
{
	std::optional<Vans::VansTimelineTick> minimum;
	std::optional<Vans::VansTimelineTick> maximum;
	const auto selected = [&](const Vans::VansTimelineId& id)
	{
		return IsSelected(id) || (m_SelectedIds.empty() && m_Selection.id == id);
	};
	const auto include = [&](Vans::VansTimelineTick start, Vans::VansTimelineTick end)
	{
		minimum = minimum ? std::min(*minimum, start) : start;
		maximum = maximum ? std::max(*maximum, end) : end;
	};
	for (const auto& marker : m_Edit.Asset().markers)
		if (selected(marker.id)) include(marker.tick, marker.tick);
	for (const auto& track : m_Edit.Asset().tracks)
	{
		const bool includeTrack = selected(track.id) ||
			(m_Selection.kind == SelectionKind::Group && track.groupId == m_Selection.id) ||
			(m_Selection.kind == SelectionKind::Binding && track.bindingId == m_Selection.id);
		for (const auto& section : track.sections)
		{
			const bool includeSection = includeTrack || selected(section.id) ||
				((m_Selection.kind == SelectionKind::Channel || m_Selection.kind == SelectionKind::Key) &&
					m_Selection.trackId == track.id && m_Selection.sectionId == section.id &&
					m_Selection.kind == SelectionKind::Channel);
			if (includeSection) include(section.startTick, section.startTick + section.durationTicks);
			for (const auto& channel : section.channels)
				for (const auto& key : channel.keys)
					if (selected(key.id)) include(section.startTick + key.tick, section.startTick + key.tick);
		}
	}
	if ((!minimum || !maximum) && m_SelectionRangeStart && m_SelectionRangeEnd)
		include(*m_SelectionRangeStart, *m_SelectionRangeEnd);
	if (!minimum || !maximum) return;
	const Vans::VansTimelineTick frameTicks = std::max<Vans::VansTimelineTick>(1,
		Vans::VansTimelineTime::FrameToTick(1, m_Edit.Asset().timebase));
	const Vans::VansTimelineTick span = std::max(frameTicks, *maximum - *minimum);
	const Vans::VansTimelineTick padding = std::max(frameTicks, span / 20);
	m_ViewStart = *minimum - padding;
	m_PixelsPerTick = std::clamp(
		static_cast<double>(std::max(64.0f, m_LastCanvasWidth - 32.0f)) /
			static_cast<double>(span + padding * 2), 0.0002, 8.0);
}

void VansTimelineEditorWindow::FrameAll()
{
	const Vans::VansTimelineTick duration = std::max<Vans::VansTimelineTick>(1, m_Edit.Asset().durationTicks);
	const Vans::VansTimelineTick padding = std::max<Vans::VansTimelineTick>(1, duration / 40);
	m_ViewStart = -padding;
	m_PixelsPerTick = std::clamp(
		static_cast<double>(std::max(64.0f, m_LastCanvasWidth - 32.0f)) /
			static_cast<double>(duration + padding * 2), 0.0002, 8.0);
	m_CurveValueZoom = 1.0;
	m_CurveValuePan = 0.0;
}

void VansTimelineEditorWindow::SetPlaybackBoundary(bool start)
{
	const auto current = m_Edit.Asset().playbackRange;
	const Vans::VansTimelineTick startTick = start
		? std::clamp(m_Playhead, Vans::VansTimelineTick{ 0 }, current.endTick - 1)
		: current.startTick;
	const Vans::VansTimelineTick endTick = start
		? current.endTick
		: std::clamp(m_Playhead, current.startTick + 1, m_Edit.Asset().durationTicks);
	const auto result = m_Edit.SetPlaybackRange(startTick, endTick);
	if (!result) SetError(result.message);
	else RefreshPreview();
}

void VansTimelineEditorWindow::SetSelectionBoundary(bool start)
{
	if (start)
	{
		m_SelectionRangeStart = m_Playhead;
		if (!m_SelectionRangeEnd || *m_SelectionRangeEnd < m_Playhead)
			m_SelectionRangeEnd = m_Playhead;
	}
	else
	{
		m_SelectionRangeEnd = m_Playhead;
		if (!m_SelectionRangeStart || *m_SelectionRangeStart > m_Playhead)
			m_SelectionRangeStart = m_Playhead;
	}
}

void VansTimelineEditorWindow::BeginRenameSelection()
{
	std::string name;
	if (m_Selection.kind == SelectionKind::Asset)
		name = m_Edit.Asset().metadata.displayName;
	else if (m_Selection.kind == SelectionKind::Binding)
	{
		const auto found = std::find_if(m_Edit.Asset().bindings.begin(), m_Edit.Asset().bindings.end(),
			[&](const auto& binding) { return binding.id == m_Selection.id; });
		if (found != m_Edit.Asset().bindings.end()) name = found->displayName;
	}
	else if (m_Selection.kind == SelectionKind::Group)
	{
		const auto found = std::find_if(m_Edit.Asset().groups.begin(), m_Edit.Asset().groups.end(),
			[&](const auto& group) { return group.id == m_Selection.id; });
		if (found != m_Edit.Asset().groups.end()) name = found->name;
	}
	else if (m_Selection.kind == SelectionKind::Track)
	{
		if (const Vans::VansTimelineTrack* track = SelectedTrack()) name = track->name;
	}
	else if (m_Selection.kind == SelectionKind::Section)
	{
		if (const Vans::VansTimelineSection* section = SelectedSection()) name = section->name;
	}
	else if (m_Selection.kind == SelectionKind::Channel)
	{
		if (const Vans::VansTimelineChannel* channel = SelectedChannel()) name = channel->name;
	}
	else if (m_Selection.kind == SelectionKind::Marker)
	{
		if (const Vans::VansTimelineMarker* marker = SelectedMarker()) name = marker->label;
	}
	else return;

	std::memset(m_RenameBuffer, 0, sizeof(m_RenameBuffer));
	std::strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer) - 1);
	m_OpenRenamePopup = true;
}

void VansTimelineEditorWindow::DrawRenamePopup()
{
	if (m_OpenRenamePopup)
	{
		ImGui::OpenPopup("Rename Timeline Item");
		m_OpenRenamePopup = false;
	}
	if (!ImGui::BeginPopupModal("Rename Timeline Item", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
	const bool accepted = ImGui::InputText("Name", m_RenameBuffer, sizeof(m_RenameBuffer),
		ImGuiInputTextFlags_EnterReturnsTrue);
	if (accepted || ImGui::Button("Rename"))
	{
		const Vans::VansTimelineId objectId = m_Selection.kind == SelectionKind::Asset
			? Vans::VansTimelineId{} : m_Selection.id;
		const auto result = m_Edit.RenameObject(objectId, m_RenameBuffer);
		if (!result) SetError(result.message);
		else { RefreshPreview(); ImGui::CloseCurrentPopup(); }
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

Vans::VansTimelineTrack* VansTimelineEditorWindow::SelectedTrack()
{
	auto& tracks = m_Edit.PreviewAsset().tracks;
	const auto found = std::find_if(tracks.begin(), tracks.end(), [&](const auto& track)
	{
		return track.id == m_Selection.trackId || track.id == m_Selection.id;
	});
	return found == tracks.end() ? nullptr : &*found;
}

Vans::VansTimelineSection* VansTimelineEditorWindow::SelectedSection()
{
	Vans::VansTimelineTrack* track = SelectedTrack();
	if (!track) return nullptr;
	const auto found = std::find_if(track->sections.begin(), track->sections.end(), [&](const auto& section)
	{
		return section.id == m_Selection.sectionId || section.id == m_Selection.id;
	});
	return found == track->sections.end() ? nullptr : &*found;
}

Vans::VansTimelineChannel* VansTimelineEditorWindow::SelectedChannel()
{
	Vans::VansTimelineSection* section = SelectedSection();
	if (!section || m_Selection.channelIndex >= section->channels.size()) return nullptr;
	return &section->channels[m_Selection.channelIndex];
}

Vans::VansTimelineKey* VansTimelineEditorWindow::SelectedKey()
{
	Vans::VansTimelineChannel* channel = SelectedChannel();
	if (!channel) return nullptr;
	auto& keys = channel->keys;
	const auto found = std::find_if(keys.begin(), keys.end(), [&](const auto& key) { return key.id == m_Selection.id; });
	return found == keys.end() ? nullptr : &*found;
}

Vans::VansTimelineMarker* VansTimelineEditorWindow::SelectedMarker()
{
	auto& markers = m_Edit.PreviewAsset().markers;
	const auto found = std::find_if(markers.begin(), markers.end(),
		[&](const auto& marker) { return marker.id == m_Selection.id; });
	return found == markers.end() ? nullptr : &*found;
}

Vans::VansTimelineTick VansTimelineEditorWindow::AdjacentKeyTick(int direction) const
{
	const auto& asset = m_Edit.Asset();
	Vans::VansTimelineTick candidate = direction < 0
		? asset.playbackRange.startTick : asset.playbackRange.endTick;
	bool found = false;
	for (const auto& track : asset.tracks)
	{
		if (!track.enabled || track.runtimeMuted) continue;
		for (const auto& section : track.sections)
		{
			for (const auto& channel : section.channels)
			{
				for (const auto& key : channel.keys)
				{
					const Vans::VansTimelineTick globalTick = section.startTick + key.tick;
					if (direction < 0 && globalTick < m_Playhead && (!found || globalTick > candidate))
					{
						candidate = globalTick;
						found = true;
					}
					else if (direction >= 0 && globalTick > m_Playhead && (!found || globalTick < candidate))
					{
						candidate = globalTick;
						found = true;
					}
				}
			}
		}
	}
	return candidate;
}

void VansTimelineEditorWindow::EnsurePreview()
{
	if (!m_ActiveAPI || m_Preview.State() != Vans::VansTimelinePreviewState::Detached) return;
	std::string error;
	const Vans::VansTimelineAsset preview = BuildPreviewAsset();
	if (!m_Preview.Attach(*m_ActiveAPI, preview, m_Path, m_InstanceOwnerGuid,
		m_PreviewSafeEvents, m_IncludeSubTimelines, m_PlaybackDirection,
		m_LoopPlaybackRange, error))
		SetError(std::move(error));
}

void VansTimelineEditorWindow::RefreshPreview()
{
	if (m_Preview.State() == Vans::VansTimelinePreviewState::Detached) return;
	std::string error;
	const Vans::VansTimelineAsset preview = BuildPreviewAsset();
	if (!m_Preview.Refresh(preview, error)) SetError(std::move(error));
}

void VansTimelineEditorWindow::SeekPreview(Vans::VansTimelineTick tick)
{
	m_Playhead = std::clamp(tick, m_Edit.Asset().playbackRange.startTick, m_Edit.Asset().playbackRange.endTick);
	EnsurePreview();
	if (m_Preview.State() == Vans::VansTimelinePreviewState::Detached) return;
	std::string error;
	if (!m_Preview.Seek(m_Playhead, error)) SetError(std::move(error));
}

void VansTimelineEditorWindow::DrawToolbar()
{
	if (ImGui::Button("Save"))
	{
		if (m_ActiveAPI)
		{
			const auto result = m_Edit.Save(*m_ActiveAPI);
			if (!result) SetError(result.message);
			else { m_LastError.clear(); Vans::VansTimelineEditorStateStore::RemoveRecovery(m_Path); }
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!m_Edit.CanUndo());
	if (ImGui::Button("Undo")) { const auto result = m_Edit.Undo(); if (!result) SetError(result.message); else RefreshPreview(); }
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!m_Edit.CanRedo());
	if (ImGui::Button("Redo")) { const auto result = m_Edit.Redo(); if (!result) SetError(result.message); else RefreshPreview(); }
	ImGui::EndDisabled();
	ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
	if (ImGui::Button("|<")) SeekPreview(m_Edit.Asset().playbackRange.startTick);
	ImGui::SameLine();
	if (ImGui::Button("<K")) SeekPreview(AdjacentKeyTick(-1));
	ImGui::SameLine();
	bool reversePlayback = m_PlaybackDirection < 0;
	if (ImGui::Checkbox("Reverse", &reversePlayback))
	{
		m_PlaybackDirection = reversePlayback ? -1 : 1;
		EnsurePreview();
		std::string error;
		if (!m_Preview.ConfigurePlayback(m_PlaybackDirection, m_LoopPlaybackRange, error))
			SetError(std::move(error));
	}
	ImGui::SameLine();
	const bool playing = m_Preview.State() == Vans::VansTimelinePreviewState::Playing;
	if (ImGui::Button(playing ? "Pause" : "Play"))
	{
		EnsurePreview(); std::string error;
		if (playing ? !m_Preview.Pause(error) : !m_Preview.Play(error)) SetError(std::move(error));
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop")) { m_Preview.RestoreAndDetach(); m_Playhead = m_Edit.Asset().playbackRange.startTick; }
	ImGui::SameLine();
	if (ImGui::Button("K>")) SeekPreview(AdjacentKeyTick(1));
	ImGui::SameLine();
	if (ImGui::Button(">|")) SeekPreview(m_Edit.Asset().playbackRange.endTick);
	ImGui::SameLine();
	if (ImGui::Checkbox("Loop", &m_LoopPlaybackRange))
	{
		EnsurePreview();
		std::string error;
		if (!m_Preview.ConfigurePlayback(m_PlaybackDirection, m_LoopPlaybackRange, error))
			SetError(std::move(error));
	}
	ImGui::SameLine();
	const char* timeModes[] = { "Frames", "Seconds", "Timecode" };
	int timeMode = static_cast<int>(m_TimeDisplayMode);
	ImGui::SetNextItemWidth(92.0f);
	if (ImGui::Combo("##TimelineTimeMode", &timeMode, timeModes, 3))
		m_TimeDisplayMode = static_cast<TimeDisplayMode>(timeMode);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(150.0f);
	if (m_TimeDisplayMode == TimeDisplayMode::Frames)
	{
		std::int64_t frame = Vans::VansTimelineTime::TickToFrame(m_Playhead, m_Edit.Asset().timebase);
		if (ImGui::InputScalar("##TimelineTime", ImGuiDataType_S64, &frame))
			SeekPreview(Vans::VansTimelineTime::FrameToTick(frame, m_Edit.Asset().timebase));
	}
	else if (m_TimeDisplayMode == TimeDisplayMode::Seconds)
	{
		double seconds = static_cast<double>(m_Playhead) /
			static_cast<double>(std::max<std::int64_t>(1, m_Edit.Asset().timebase.ticksPerSecond));
		if (ImGui::InputDouble("##TimelineTime", &seconds, 0.0, 0.0, "%.3f"))
			SeekPreview(static_cast<Vans::VansTimelineTick>(std::llround(seconds * m_Edit.Asset().timebase.ticksPerSecond)));
	}
	else
	{
		char timecode[32]{};
		std::strncpy(timecode, FormatTick(m_Playhead).c_str(), sizeof(timecode) - 1);
		if (ImGui::InputText("##TimelineTime", timecode, sizeof(timecode), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			int hours = 0, minutes = 0, seconds = 0, frames = 0;
			char separator = ':';
			if (std::sscanf(timecode, "%d:%d:%d%c%d", &hours, &minutes, &seconds, &separator, &frames) == 5)
			{
				const auto& timebase = m_Edit.Asset().timebase;
				const int nominalRate = std::max(1, static_cast<int>(std::llround(
					static_cast<double>(timebase.displayRateNumerator) / timebase.displayRateDenominator)));
				std::int64_t frameNumber = ((hours * 60ll + minutes) * 60ll + seconds) * nominalRate + frames;
				if (separator == ';')
				{
					const int dropFrames = std::max(1, static_cast<int>(std::llround(nominalRate * 0.0666666667)));
					const std::int64_t totalMinutes = hours * 60ll + minutes;
					frameNumber -= dropFrames * (totalMinutes - totalMinutes / 10);
				}
				SeekPreview(Vans::VansTimelineTime::FrameToTick(frameNumber, timebase));
			}
			else SetError("Timecode must use HH:MM:SS:FF or HH:MM:SS;FF");
		}
	}
	ImGui::NewLine();
	ImGui::Checkbox("Snap", &m_SnapEnabled);
	ImGui::SameLine();
	if (ImGui::Button("Sources")) ImGui::OpenPopup("TimelineSnapSources");
	if (ImGui::BeginPopup("TimelineSnapSources"))
	{
		ImGui::Checkbox("Display Frames", &m_SnapFrames);
		ImGui::Checkbox("Keys", &m_SnapKeys);
		ImGui::Checkbox("Section Edges", &m_SnapSections);
		ImGui::Checkbox("Markers / Fences", &m_SnapMarkers);
		ImGui::Checkbox("Playback / Work Range", &m_SnapRanges);
		ImGui::Checkbox("Playhead", &m_SnapPlayhead);
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	const char* autoKeyModes[] = { "Auto Key: Off", "Auto Key: Existing", "Auto Key: All Allowed" };
	int autoKeyMode = static_cast<int>(m_AutoKeyMode);
	ImGui::SetNextItemWidth(165.0f);
	if (ImGui::Combo("##TimelineAutoKey", &autoKeyMode, autoKeyModes, 3))
		m_AutoKeyMode = static_cast<AutoKeyMode>(autoKeyMode);
	ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
	if (ImGui::RadioButton("Move", m_SectionEditMode == SectionEditMode::Move)) m_SectionEditMode = SectionEditMode::Move;
	ImGui::SameLine();
	if (ImGui::RadioButton("Slip", m_SectionEditMode == SectionEditMode::Slip)) m_SectionEditMode = SectionEditMode::Slip;
	ImGui::SameLine();
	if (ImGui::RadioButton("Ripple", m_SectionEditMode == SectionEditMode::Ripple)) m_SectionEditMode = SectionEditMode::Ripple;
	ImGui::SameLine();
	if (ImGui::RadioButton("Scale", m_SectionEditMode == SectionEditMode::Scale)) m_SectionEditMode = SectionEditMode::Scale;
	ImGui::SameLine();
	if (ImGui::RadioButton("Loop", m_SectionEditMode == SectionEditMode::LoopExtend)) m_SectionEditMode = SectionEditMode::LoopExtend;
	ImGui::SameLine();
	ImGui::BeginDisabled(SelectedSection() == nullptr);
	if (ImGui::Button("Split")) SplitSelection();
	ImGui::SameLine();
	if (ImGui::Button("Duplicate")) DuplicateSelection();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Checkbox("Safe Events", &m_PreviewSafeEvents))
	{
		m_Preview.SetSafeEvents(m_PreviewSafeEvents);
		RefreshPreview();
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("SubTimelines", &m_IncludeSubTimelines))
	{
		m_Preview.RestoreAndDetach();
		EnsurePreview();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Curves", &m_ShowCurves);
	ImGui::SameLine();
	ImGui::Checkbox("Waveforms", &m_ShowWaveforms);
	ImGui::SameLine();
	ImGui::Checkbox("Thumbnails", &m_ShowThumbnails);
	ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
	if (ImGui::Button("+ Marker")) AddMarkerAtPlayhead();
	ImGui::SameLine();
	if (ImGui::Button("Frame Sel")) FrameSelection();
	ImGui::SameLine();
	if (ImGui::Button("Frame All")) FrameAll();
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	ImGui::SliderFloat("Rows", &m_RowHeight, 24.0f, 48.0f, "%.0f px");
}

void VansTimelineEditorWindow::DrawAddTrackMenu()
{
	std::string activeCategory;
	for (const auto& descriptor : Vans::VansTimelineTrackDescriptorRegistry::All())
	{
		if (descriptor.category != activeCategory)
		{
			if (!activeCategory.empty()) ImGui::Separator();
			activeCategory = descriptor.category;
			ImGui::TextDisabled("%s", activeCategory.c_str());
		}
		const bool hasBinding = m_Selection.kind == SelectionKind::Binding || !m_Selection.trackId.empty();
		const bool available = (!descriptor.bindingRequired || hasBinding) &&
			descriptor.capability != Vans::VansTimelineEditorCapabilityLevel::RegisteredOnly;
		ImGui::BeginDisabled(!available);
		if (ImGui::MenuItem(descriptor.displayName.c_str()))
		{
			Vans::VansTimelineId bindingId;
			if (m_Selection.kind == SelectionKind::Binding) bindingId = m_Selection.id;
			else if (Vans::VansTimelineTrack* selected = SelectedTrack()) bindingId = selected->bindingId;
			const auto result = m_Edit.AddTrack(descriptor.type, bindingId);
			if (!result) SetError(result.message);
			else { Select({ SelectionKind::Track, result.objectId, result.objectId, {} }); RefreshPreview(); }
		}
		ImGui::EndDisabled();
	}
}

void VansTimelineEditorWindow::DrawOutliner()
{
	ImGui::InputTextWithHint("##TimelineSearch", "Filter tracks", m_Search, sizeof(m_Search));
	if (ImGui::Button("+ Binding"))
	{
		Vans::VansTimelineBinding binding;
		binding.displayName = "Binding";
		binding.kind = Vans::VansTimelineBindingKind::SceneEntity;
		binding.targetGuid = Vans::VansEditorSelection::EntityGuid();
		binding.required = !binding.targetGuid.empty();
		const auto result = m_Edit.AddBinding(std::move(binding));
		if (!result) SetError(result.message);
		else Select({ SelectionKind::Binding, result.objectId });
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Track")) ImGui::OpenPopup("TimelineAddTrack");
	ImGui::SameLine();
	if (ImGui::Button("+ Group"))
	{
		Vans::VansTimelineGroup group;
		group.name = "Group";
		if (m_Selection.kind == SelectionKind::Group) group.parentId = m_Selection.id;
		const auto result = m_Edit.AddGroup(std::move(group));
		if (!result) SetError(result.message);
		else Select({ SelectionKind::Group, result.objectId });
	}
	if (ImGui::BeginPopup("TimelineAddTrack")) { DrawAddTrackMenu(); ImGui::EndPopup(); }
	ImGui::Separator();

	for (const auto& binding : m_Edit.Asset().bindings)
	{
		if (!MatchesSearch(binding.displayName, m_Search)) continue;
		ImGui::PushID(binding.id.c_str());
		const bool selected = m_Selection.kind == SelectionKind::Binding && m_Selection.id == binding.id;
		if (ImGui::Selectable(binding.displayName.c_str(), selected))
			Select({ SelectionKind::Binding, binding.id }, ImGui::GetIO().KeyCtrl);
		ImGui::PopID();
	}
	ImGui::Separator();

	Vans::VansTimelineId pendingTrack;
	Vans::VansTimelineId pendingGroup;
	Vans::VansTimelineId pendingBefore;
	const auto bindingFor = [&](const Vans::VansTimelineId& id) -> const Vans::VansTimelineBinding*
	{
		const auto found = std::find_if(m_Edit.Asset().bindings.begin(), m_Edit.Asset().bindings.end(),
			[&](const auto& binding) { return binding.id == id; });
		return found == m_Edit.Asset().bindings.end() ? nullptr : &*found;
	};
	const auto drawTrack = [&](const Vans::VansTimelineTrack& track)
	{
		const auto* descriptor = Vans::VansTimelineTrackDescriptorRegistry::Find(track.type);
		const Vans::VansTimelineBinding* binding = bindingFor(track.bindingId);
		const std::string searchable = track.name + " " + (descriptor ? descriptor->displayName : std::string{}) +
			" " + (binding ? binding->displayName : std::string{});
		if (!MatchesSearch(searchable, m_Search)) return;
		ImGui::PushID(track.id.c_str());
		const bool selected = IsSelected(track.id) || (m_Selection.kind == SelectionKind::Track && m_Selection.id == track.id) ||
			m_Selection.trackId == track.id;
		const bool sessionMuted = m_SessionMutedTracks.find(track.id) != m_SessionMutedTracks.end();
		const bool sessionSolo = m_SessionSoloTracks.find(track.id) != m_SessionSoloTracks.end();
		const bool hidden = m_HiddenTracks.find(track.id) != m_HiddenTracks.end();
		const bool pinned = m_PinnedTracks.find(track.id) != m_PinnedTracks.end();
		const bool bindingMissing = descriptor && descriptor->bindingRequired &&
			(!binding || (binding->targetGuid.empty() && binding->assetGuid.empty()));
		const std::string label = std::string(bindingMissing ? "! " : "") +
			(descriptor ? "[" + descriptor->stableTypeId + "] " : std::string{}) + track.name;
		if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick,
			ImVec2(std::max(40.0f, ImGui::GetContentRegionAvail().x - 166.0f),
				TrackRowHeight(track, m_RowHeight))))
			Select({ SelectionKind::Track, track.id, track.id, {} }, ImGui::GetIO().KeyCtrl);
		if (bindingMissing && ImGui::IsItemHovered()) ImGui::SetTooltip("Required binding is missing");
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("TIMELINE_TRACK_ID", track.id.c_str(), track.id.size() + 1);
			ImGui::TextUnformatted(track.name.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TIMELINE_TRACK_ID"))
			{
				pendingTrack = static_cast<const char*>(payload->Data);
				pendingGroup = track.groupId;
				pendingBefore = track.id;
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton(sessionMuted ? "M*" : "M"))
		{
			if (sessionMuted) m_SessionMutedTracks.erase(track.id); else m_SessionMutedTracks.insert(track.id);
			RefreshPreview();
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mute in this editor session");
		ImGui::SameLine();
		if (ImGui::SmallButton(sessionSolo ? "S*" : "S"))
		{
			if (sessionSolo) m_SessionSoloTracks.erase(track.id); else m_SessionSoloTracks.insert(track.id);
			RefreshPreview();
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo in this editor session");
		ImGui::SameLine();
		if (ImGui::SmallButton(track.locked ? "L*" : "L"))
		{
			const auto result = m_Edit.SetTrackLocked(track.id, !track.locked);
			if (!result) SetError(result.message);
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock authored track edits");
		ImGui::SameLine();
		if (ImGui::SmallButton(pinned ? "P*" : "P"))
		{
			if (pinned) m_PinnedTracks.erase(track.id); else m_PinnedTracks.insert(track.id);
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pin this track to the top for this editor session");
		ImGui::SameLine();
		if (ImGui::SmallButton(hidden ? "H*" : "H"))
		{
			if (hidden) m_HiddenTracks.erase(track.id); else m_HiddenTracks.insert(track.id);
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle canvas visibility");
		ImGui::SameLine();
		if (ImGui::SmallButton("+"))
		{
			const auto* descriptor = Vans::VansTimelineTrackDescriptorRegistry::Find(track.type);
			if (descriptor && descriptor->sectionAssetType != Vans::EditorAPI::AssetType::Unknown)
				SetError("Drop a compatible project asset onto this track to create a section");
			else
			{
				Vans::VansTimelineSection section;
				section.startTick = m_Playhead;
				section.durationTicks = std::max<Vans::VansTimelineTick>(1,
					Vans::VansTimelineTime::FrameToTick(30, m_Edit.Asset().timebase));
				const auto result = m_Edit.AddSection(track.id, std::move(section));
				if (!result) SetError(result.message);
				else Select({ SelectionKind::Section, result.objectId, track.id, result.objectId });
			}
		}
		ImGui::PopID();
	};

	bool hasPinned = false;
	for (const auto& track : m_Edit.Asset().tracks)
		if (m_PinnedTracks.find(track.id) != m_PinnedTracks.end()) { hasPinned = true; drawTrack(track); }
	if (hasPinned) ImGui::SeparatorText("Tracks");
	if (ImGui::Selectable("Ungrouped", m_Selection.kind == SelectionKind::Asset, ImGuiSelectableFlags_Disabled)) {}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TIMELINE_TRACK_ID"))
		{
			pendingTrack = static_cast<const char*>(payload->Data);
			pendingGroup.clear();
			pendingBefore.clear();
		}
		ImGui::EndDragDropTarget();
	}
	for (const auto& track : m_Edit.Asset().tracks)
		if (track.groupId.empty() && m_PinnedTracks.find(track.id) == m_PinnedTracks.end()) drawTrack(track);

	std::function<void(const Vans::VansTimelineGroup&, int)> drawGroup;
	drawGroup = [&](const Vans::VansTimelineGroup& group, int depth)
	{
		ImGui::PushID(group.id.c_str());
		ImGui::Indent(static_cast<float>(depth) * 12.0f);
		const bool selected = m_Selection.kind == SelectionKind::Group && m_Selection.id == group.id;
		if (ImGui::Selectable(("Group: " + group.name).c_str(), selected))
			Select({ SelectionKind::Group, group.id }, ImGui::GetIO().KeyCtrl);
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TIMELINE_TRACK_ID"))
			{
				pendingTrack = static_cast<const char*>(payload->Data);
				pendingGroup = group.id;
				pendingBefore.clear();
			}
			ImGui::EndDragDropTarget();
		}
		for (const auto& track : m_Edit.Asset().tracks)
			if (track.groupId == group.id && m_PinnedTracks.find(track.id) == m_PinnedTracks.end()) drawTrack(track);
		for (const auto& child : m_Edit.Asset().groups)
			if (child.parentId == group.id) drawGroup(child, depth + 1);
		ImGui::Unindent(static_cast<float>(depth) * 12.0f);
		ImGui::PopID();
	};
	for (const auto& group : m_Edit.Asset().groups)
		if (group.parentId.empty()) drawGroup(group, 0);
	if (!pendingTrack.empty() && pendingTrack != pendingBefore)
	{
		const auto result = m_Edit.MoveTrack(pendingTrack, pendingGroup, pendingBefore);
		if (!result) SetError(result.message);
		else RefreshPreview();
	}
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
	if (m_TimeDisplayMode == TimeDisplayMode::Frames)
		return std::to_string(Vans::VansTimelineTime::TickToFrame(tick, m_Edit.Asset().timebase));
	if (m_TimeDisplayMode == TimeDisplayMode::Seconds)
	{
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(3) << static_cast<double>(tick) /
			static_cast<double>(std::max<std::int64_t>(1, m_Edit.Asset().timebase.ticksPerSecond));
		return stream.str();
	}
	const auto& timebase = m_Edit.Asset().timebase;
	const bool dropFrame = timebase.displayRateDenominator == 1001 &&
		(timebase.displayRateNumerator == 30000 || timebase.displayRateNumerator == 60000);
	return Vans::VansTimelineTime::FormatTimecode(tick, timebase, dropFrame);
}

Vans::VansTimelineTick VansTimelineEditorWindow::SnapTick(Vans::VansTimelineTick tick) const
{
	m_HasSnapTarget = false;
	const bool enabled = ImGui::GetIO().KeyAlt ? !m_SnapEnabled : m_SnapEnabled;
	if (!enabled) return tick;
	Vans::VansTimelineTick best = tick;
	Vans::VansTimelineTick distance = std::numeric_limits<Vans::VansTimelineTick>::max();
	auto consider = [&](Vans::VansTimelineTick candidate)
	{
		const auto candidateDistance = std::llabs(candidate - tick);
		if (candidateDistance < distance) { best = candidate; distance = candidateDistance; }
	};
	if (m_SnapFrames)
	{
		const auto frame = Vans::VansTimelineTime::TickToFrame(tick, m_Edit.Asset().timebase);
		consider(Vans::VansTimelineTime::FrameToTick(frame, m_Edit.Asset().timebase));
	}
	if (m_SnapMarkers) for (const auto& marker : m_Edit.Asset().markers) consider(marker.tick);
	if (m_SnapKeys) for (const auto& track : m_Edit.Asset().tracks) for (const auto& section : track.sections)
		for (const auto& channel : section.channels)
		{
			const Vans::VansTimelineTick localTick = tick - section.startTick;
			const auto next = std::lower_bound(channel.keys.begin(), channel.keys.end(), localTick,
				[](const auto& key, Vans::VansTimelineTick candidate) { return key.tick < candidate; });
			if (next != channel.keys.end()) consider(section.startTick + next->tick);
			if (next != channel.keys.begin()) consider(section.startTick + std::prev(next)->tick);
		}
	if (m_SnapSections) for (const auto& track : m_Edit.Asset().tracks) for (const auto& section : track.sections)
	{
		consider(section.startTick); consider(section.startTick + section.durationTicks);
	}
	if (m_SnapRanges)
	{
		consider(m_Edit.Asset().playbackRange.startTick); consider(m_Edit.Asset().playbackRange.endTick);
		consider(m_Edit.Asset().workRange.startTick); consider(m_Edit.Asset().workRange.endTick);
	}
	if (m_SnapPlayhead) consider(m_Playhead);
	const Vans::VansTimelineTick threshold = std::max<Vans::VansTimelineTick>(1,
		static_cast<Vans::VansTimelineTick>(8.0 / m_PixelsPerTick));
	if (distance <= threshold)
	{
		m_LastSnapTarget = best;
		m_HasSnapTarget = best != tick;
		return best;
	}
	return tick;
}

void VansTimelineEditorWindow::DrawCanvas()
{
	const ImVec2 canvasPosition = ImGui::GetCursorScreenPos();
	const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
	m_LastCanvasWidth = std::max(64.0f, canvasSize.x);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float rulerHeight = 28.0f;
	draw->AddRectFilled(canvasPosition, ImVec2(canvasPosition.x + canvasSize.x, canvasPosition.y + canvasSize.y),
		IM_COL32(25, 27, 31, 255));
	const auto rangeX = [&](Vans::VansTimelineTick tick)
	{
		return std::clamp(static_cast<float>(TickToX(tick, canvasPosition.x)),
			canvasPosition.x, canvasPosition.x + canvasSize.x);
	};
	const float workStartX = rangeX(m_Edit.Asset().workRange.startTick);
	const float workEndX = rangeX(m_Edit.Asset().workRange.endTick);
	if (workEndX > workStartX)
		draw->AddRectFilled(ImVec2(workStartX, canvasPosition.y),
			ImVec2(workEndX, canvasPosition.y + canvasSize.y), IM_COL32(41, 44, 50, 125));
	const float playbackStartX = rangeX(m_Edit.Asset().playbackRange.startTick);
	const float playbackEndX = rangeX(m_Edit.Asset().playbackRange.endTick);
	if (playbackEndX > playbackStartX)
		draw->AddRectFilled(ImVec2(playbackStartX, canvasPosition.y),
			ImVec2(playbackEndX, canvasPosition.y + canvasSize.y), IM_COL32(55, 64, 72, 75));
	if (m_SelectionRangeStart && m_SelectionRangeEnd && *m_SelectionRangeEnd > *m_SelectionRangeStart)
	{
		const float selectionStartX = rangeX(*m_SelectionRangeStart);
		const float selectionEndX = rangeX(*m_SelectionRangeEnd);
		draw->AddRectFilled(ImVec2(selectionStartX, canvasPosition.y),
			ImVec2(selectionEndX, canvasPosition.y + canvasSize.y), IM_COL32(94, 174, 218, 42));
		draw->AddLine(ImVec2(selectionStartX, canvasPosition.y),
			ImVec2(selectionStartX, canvasPosition.y + canvasSize.y), IM_COL32(120, 201, 239, 180));
		draw->AddLine(ImVec2(selectionEndX, canvasPosition.y),
			ImVec2(selectionEndX, canvasPosition.y + canvasSize.y), IM_COL32(120, 201, 239, 180));
	}
	draw->AddRectFilled(canvasPosition, ImVec2(canvasPosition.x + canvasSize.x, canvasPosition.y + rulerHeight),
		IM_COL32(35, 38, 44, 255));

	const auto frameTicks = std::max<Vans::VansTimelineTick>(1,
		Vans::VansTimelineTime::FrameToTick(1, m_Edit.Asset().timebase));
	Vans::VansTimelineTick gridStep = frameTicks;
	while (gridStep * m_PixelsPerTick < 48.0) gridStep *= 5;
	const Vans::VansTimelineTick firstGrid = (m_ViewStart / gridStep) * gridStep;
	for (Vans::VansTimelineTick tick = firstGrid; ; tick += gridStep)
	{
		const float x = static_cast<float>(TickToX(tick, canvasPosition.x));
		if (x > canvasPosition.x + canvasSize.x) break;
		if (x >= canvasPosition.x)
		{
			draw->AddLine(ImVec2(x, canvasPosition.y), ImVec2(x, canvasPosition.y + canvasSize.y), IM_COL32(72, 76, 84, 110));
			const std::string label = FormatTick(tick);
			draw->AddText(ImVec2(x + 4.0f, canvasPosition.y + 6.0f), IM_COL32(190, 195, 204, 255), label.c_str());
		}
		if (tick > m_Edit.Asset().durationTicks + gridStep) break;
	}
	std::vector<Vans::VansTimelineTrack*> orderedTracks;
	orderedTracks.reserve(m_Edit.PreviewAsset().tracks.size());
	for (auto& track : m_Edit.PreviewAsset().tracks)
		if (m_PinnedTracks.find(track.id) != m_PinnedTracks.end()) orderedTracks.push_back(&track);
	for (auto& track : m_Edit.PreviewAsset().tracks)
		if (m_PinnedTracks.find(track.id) == m_PinnedTracks.end()) orderedTracks.push_back(&track);
	float visibleTrackHeight = 0.0f;
	for (const auto* track : orderedTracks)
		if (MatchesSearch(track->name, m_Search) && m_HiddenTracks.find(track->id) == m_HiddenTracks.end())
			visibleTrackHeight += TrackRowHeight(*track, m_RowHeight);
	const float maximumTrackScroll = std::max(0.0f,
		visibleTrackHeight - std::max(0.0f, canvasSize.y - rulerHeight));
	m_TrackScroll = std::clamp(m_TrackScroll, 0.0f, maximumTrackScroll);

	ImGui::InvisibleButton("TimelineCanvas", canvasSize,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
	const bool hovered = ImGui::IsItemHovered();
	const bool canvasLeftClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		ImGui::GetIO().MousePos.y >= canvasPosition.y + rulerHeight;
	if (hovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f)
	{
		const Vans::VansTimelineTick anchor = XToTick(ImGui::GetIO().MousePos.x, canvasPosition.x);
		m_PixelsPerTick = std::clamp(m_PixelsPerTick * (ImGui::GetIO().MouseWheel > 0.0f ? 1.2 : 0.8), 0.0002, 8.0);
		m_ViewStart = anchor - static_cast<Vans::VansTimelineTick>((ImGui::GetIO().MousePos.x - canvasPosition.x) / m_PixelsPerTick);
	}
	else if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
	{
		if (ImGui::GetIO().KeyShift)
			m_ViewStart -= static_cast<Vans::VansTimelineTick>(
				ImGui::GetIO().MouseWheel * 96.0 / m_PixelsPerTick);
		else m_TrackScroll = std::clamp(m_TrackScroll - ImGui::GetIO().MouseWheel * m_RowHeight * 3.0f,
			0.0f, maximumTrackScroll);
	}
	if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
		ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
		m_ViewStart -= static_cast<Vans::VansTimelineTick>(ImGui::GetIO().MouseDelta.x / m_PixelsPerTick);
	if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::GetIO().MousePos.y < canvasPosition.y + rulerHeight)
		SeekPreview(SnapTick(XToTick(ImGui::GetIO().MousePos.x, canvasPosition.x)));

	if (m_CanvasDrag.kind != CanvasDragKind::None)
	{
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			const auto delta = static_cast<Vans::VansTimelineTick>(std::llround(
				(ImGui::GetIO().MousePos.x - m_CanvasDrag.initialMouseX) / m_PixelsPerTick));
			Vans::TimelineEditResult result{ true, {}, {} };
			if (m_CanvasDrag.kind == CanvasDragKind::MoveSection)
			{
				if (m_SectionEditMode == SectionEditMode::Slip)
				{
					result = m_Edit.SlipSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId,
						delta - m_CanvasDrag.lastDeltaTick);
					if (result) m_CanvasDrag.lastDeltaTick = delta;
				}
				else if (m_SectionEditMode == SectionEditMode::Ripple)
					result = m_Edit.RippleMoveSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId,
						SnapTick(m_CanvasDrag.initialStartTick + delta));
				else if (m_SelectedIds.size() > 1 &&
					m_SelectedIds.find(m_CanvasDrag.sectionId) != m_SelectedIds.end())
				{
					result = m_Edit.MoveSectionsBy(m_SelectedIds, delta - m_CanvasDrag.lastDeltaTick);
					if (result) m_CanvasDrag.lastDeltaTick = delta;
				}
				else
					result = m_Edit.MoveSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId,
						SnapTick(m_CanvasDrag.initialStartTick + delta));
			}
			else if (m_CanvasDrag.kind == CanvasDragKind::TrimSectionStart)
			{
				const auto endTick = m_CanvasDrag.initialStartTick + m_CanvasDrag.initialDurationTicks;
				const auto startTick = std::min(SnapTick(m_CanvasDrag.initialStartTick + delta), endTick - 1);
				result = m_SectionEditMode == SectionEditMode::Ripple
					? m_Edit.RippleTrimSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId, startTick, endTick - startTick)
					: m_Edit.TrimSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId, startTick, endTick - startTick);
			}
			else if (m_CanvasDrag.kind == CanvasDragKind::TrimSectionEnd)
			{
				const auto endTick = std::max(SnapTick(m_CanvasDrag.initialStartTick +
					m_CanvasDrag.initialDurationTicks + delta), m_CanvasDrag.initialStartTick + 1);
				if (m_SectionEditMode == SectionEditMode::Ripple)
					result = m_Edit.RippleTrimSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId,
						m_CanvasDrag.initialStartTick, endTick - m_CanvasDrag.initialStartTick);
				else if (m_SectionEditMode == SectionEditMode::Scale)
					result = m_Edit.ScaleSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId,
						endTick - m_CanvasDrag.initialStartTick);
				else if (m_SectionEditMode == SectionEditMode::LoopExtend)
					result = m_Edit.LoopExtendSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId,
						endTick - m_CanvasDrag.initialStartTick);
				else result = m_Edit.TrimSection(m_CanvasDrag.trackId, m_CanvasDrag.sectionId,
					m_CanvasDrag.initialStartTick, endTick - m_CanvasDrag.initialStartTick);
			}
			else if (m_CanvasDrag.kind == CanvasDragKind::MoveKey)
			{
				const auto globalTick = SnapTick(m_CanvasDrag.initialStartTick + m_CanvasDrag.initialKeyTick + delta);
				result = m_Edit.MoveKey(m_CanvasDrag.trackId, m_CanvasDrag.sectionId,
					m_CanvasDrag.channelIndex, m_CanvasDrag.keyId, globalTick - m_CanvasDrag.initialStartTick);
			}
			else if (m_CanvasDrag.kind == CanvasDragKind::MoveMarker)
				result = m_Edit.MoveMarker(m_CanvasDrag.keyId,
					SnapTick(m_CanvasDrag.initialKeyTick + delta));
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

	float y = canvasPosition.y + rulerHeight - m_TrackScroll;
	const Vans::VansTimelineTick visibleStartTick = XToTick(canvasPosition.x, canvasPosition.x);
	const Vans::VansTimelineTick visibleEndTick = XToTick(canvasPosition.x + canvasSize.x, canvasPosition.x);
	struct CanvasSelectionCandidate
	{
		Selection selection;
		ImVec2 minimum;
		ImVec2 maximum;
	};
	std::vector<CanvasSelectionCandidate> sectionCandidates;
	std::vector<CanvasSelectionCandidate> keyCandidates;
	bool canvasObjectClicked = false;
	for (auto& marker : m_Edit.PreviewAsset().markers)
	{
		const float x = static_cast<float>(TickToX(marker.tick, canvasPosition.x));
		if (x < canvasPosition.x - 8.0f || x > canvasPosition.x + canvasSize.x + 8.0f) continue;
		const bool selected = IsSelected(marker.id) ||
			(m_Selection.kind == SelectionKind::Marker && m_Selection.id == marker.id);
		const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(
			marker.color[0], marker.color[1], marker.color[2], marker.color[3]));
		draw->AddLine(ImVec2(x, canvasPosition.y + rulerHeight - 6.0f),
			ImVec2(x, canvasPosition.y + canvasSize.y), color, selected ? 2.0f : 1.0f);
		draw->AddTriangleFilled(ImVec2(x - 6.0f, canvasPosition.y + rulerHeight - 7.0f),
			ImVec2(x + 6.0f, canvasPosition.y + rulerHeight - 7.0f),
			ImVec2(x, canvasPosition.y + rulerHeight - 1.0f), color);
		if (selected && !marker.label.empty())
			draw->AddText(ImVec2(x + 5.0f, canvasPosition.y + 5.0f), color, marker.label.c_str());
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			ImGui::GetIO().MousePos.y < canvasPosition.y + rulerHeight &&
			std::abs(ImGui::GetIO().MousePos.x - x) <= 8.0f)
		{
			Select({ SelectionKind::Marker, marker.id }, ImGui::GetIO().KeyCtrl);
			canvasObjectClicked = true;
			if (!ImGui::GetIO().KeyCtrl && m_Edit.BeginInteraction())
				m_CanvasDrag = { CanvasDragKind::MoveMarker, {}, {}, marker.id, 0,
					0, 0, marker.tick, ImGui::GetIO().MousePos.x };
		}
	}
	for (Vans::VansTimelineTrack* trackPointer : orderedTracks)
	{
		auto& track = *trackPointer;
		const float rowHeight = TrackRowHeight(track, m_RowHeight);
		if (!MatchesSearch(track.name, m_Search)) continue;
		if (m_HiddenTracks.find(track.id) != m_HiddenTracks.end()) continue;
		if (y + rowHeight < canvasPosition.y + rulerHeight || y > canvasPosition.y + canvasSize.y)
		{
			y += rowHeight;
			continue;
		}
		const bool hasSolo = !m_SessionSoloTracks.empty();
		const bool sessionDisabled = m_SessionMutedTracks.find(track.id) != m_SessionMutedTracks.end() ||
			(hasSolo && m_SessionSoloTracks.find(track.id) == m_SessionSoloTracks.end());
		draw->AddLine(ImVec2(canvasPosition.x, y + rowHeight), ImVec2(canvasPosition.x + canvasSize.x, y + rowHeight),
			IM_COL32(54, 57, 63, 180));
		bool clickConsumed = false;
		for (auto& section : track.sections)
		{
			const float x0 = static_cast<float>(TickToX(section.startTick, canvasPosition.x));
			const float x1 = static_cast<float>(TickToX(section.startTick + section.durationTicks, canvasPosition.x));
			if (x1 < canvasPosition.x || x0 > canvasPosition.x + canvasSize.x) continue;
			const bool selected = IsSelected(section.id) ||
				(m_Selection.kind == SelectionKind::Section && m_Selection.id == section.id);
			const ImVec2 min(std::max(x0, canvasPosition.x), y + 4.0f);
			const ImVec2 max(std::min(x1, canvasPosition.x + canvasSize.x), y + rowHeight - 4.0f);
			sectionCandidates.push_back({ { SelectionKind::Section, section.id, track.id, section.id }, min, max });
			ImU32 color = TrackColor(track, selected);
			if (sessionDisabled) color = (color & 0x00FFFFFFu) | (0x36u << 24u);
			draw->AddRectFilled(min, max, color, 3.0f);
			std::string sectionLabel = section.name;
			if (track.type == Vans::VansTimelineTrackType::Audio && m_ShowWaveforms)
			{
				if (const auto waveform = m_ActiveAPI
					? m_ActiveAPI->RequestTimelineAudioWaveform(section.assetGuid) : nullptr;
					waveform && waveform->durationSeconds > 0.0 && !waveform->minima.empty())
				{
					const int columns = std::clamp(static_cast<int>((max.x - min.x) * 0.5f), 1, 512);
					const float centerY = (min.y + max.y) * 0.5f;
					const float amplitude = std::max(1.0f, (max.y - min.y) * 0.42f);
					for (int column = 0; column < columns; ++column)
					{
						const double ratio = columns == 1 ? 0.0 : static_cast<double>(column) / (columns - 1);
						double sourceTick = section.sourceInTick + ratio * section.durationTicks * section.playRate;
						if (section.reverse)
						{
							const double endTick = section.sourceOutTick >= 0
								? static_cast<double>(section.sourceOutTick)
								: section.sourceInTick + section.durationTicks * section.playRate;
							sourceTick = endTick - ratio * section.durationTicks * section.playRate;
						}
						double seconds = Vans::VansTimelineTime::TickToSeconds(
							static_cast<Vans::VansTimelineTick>(std::llround(sourceTick)), m_Edit.Asset().timebase);
						if (section.loopMode != Vans::VansTimelineLoopMode::None)
							seconds = std::fmod(std::max(0.0, seconds), waveform->durationSeconds);
						const double normalized = std::clamp(seconds / waveform->durationSeconds, 0.0, 1.0);
						const std::size_t bin = std::min(waveform->minima.size() - 1,
							static_cast<std::size_t>(normalized * (waveform->minima.size() - 1)));
						const float x = min.x + (max.x - min.x) * static_cast<float>(ratio);
						draw->AddLine(ImVec2(x, centerY - waveform->maxima[bin] * amplitude),
							ImVec2(x, centerY - waveform->minima[bin] * amplitude),
							IM_COL32(224, 244, 236, 185));
					}
				}
			}
			else if (track.type == Vans::VansTimelineTrackType::Media && m_ShowThumbnails)
			{
				if (const auto thumbnail = m_ActiveAPI
					? m_ActiveAPI->RequestTimelineVideoThumbnail(section.assetGuid) : nullptr;
					thumbnail && thumbnail->width > 0 && thumbnail->height > 0 && !thumbnail->rgba.empty())
				{
					const int cellsX = 16;
					const int cellsY = 9;
					const float thumbnailWidth = std::min(max.x - min.x, (max.y - min.y) * 16.0f / 9.0f);
					for (int cellY = 0; cellY < cellsY; ++cellY)
						for (int cellX = 0; cellX < cellsX; ++cellX)
						{
							const int sourceX = cellX * thumbnail->width / cellsX;
							const int sourceY = cellY * thumbnail->height / cellsY;
							const std::size_t pixel = (static_cast<std::size_t>(sourceY) * thumbnail->width + sourceX) * 4;
							const ImU32 pixelColor = IM_COL32(thumbnail->rgba[pixel], thumbnail->rgba[pixel + 1],
								thumbnail->rgba[pixel + 2], 210);
							draw->AddRectFilled(
								ImVec2(min.x + thumbnailWidth * cellX / cellsX, min.y + (max.y - min.y) * cellY / cellsY),
								ImVec2(min.x + thumbnailWidth * (cellX + 1) / cellsX,
									min.y + (max.y - min.y) * (cellY + 1) / cellsY), pixelColor);
						}
				}
			}
			else if (track.type == Vans::VansTimelineTrackType::CameraCut && m_ShowThumbnails)
			{
				const auto* shot = std::get_if<Vans::VansTimelineCameraCutTrackConfig>(&section.config);
				if (!shot) shot = std::get_if<Vans::VansTimelineCameraCutTrackConfig>(&track.config);
				if (shot)
				{
					const float slateWidth = std::min(max.x - min.x, (max.y - min.y) * 16.0f / 9.0f);
					const bool currentShot = m_Playhead >= section.startTick &&
						m_Playhead < section.startTick + section.durationTicks;
					const auto preview = currentShot && m_ActiveAPI
						? m_ActiveAPI->GetViewportPreview(Vans::EditorAPI::MainViewportId)
						: Vans::EditorAPI::RenderTexturePreview{};
					if (preview.texture)
						draw->AddImage(preview.texture, min, ImVec2(min.x + slateWidth, max.y));
					else
					{
						const ImU32 shotColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
							shot->shotColor[0], shot->shotColor[1], shot->shotColor[2], 0.7f));
						draw->AddRectFilled(min, ImVec2(min.x + slateWidth, max.y), shotColor, 2.0f);
						draw->AddLine(min, ImVec2(min.x + slateWidth, max.y), IM_COL32(238, 242, 247, 150));
					}
					if (!shot->shotName.empty()) sectionLabel = shot->shotName;
				}
			}
			else if (track.type == Vans::VansTimelineTrackType::Particle)
			{
				const auto* particle = std::get_if<Vans::VansTimelineParticleTrackConfig>(&section.config);
				if (!particle) particle = std::get_if<Vans::VansTimelineParticleTrackConfig>(&track.config);
				if (particle) sectionLabel += " | " + particle->action;
			}
			if (section.easeInTicks > 0 || section.easeOutTicks > 0)
			{
				ImVec2 previous{};
				for (int sample = 0; sample <= 24; ++sample)
				{
					const double ratio = static_cast<double>(sample) / 24.0;
					const auto localTick = static_cast<Vans::VansTimelineTick>(std::llround(
						ratio * section.durationTicks));
					double weight = 1.0;
					if (section.easeInTicks > 0 && localTick < section.easeInTicks)
						weight = std::min(weight, BlendAlpha(section.blendIn,
							static_cast<double>(localTick) / section.easeInTicks));
					const auto remaining = section.durationTicks - localTick;
					if (section.easeOutTicks > 0 && remaining < section.easeOutTicks)
						weight = std::min(weight, BlendAlpha(section.blendOut,
							static_cast<double>(remaining) / section.easeOutTicks));
					const ImVec2 point(min.x + (max.x - min.x) * static_cast<float>(ratio),
						max.y - (max.y - min.y) * static_cast<float>(weight));
					if (sample > 0) draw->AddLine(previous, point, IM_COL32(246, 230, 158, 205), 1.5f);
					previous = point;
				}
			}
			draw->AddRect(min, max, selected ? IM_COL32(245, 248, 252, 255) : IM_COL32(18, 20, 23, 255), 3.0f, 0, selected ? 2.0f : 1.0f);
			if (!track.locked && !section.locked)
			{
				draw->AddRectFilled(ImVec2(min.x, min.y), ImVec2(std::min(min.x + 5.0f, max.x), max.y), IM_COL32(236, 239, 244, 150), 2.0f);
				draw->AddRectFilled(ImVec2(std::max(max.x - 5.0f, min.x), min.y), ImVec2(max.x, max.y), IM_COL32(236, 239, 244, 150), 2.0f);
			}
			draw->PushClipRect(min, max, true);
			draw->AddText(ImVec2(min.x + 6.0f, min.y + 5.0f), IM_COL32(245, 247, 250, 255), sectionLabel.c_str());
			draw->PopClipRect();
			for (std::size_t channelIndex = 0; channelIndex < section.channels.size(); ++channelIndex)
			{
				const auto& keys = section.channels[channelIndex].keys;
				const Vans::VansTimelineTick firstLocalTick = visibleStartTick - section.startTick;
				const Vans::VansTimelineTick lastLocalTick = visibleEndTick - section.startTick;
				const auto firstKey = std::lower_bound(keys.begin(), keys.end(), firstLocalTick,
					[](const auto& key, Vans::VansTimelineTick tick) { return key.tick < tick; });
				const auto lastKey = std::upper_bound(firstKey, keys.end(), lastLocalTick,
					[](Vans::VansTimelineTick tick, const auto& key) { return tick < key.tick; });
				for (auto keyIterator = firstKey; keyIterator != lastKey; ++keyIterator)
				{
					const auto& key = *keyIterator;
					const float keyX = static_cast<float>(TickToX(section.startTick + key.tick, canvasPosition.x));
					const ImVec2 keyPosition(keyX, y + rowHeight * 0.5f);
					keyCandidates.push_back({ { SelectionKind::Key, key.id, track.id, section.id, channelIndex },
						ImVec2(keyPosition.x - 5.0f, keyPosition.y - 5.0f),
						ImVec2(keyPosition.x + 5.0f, keyPosition.y + 5.0f) });
					const bool keySelected = IsSelected(key.id) ||
						(m_Selection.kind == SelectionKind::Key && m_Selection.id == key.id);
					const ImU32 keyColor = keySelected
						? IM_COL32(255, 241, 184, 255) : IM_COL32(245, 208, 92, 255);
					if (track.type == Vans::VansTimelineTrackType::EventSignal)
					{
						draw->AddLine(ImVec2(keyPosition.x, min.y), ImVec2(keyPosition.x, max.y), keyColor, 1.5f);
						const ImVec2 triangle[3]{ ImVec2(keyPosition.x, min.y),
							ImVec2(keyPosition.x + 7.0f, min.y + 4.0f), ImVec2(keyPosition.x, min.y + 8.0f) };
						draw->AddTriangleFilled(triangle[0], triangle[1], triangle[2], keyColor);
					}
					else draw->AddCircleFilled(keyPosition, keySelected ? 5.0f : 3.5f, keyColor);
					if (!clickConsumed && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
						std::abs(ImGui::GetIO().MousePos.x - keyPosition.x) <= 7.0f &&
						std::abs(ImGui::GetIO().MousePos.y - keyPosition.y) <= 8.0f)
					{
						const bool additive = ImGui::GetIO().KeyCtrl;
						Selection selection{ SelectionKind::Key, key.id, track.id, section.id, channelIndex };
						if (ImGui::GetIO().KeyShift) SelectKeyRange(std::move(selection));
						else Select(std::move(selection), additive);
						clickConsumed = true;
						canvasObjectClicked = true;
						if (!additive && !ImGui::GetIO().KeyShift && !track.locked && !section.locked && m_Edit.BeginInteraction())
							m_CanvasDrag = { CanvasDragKind::MoveKey, track.id, section.id, key.id,
								channelIndex, section.startTick, section.durationTicks, key.tick, ImGui::GetIO().MousePos.x };
					}
				}
			}

			if (!clickConsumed && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				ImGui::GetIO().MousePos.x >= min.x && ImGui::GetIO().MousePos.x <= max.x &&
				ImGui::GetIO().MousePos.y >= min.y && ImGui::GetIO().MousePos.y <= max.y)
			{
				const bool additive = ImGui::GetIO().KeyCtrl;
				Select({ SelectionKind::Section, section.id, track.id, section.id }, additive);
				clickConsumed = true;
				canvasObjectClicked = true;
				if (!additive && ImGui::GetIO().KeyAlt && !track.locked && !section.locked)
				{
					const auto begin = m_Edit.BeginInteraction();
					const auto duplicate = begin
						? m_Edit.DuplicateSection(track.id, section.id, 0)
						: begin;
					if (!duplicate) { SetError(duplicate.message); m_Edit.CancelInteraction(); }
					else
					{
						Select({ SelectionKind::Section, duplicate.objectId, track.id, duplicate.objectId });
						m_CanvasDrag = { CanvasDragKind::MoveSection, track.id, duplicate.objectId, {}, 0,
							section.startTick, section.durationTicks, 0, ImGui::GetIO().MousePos.x };
						RefreshPreview();
					}
					return;
				}
				if (!additive && !track.locked && !section.locked && m_Edit.BeginInteraction())
				{
					CanvasDragKind kind = CanvasDragKind::MoveSection;
					if (std::abs(ImGui::GetIO().MousePos.x - x0) <= 7.0f) kind = CanvasDragKind::TrimSectionStart;
					else if (std::abs(ImGui::GetIO().MousePos.x - x1) <= 7.0f) kind = CanvasDragKind::TrimSectionEnd;
					m_CanvasDrag = { kind, track.id, section.id, {}, 0, section.startTick,
						section.durationTicks, 0, ImGui::GetIO().MousePos.x };
				}
			}
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
				ImGui::GetIO().MousePos.x >= min.x && ImGui::GetIO().MousePos.x <= max.x &&
				ImGui::GetIO().MousePos.y >= min.y && ImGui::GetIO().MousePos.y <= max.y)
			{
				Select({ SelectionKind::Section, section.id, track.id, section.id }, ImGui::GetIO().KeyCtrl);
				ImGui::OpenPopup("TimelineSectionContext");
			}
		}

		if (hovered && ImGui::GetIO().MousePos.y >= y && ImGui::GetIO().MousePos.y < y + rowHeight &&
			ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Vans::VansObjectReferenceDragPayloadType))
			{
				Vans::EditorObjectHandle handle;
				const auto* descriptor = Vans::VansTimelineTrackDescriptorRegistry::Find(track.type);
				if (Vans::TryDeserializeEditorObjectHandle(payload->Data, payload->DataSize, handle) && descriptor &&
					descriptor->sectionAssetType == handle.assetType)
				{
					Vans::VansTimelineSection section;
					section.startTick = SnapTick(XToTick(ImGui::GetIO().MousePos.x, canvasPosition.x));
					section.durationTicks = std::max<Vans::VansTimelineTick>(1,
						Vans::VansTimelineTime::FrameToTick(30, m_Edit.Asset().timebase));
					section.assetGuid = handle.guid;
					section.assetPath = handle.path;
					const auto result = m_Edit.AddSection(track.id, std::move(section));
					if (!result) SetError(result.message);
					else Select({ SelectionKind::Section, result.objectId, track.id, result.objectId });
				}
			}
			ImGui::EndDragDropTarget();
		}
		y += rowHeight;
	}
	if (canvasLeftClicked && !canvasObjectClicked && m_CanvasDrag.kind == CanvasDragKind::None)
	{
		m_CanvasMarquee.active = true;
		m_CanvasMarquee.additive = ImGui::GetIO().KeyCtrl;
		m_CanvasMarquee.originX = m_CanvasMarquee.currentX = ImGui::GetIO().MousePos.x;
		m_CanvasMarquee.originY = m_CanvasMarquee.currentY = ImGui::GetIO().MousePos.y;
		if (!m_CanvasMarquee.additive) m_SelectedIds.clear();
	}
	if (m_CanvasMarquee.active)
	{
		m_CanvasMarquee.currentX = ImGui::GetIO().MousePos.x;
		m_CanvasMarquee.currentY = ImGui::GetIO().MousePos.y;
		const ImVec2 minimum(
			std::min(m_CanvasMarquee.originX, m_CanvasMarquee.currentX),
			std::min(m_CanvasMarquee.originY, m_CanvasMarquee.currentY));
		const ImVec2 maximum(
			std::max(m_CanvasMarquee.originX, m_CanvasMarquee.currentX),
			std::max(m_CanvasMarquee.originY, m_CanvasMarquee.currentY));
		draw->AddRectFilled(minimum, maximum, IM_COL32(67, 142, 185, 42));
		draw->AddRect(minimum, maximum, IM_COL32(105, 191, 239, 220));
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			const auto intersects = [&](const CanvasSelectionCandidate& candidate)
			{
				return candidate.maximum.x >= minimum.x && candidate.minimum.x <= maximum.x &&
					candidate.maximum.y >= minimum.y && candidate.minimum.y <= maximum.y;
			};
			std::vector<const CanvasSelectionCandidate*> matches;
			for (const auto& candidate : keyCandidates) if (intersects(candidate)) matches.push_back(&candidate);
			if (matches.empty())
				for (const auto& candidate : sectionCandidates) if (intersects(candidate)) matches.push_back(&candidate);
			if (!m_CanvasMarquee.additive) m_SelectedIds.clear();
			for (const CanvasSelectionCandidate* candidate : matches)
			{
				m_SelectedIds.insert(candidate->selection.id);
				m_Selection = candidate->selection;
			}
			if (matches.empty() && !m_CanvasMarquee.additive) m_Selection = {};
			m_CanvasMarquee = {};
		}
	}
	if (ImGui::BeginPopup("TimelineSectionContext"))
	{
		if (ImGui::MenuItem("Split at Playhead")) SplitSelection();
		if (ImGui::MenuItem("Duplicate")) DuplicateSelection();
		if (ImGui::MenuItem("Copy")) CopySelection();
		ImGui::BeginDisabled(!m_SectionClipboard.has_value());
		if (ImGui::MenuItem("Paste at Playhead")) PasteSelection();
		ImGui::EndDisabled();
		ImGui::Separator();
		if (ImGui::MenuItem("Delete")) DeleteSelection();
		ImGui::EndPopup();
	}
	const float playheadX = static_cast<float>(TickToX(m_Playhead, canvasPosition.x));
	draw->AddLine(ImVec2(playheadX, canvasPosition.y), ImVec2(playheadX, canvasPosition.y + canvasSize.y), IM_COL32(238, 92, 82, 255), 2.0f);
	if (m_HasSnapTarget)
	{
		const float snapX = static_cast<float>(TickToX(m_LastSnapTarget, canvasPosition.x));
		draw->AddLine(ImVec2(snapX, canvasPosition.y), ImVec2(snapX, canvasPosition.y + canvasSize.y),
			IM_COL32(245, 208, 92, 230), 1.5f);
		const std::string snapLabel = FormatTick(m_LastSnapTarget);
		draw->AddText(ImVec2(snapX + 5.0f, canvasPosition.y + rulerHeight + 3.0f),
			IM_COL32(245, 222, 132, 255), snapLabel.c_str());
	}
}

bool VansTimelineEditorWindow::DrawSerializedValue(
	const std::string& label,
	Vans::VansSerializedValue& value,
	int depth,
	Vans::VansSerializedValue* parent)
{
	if (depth > 8) { ImGui::TextDisabled("%s", label.c_str()); return false; }
	ImGui::PushID(&value);
	bool changed = false;
	switch (value.kind)
	{
	case Vans::VansSerializedValue::Kind::Bool:
		changed = ImGui::Checkbox(label.c_str(), &value.boolValue); break;
	case Vans::VansSerializedValue::Kind::Int:
		changed = ImGui::DragScalar(label.c_str(), ImGuiDataType_S64, &value.intValue, 1.0f); break;
	case Vans::VansSerializedValue::Kind::Float:
		changed = ImGui::DragScalar(label.c_str(), ImGuiDataType_Double, &value.floatValue, 0.01f); break;
	case Vans::VansSerializedValue::Kind::String:
	{
		if (label == "id" || label == "type" || label == "assetKind")
			ImGui::TextDisabled("%s: %s", label.c_str(), value.stringValue.c_str());
		else if (label == "assetPath" || label == "avatarMaskPath" || label == "profilePath" ||
			label == "scenePath" || label == "spawnTemplatePath")
			ImGui::TextDisabled("%s: %s", label.c_str(), value.stringValue.empty() ? "<indexed asset>" : value.stringValue.c_str());
		else if (parent && InspectorPathField(label))
		{
			const Vans::VansTimelineTrack* track = SelectedTrack();
			const auto* descriptor = track ? Vans::VansTimelineTrackDescriptorRegistry::Find(track->type) : nullptr;
			const auto expectedType = InspectorAssetType(label, descriptor);
			const char* pathField = InspectorPathField(label);
			const Vans::VansSerializedValue* path = pathField ? Field(*parent, pathField) : nullptr;
			ImGui::TextUnformatted(label.c_str());
			std::string display = path && !path->stringValue.empty() ? path->stringValue : value.stringValue;
			if (display.empty()) display = "Drop project asset";
			if (ImGui::Button((display + "##TimelineAssetSlot").c_str(), ImVec2(-32.0f, 0.0f))) {}
			if (ImGui::IsItemHovered() && !value.stringValue.empty())
				ImGui::SetTooltip("GUID: %s\nPath: %s", value.stringValue.c_str(),
					path ? path->stringValue.c_str() : "");
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Vans::VansObjectReferenceDragPayloadType))
				{
					Vans::EditorObjectHandle handle;
					if (Vans::TryDeserializeEditorObjectHandle(payload->Data, payload->DataSize, handle) &&
						handle.domain == Vans::EditorObjectDomain::ProjectAsset &&
						(expectedType == Vans::EditorAPI::AssetType::Unknown || handle.assetType == expectedType))
					{
						value.stringValue = handle.guid;
						if (Vans::VansSerializedValue* pathValue = pathField ? Field(*parent, pathField) : nullptr)
							pathValue->stringValue = handle.path;
						changed = true;
					}
					else SetError("Dropped asset is incompatible with this Timeline slot");
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("X"))
			{
				value.stringValue.clear();
				if (Vans::VansSerializedValue* pathValue = pathField ? Field(*parent, pathField) : nullptr)
					pathValue->stringValue.clear();
				changed = true;
			}
		}
		else
		{
			const Vans::VansTimelineTrack* track = SelectedTrack();
			const auto* options = InspectorEnumOptions(label,
				track ? track->type : Vans::VansTimelineTrackType::Custom);
			if (options)
			{
				if (ImGui::BeginCombo(label.c_str(), value.stringValue.c_str()))
				{
					for (const std::string& option : *options)
					{
						const bool selected = value.stringValue == option;
						if (ImGui::Selectable(option.c_str(), selected)) { value.stringValue = option; changed = true; }
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
			else
			{
				char buffer[512]{}; std::strncpy(buffer, value.stringValue.c_str(), sizeof(buffer) - 1);
				if (ImGui::InputText(label.c_str(), buffer, sizeof(buffer))) { value.stringValue = buffer; changed = true; }
			}
		}
		break;
	}
	case Vans::VansSerializedValue::Kind::Array:
	{
		const bool numericVector = value.arrayItems.size() >= 2 && value.arrayItems.size() <= 4 &&
			std::all_of(value.arrayItems.begin(), value.arrayItems.end(), [](const auto& item)
			{
				return item.kind == Vans::VansSerializedValue::Kind::Float ||
					item.kind == Vans::VansSerializedValue::Kind::Int;
			});
		if (numericVector)
		{
			std::array<double, 4> components{};
			for (std::size_t index = 0; index < value.arrayItems.size(); ++index)
				components[index] = value.arrayItems[index].kind == Vans::VansSerializedValue::Kind::Float
					? value.arrayItems[index].floatValue : static_cast<double>(value.arrayItems[index].intValue);
			const bool isColor = Lower(label).find("color") != std::string::npos;
			if (isColor && value.arrayItems.size() == 4)
			{
				float color[4]{ static_cast<float>(components[0]), static_cast<float>(components[1]),
					static_cast<float>(components[2]), static_cast<float>(components[3]) };
				if (ImGui::ColorEdit4(label.c_str(), color, ImGuiColorEditFlags_Float))
				{
					for (std::size_t index = 0; index < 4; ++index)
					{
						value.arrayItems[index].kind = Vans::VansSerializedValue::Kind::Float;
						value.arrayItems[index].floatValue = color[index];
					}
					changed = true;
				}
			}
			else if (ImGui::DragScalarN(label.c_str(), ImGuiDataType_Double, components.data(),
				static_cast<int>(value.arrayItems.size()), 0.01f))
			{
				for (std::size_t index = 0; index < value.arrayItems.size(); ++index)
				{
					value.arrayItems[index].kind = Vans::VansSerializedValue::Kind::Float;
					value.arrayItems[index].floatValue = components[index];
				}
				changed = true;
			}
		}
		else if (ImGui::TreeNode(label.c_str()))
		{
			for (std::size_t index = 0; index < value.arrayItems.size(); ++index)
				changed = DrawSerializedValue(std::to_string(index), value.arrayItems[index], depth + 1, &value) || changed;
			ImGui::TreePop();
		}
		break;
	}
	case Vans::VansSerializedValue::Kind::Object:
		if (label.empty() || ImGui::TreeNode(label.c_str()))
		{
			for (auto& [name, child] : value.objectFields)
				changed = DrawSerializedValue(name, child, depth + 1, &value) || changed;
			if (!label.empty()) ImGui::TreePop();
		}
		break;
	default: ImGui::TextDisabled("%s", label.c_str()); break;
	}
	ImGui::PopID();
	return changed;
}

void VansTimelineEditorWindow::ApplySerializedEdit(Vans::VansSerializedValue root)
{
	Vans::VansTimelineAsset decoded;
	std::string error;
	if (!Vans::VansTimelineSerialization::DecodeSerialized(root, decoded, error)) { SetError(std::move(error)); return; }
	if (!m_Edit.IsInteracting())
	{
		const auto result = m_Edit.BeginInteraction();
		if (!result) { SetError(result.message); return; }
	}
	m_Edit.PreviewAsset() = std::move(decoded);
	m_InteractionPending = true;
}

void VansTimelineEditorWindow::CommitPendingInteraction()
{
	if (!m_InteractionPending || ImGui::IsAnyItemActive()) return;
	const auto result = m_Edit.CommitInteraction();
	if (!result) { SetError(result.message); m_Edit.CancelInteraction(); }
	else { m_LastError.clear(); RefreshPreview(); }
	m_InteractionPending = false;
}

void VansTimelineEditorWindow::DrawInspector()
{
	ImGui::TextUnformatted("Inspector");
	ImGui::Separator();
	const Vans::VansTimelineTrack* selectedTrack = SelectedTrack();
	if (selectedTrack)
	{
		if (const auto* descriptor = Vans::VansTimelineTrackDescriptorRegistry::Find(selectedTrack->type))
		{
			ImGui::TextDisabled("%s / %s", descriptor->category.c_str(), descriptor->displayName.c_str());
			if (descriptor->capability != Vans::VansTimelineEditorCapabilityLevel::Full)
				ImGui::TextColored(ImVec4(0.96f, 0.72f, 0.28f, 1.0f), "Runtime capability is not registered");
		}
	}
	bool hasRelevantDiagnostics = false;
	for (const auto& diagnostic : m_Edit.Diagnostics())
	{
		const bool relevant = m_Selection.kind == SelectionKind::Asset || diagnostic.objectId == m_Selection.id ||
			diagnostic.objectId == m_Selection.trackId || diagnostic.objectId == m_Selection.sectionId;
		if (!relevant) continue;
		hasRelevantDiagnostics = true;
		const ImVec4 color = diagnostic.severity == Vans::VansTimelineDiagnosticSeverity::Error
			? ImVec4(1.0f, 0.36f, 0.31f, 1.0f)
			: diagnostic.severity == Vans::VansTimelineDiagnosticSeverity::Warning
				? ImVec4(0.96f, 0.72f, 0.28f, 1.0f) : ImVec4(0.58f, 0.75f, 0.92f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextWrapped("%s: %s", diagnostic.propertyPath.c_str(), diagnostic.message.c_str());
		ImGui::PopStyleColor();
	}
	if (hasRelevantDiagnostics) ImGui::Separator();
	Vans::VansSerializedValue root = Vans::VansTimelineSerialization::EncodeSerialized(m_Edit.Asset());
	Vans::VansSerializedValue* selected = &root;
	if (m_Selection.kind == SelectionKind::Binding)
	{
		if (auto* values = Field(root, "bindings"))
			for (auto& value : values->arrayItems) if (auto* id = Field(value, "id"); id && id->stringValue == m_Selection.id) selected = &value;
	}
	else if (m_Selection.kind == SelectionKind::Group)
	{
		if (auto* values = Field(root, "groups"))
			for (auto& value : values->arrayItems)
				if (auto* id = Field(value, "id"); id && id->stringValue == m_Selection.id) selected = &value;
	}
	else if (m_Selection.kind == SelectionKind::Marker)
	{
		if (auto* values = Field(root, "markers"))
			for (auto& value : values->arrayItems)
				if (auto* id = Field(value, "id"); id && id->stringValue == m_Selection.id) selected = &value;
	}
	else if (m_Selection.kind == SelectionKind::Track || m_Selection.kind == SelectionKind::Section ||
		m_Selection.kind == SelectionKind::Channel || m_Selection.kind == SelectionKind::Key)
	{
		if (auto* tracks = Field(root, "tracks"))
			for (auto& track : tracks->arrayItems)
			{
				auto* id = Field(track, "id"); if (!id || id->stringValue != m_Selection.trackId) continue;
				selected = &track;
				if (m_Selection.kind == SelectionKind::Track) break;
				if (auto* sections = Field(track, "sections")) for (auto& section : sections->arrayItems)
				{
					auto* sectionId = Field(section, "id"); if (!sectionId || sectionId->stringValue != m_Selection.sectionId) continue;
					selected = &section;
					if (m_Selection.kind == SelectionKind::Section) break;
					if (auto* channels = Field(section, "channels"); channels &&
						m_Selection.channelIndex < channels->arrayItems.size())
					{
						auto& channel = channels->arrayItems[m_Selection.channelIndex];
						selected = &channel;
						if (m_Selection.kind == SelectionKind::Key)
							if (auto* keys = Field(channel, "keys")) for (auto& key : keys->arrayItems)
								if (auto* keyId = Field(key, "id"); keyId && keyId->stringValue == m_Selection.id) selected = &key;
					}
				}
			}
	}
	if (DrawSerializedValue({}, *selected)) ApplySerializedEdit(std::move(root));
	if (m_Selection.kind != SelectionKind::Asset && ImGui::Button("Delete"))
		DeleteSelection();
}

void VansTimelineEditorWindow::DrawCurveAndDopeSheet()
{
	Vans::VansTimelineSection* section = SelectedSection();
	if (!section) { ImGui::TextDisabled("Select a section to edit channels and keys"); return; }
	if (ImGui::BeginTabBar("TimelineCurves"))
	{
		if (ImGui::BeginTabItem("Dope Sheet"))
		{
			for (std::size_t channelIndex = 0; channelIndex < section->channels.size(); ++channelIndex)
			{
				auto& channel = section->channels[channelIndex];
				ImGui::PushID(channel.id.c_str());
				const bool channelOpen = ImGui::TreeNodeEx(channel.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
					(IsSelected(channel.id) ? ImGuiTreeNodeFlags_Selected : 0));
				if (ImGui::IsItemClicked())
					Select({ SelectionKind::Channel, channel.id, m_Selection.trackId, section->id, channelIndex },
						ImGui::GetIO().KeyCtrl);
				if (channelOpen)
				{
					ImGui::SameLine();
					if (ImGui::SmallButton("+ Key"))
					{
						Vans::VansTimelineKey key;
						key.tick = std::clamp(m_Playhead - section->startTick, Vans::VansTimelineTick{ 0 }, section->durationTicks - 1);
						key.value = DefaultKeyValue(channel.type);
						key.interpolation = channel.type == Vans::VansTimelineChannelType::Bool ||
							channel.type == Vans::VansTimelineChannelType::String ||
							channel.type == Vans::VansTimelineChannelType::EventPayload
							? Vans::VansTimelineInterpolation::Constant : Vans::VansTimelineInterpolation::Auto;
						const auto result = m_Edit.AddKey(m_Selection.trackId, section->id, channelIndex, std::move(key));
						if (!result) SetError(result.message);
						else Select({ SelectionKind::Key, result.objectId, m_Selection.trackId, section->id, channelIndex });
					}
					const Vans::VansTimelineTick localTick = std::clamp(m_Playhead - section->startTick,
						Vans::VansTimelineTick{ 0 }, std::max<Vans::VansTimelineTick>(0, section->durationTicks - 1));
					auto exact = std::find_if(channel.keys.begin(), channel.keys.end(),
						[&](const auto& key) { return key.tick == localTick; });
					Vans::VansTimelineKeyValue currentValue = DefaultKeyValue(channel.type);
					if (exact != channel.keys.end()) currentValue = exact->value;
					else
					{
						const Vans::VansTimelineKey* previous = nullptr;
						for (const auto& key : channel.keys)
							if (key.tick <= localTick && (!previous || key.tick > previous->tick)) previous = &key;
						if (previous) currentValue = previous->value;
					}
					const bool canAutoKey = m_AutoKeyMode == AutoKeyMode::KeyAllAllowed ||
						(m_AutoKeyMode == AutoKeyMode::KeyExisting && exact != channel.keys.end());
					ImGui::BeginDisabled(!canAutoKey);
					if (DrawTimelineKeyValue("Value at Playhead", currentValue))
					{
						Vans::TimelineEditResult result;
						if (exact != channel.keys.end())
							result = m_Edit.SetKeyValue(m_Selection.trackId, section->id, channelIndex,
								exact->id, std::move(currentValue));
						else
						{
							Vans::VansTimelineKey key;
							key.tick = localTick;
							key.value = std::move(currentValue);
							key.interpolation = channel.type == Vans::VansTimelineChannelType::Bool ||
								channel.type == Vans::VansTimelineChannelType::String ||
								channel.type == Vans::VansTimelineChannelType::EventPayload
								? Vans::VansTimelineInterpolation::Constant : Vans::VansTimelineInterpolation::Auto;
							result = m_Edit.AddKey(m_Selection.trackId, section->id, channelIndex, std::move(key));
						}
						if (!result) SetError(result.message);
						else { Select({ SelectionKind::Key, result.objectId, m_Selection.trackId, section->id, channelIndex }); RefreshPreview(); }
					}
					ImGui::EndDisabled();
					if (!canAutoKey && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
						ImGui::SetTooltip(m_AutoKeyMode == AutoKeyMode::Off
							? "Enable Auto Key to edit the channel at the playhead"
							: "Key Existing only edits a key already present at this time");
					for (const auto& key : channel.keys)
					{
						ImGui::PushID(key.id.c_str());
						const bool selected = m_Selection.kind == SelectionKind::Key && m_Selection.id == key.id;
						if (ImGui::Selectable(("Tick " + std::to_string(key.tick)).c_str(), selected))
						{
							Selection selection{ SelectionKind::Key, key.id, m_Selection.trackId, section->id, channelIndex };
							if (ImGui::GetIO().KeyShift) SelectKeyRange(std::move(selection));
							else Select(std::move(selection), ImGui::GetIO().KeyCtrl);
						}
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Curve Editor"))
		{
			Vans::VansTimelineChannel* selectedChannel =
				(m_Selection.kind == SelectionKind::Channel || m_Selection.kind == SelectionKind::Key)
				? SelectedChannel() : nullptr;
			ImGui::BeginDisabled(selectedChannel == nullptr);
			if (ImGui::SmallButton("Buffer Curve") && selectedChannel)
			{
				m_CurveBuffer = CurveBuffer{ m_Selection.trackId, section->id, selectedChannel->id,
					m_Selection.channelIndex, selectedChannel->keys };
				m_ShowCurveComparison = true;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			const bool canRestoreBuffer = m_CurveBuffer && selectedChannel &&
				m_CurveBuffer->trackId == m_Selection.trackId &&
				m_CurveBuffer->sectionId == section->id &&
				m_CurveBuffer->channelId == selectedChannel->id;
			ImGui::BeginDisabled(!canRestoreBuffer);
			if (ImGui::SmallButton("Restore Buffer") && canRestoreBuffer)
			{
				const auto result = m_Edit.ReplaceChannelKeys(m_CurveBuffer->trackId,
					m_CurveBuffer->sectionId, m_CurveBuffer->channelIndex, m_CurveBuffer->keys);
				if (!result) SetError(result.message);
				else RefreshPreview();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!canRestoreBuffer);
			ImGui::Checkbox("Compare", &m_ShowCurveComparison);
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::SmallButton("Fit Value"))
			{
				m_CurveValueZoom = 1.0;
				m_CurveValuePan = 0.0;
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(78.0f);
			ImGui::DragScalar("Time Scale", ImGuiDataType_Double, &m_KeyTimeScale, 0.01f,
				nullptr, nullptr, "%.2f");
			m_KeyTimeScale = std::max(0.01, m_KeyTimeScale);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(78.0f);
			ImGui::DragScalar("Value Scale", ImGuiDataType_Double, &m_KeyValueScale, 0.01f,
				nullptr, nullptr, "%.2f");
			ImGui::SameLine();
			ImGui::BeginDisabled(m_SelectedIds.empty());
			if (ImGui::SmallButton("Apply Scale"))
			{
				const auto result = m_Edit.ScaleKeys(m_SelectedIds, m_KeyTimeScale, m_KeyValueScale);
				if (!result) SetError(result.message);
				else RefreshPreview();
			}
			ImGui::EndDisabled();
			static constexpr std::array<std::pair<const char*, Vans::VansTimelineInterpolation>, 7> Interpolations{{
				{ "Constant", Vans::VansTimelineInterpolation::Constant },
				{ "Linear", Vans::VansTimelineInterpolation::Linear },
				{ "Auto", Vans::VansTimelineInterpolation::Auto },
				{ "Clamped Auto", Vans::VansTimelineInterpolation::ClampedAuto },
				{ "Cubic", Vans::VansTimelineInterpolation::Cubic },
				{ "Bezier", Vans::VansTimelineInterpolation::Bezier },
				{ "Slerp", Vans::VansTimelineInterpolation::Slerp }
			}};
			static constexpr std::array<std::pair<const char*, Vans::VansTimelineTangentMode>, 3> TangentModes{{
				{ "Unified", Vans::VansTimelineTangentMode::Unified },
				{ "Broken", Vans::VansTimelineTangentMode::Broken },
				{ "Weighted", Vans::VansTimelineTangentMode::Weighted }
			}};
			const auto interpolationName = std::find_if(Interpolations.begin(), Interpolations.end(),
				[&](const auto& item) { return item.second == m_BatchInterpolation; });
			const auto tangentName = std::find_if(TangentModes.begin(), TangentModes.end(),
				[&](const auto& item) { return item.second == m_BatchTangentMode; });
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::BeginCombo("Interpolation", interpolationName->first))
			{
				for (const auto& [name, interpolation] : Interpolations)
					if (ImGui::Selectable(name, interpolation == m_BatchInterpolation))
						m_BatchInterpolation = interpolation;
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100.0f);
			if (ImGui::BeginCombo("Tangent", tangentName->first))
			{
				for (const auto& [name, tangentMode] : TangentModes)
					if (ImGui::Selectable(name, tangentMode == m_BatchTangentMode))
						m_BatchTangentMode = tangentMode;
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(m_SelectedIds.empty());
			if (ImGui::SmallButton("Apply Curve"))
			{
				const auto result = m_Edit.SetKeysCurveMode(
					m_SelectedIds, m_BatchInterpolation, m_BatchTangentMode);
				if (!result) SetError(result.message);
				else RefreshPreview();
			}
			ImGui::EndDisabled();

			const ImVec2 p = ImGui::GetCursorScreenPos();
			const ImVec2 size = ImGui::GetContentRegionAvail();
			ImGui::InvisibleButton("TimelineCurveCanvas", size,
				ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
			const bool hovered = ImGui::IsItemHovered();
			const bool curveLeftClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(23, 25, 29, 255));
			const auto curveTickToX = [&](Vans::VansTimelineTick localTick)
			{
				return p.x + static_cast<float>((section->startTick + localTick - m_ViewStart) * m_PixelsPerTick);
			};
			const auto curveXToTick = [&](float x)
			{
				return static_cast<Vans::VansTimelineTick>(std::llround(
					m_ViewStart + (x - p.x) / m_PixelsPerTick - section->startTick));
			};
			for (int line = 1; line < 4; ++line)
			{
				const float y = p.y + size.y * static_cast<float>(line) / 4.0f;
				draw->AddLine(ImVec2(p.x, y), ImVec2(p.x + size.x, y), IM_COL32(64, 68, 75, 130));
			}
			double minimum = std::numeric_limits<double>::max();
			double maximum = std::numeric_limits<double>::lowest();
			for (const auto& channel : section->channels) for (const auto& key : channel.keys)
			{
				double value = 0.0;
				if (NumericKeyValue(key.value, value)) { minimum = std::min(minimum, value); maximum = std::max(maximum, value); }
			}
			if (canRestoreBuffer && m_ShowCurveComparison)
				for (const auto& key : m_CurveBuffer->keys)
				{
					double value = 0.0;
					if (NumericKeyValue(key.value, value)) { minimum = std::min(minimum, value); maximum = std::max(maximum, value); }
				}
			if (minimum == std::numeric_limits<double>::max()) { minimum = 0.0; maximum = 1.0; }
			if (std::abs(maximum - minimum) < 1e-9) { minimum -= 0.5; maximum += 0.5; }
			if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
			{
				if (ImGui::GetIO().KeyAlt)
					m_CurveValueZoom = std::clamp(m_CurveValueZoom *
						(ImGui::GetIO().MouseWheel > 0.0f ? 1.2 : 0.8), 0.1, 100.0);
				else if (ImGui::GetIO().KeyCtrl)
				{
					const auto anchor = m_ViewStart + static_cast<Vans::VansTimelineTick>(std::llround(
						(ImGui::GetIO().MousePos.x - p.x) / m_PixelsPerTick));
					m_PixelsPerTick = std::clamp(m_PixelsPerTick *
						(ImGui::GetIO().MouseWheel > 0.0f ? 1.2 : 0.8), 0.0002, 8.0);
					m_ViewStart = anchor - static_cast<Vans::VansTimelineTick>(std::llround(
						(ImGui::GetIO().MousePos.x - p.x) / m_PixelsPerTick));
				}
				else if (ImGui::GetIO().KeyShift)
					m_ViewStart -= static_cast<Vans::VansTimelineTick>(std::llround(
						ImGui::GetIO().MouseWheel * 96.0 / m_PixelsPerTick));
			}
			const double automaticSpan = maximum - minimum;
			if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
			{
				m_ViewStart -= static_cast<Vans::VansTimelineTick>(std::llround(
					ImGui::GetIO().MouseDelta.x / m_PixelsPerTick));
				m_CurveValuePan += ImGui::GetIO().MouseDelta.y /
					std::max(1.0f, size.y) * (automaticSpan / m_CurveValueZoom);
			}
			const double valueCenter = (minimum + maximum) * 0.5 + m_CurveValuePan;
			const double displaySpan = automaticSpan / m_CurveValueZoom;
			minimum = valueCenter - displaySpan * 0.5;
			maximum = valueCenter + displaySpan * 0.5;
			const float playheadX = curveTickToX(m_Playhead - section->startTick);
			if (playheadX >= p.x && playheadX <= p.x + size.x)
				draw->AddLine(ImVec2(playheadX, p.y), ImVec2(playheadX, p.y + size.y),
					IM_COL32(236, 92, 84, 225), 1.5f);
			if (canRestoreBuffer && m_ShowCurveComparison)
			{
				ImVec2 previous{};
				bool hasPrevious = false;
				const std::size_t stride = std::max<std::size_t>(1, m_CurveBuffer->keys.size() / 2000);
				for (std::size_t keyIndex = 0; keyIndex < m_CurveBuffer->keys.size(); keyIndex += stride)
				{
					const auto& key = m_CurveBuffer->keys[keyIndex];
					double value = 0.0;
					if (!NumericKeyValue(key.value, value)) continue;
					const ImVec2 point(
						curveTickToX(key.tick),
						p.y + size.y - static_cast<float>((value - minimum) / (maximum - minimum)) * size.y);
					if (hasPrevious) draw->AddLine(previous, point, IM_COL32(165, 169, 178, 190), 1.5f);
					previous = point;
					hasPrevious = true;
				}
			}
			if (m_CurveDrag.kind != CurveDragKind::None)
			{
				if (m_CommandMap.IsTriggered(Vans::VansTimelineCommand::CancelInteraction))
				{
					m_Edit.CancelInteraction();
					m_CurveDrag = {};
					RefreshPreview();
				}
				else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					Vans::VansTimelineSection* draggedSection = SelectedSection();
					Vans::VansTimelineChannel* draggedChannel = draggedSection &&
						m_CurveDrag.channelIndex < draggedSection->channels.size()
						? &draggedSection->channels[m_CurveDrag.channelIndex] : nullptr;
					auto draggedKey = draggedChannel ? std::find_if(draggedChannel->keys.begin(), draggedChannel->keys.end(),
						[&](const auto& key) { return key.id == m_CurveDrag.keyId; }) : decltype(draggedChannel->keys.begin()){};
					if (!draggedChannel || draggedKey == draggedChannel->keys.end())
					{
						m_Edit.CancelInteraction();
						m_CurveDrag = {};
					}
					else if (m_CurveDrag.kind == CurveDragKind::MoveKey)
					{
						const Vans::VansTimelineTick tick = std::clamp<Vans::VansTimelineTick>(
							curveXToTick(ImGui::GetIO().MousePos.x),
							0, std::max<Vans::VansTimelineTick>(0, draggedSection->durationTicks - 1));
						const double number = maximum - std::clamp(
							static_cast<double>((ImGui::GetIO().MousePos.y - p.y) / std::max(1.0f, size.y)), 0.0, 1.0) *
							(maximum - minimum);
						double currentNumber = 0.0;
						NumericKeyValue(draggedKey->value, currentNumber);
						const bool multiKeyMove = m_SelectedIds.size() > 1 &&
							m_SelectedIds.find(m_CurveDrag.keyId) != m_SelectedIds.end();
						Vans::TimelineEditResult result;
						if (multiKeyMove)
						{
							result = m_Edit.MoveKeysBy(m_SelectedIds, tick - draggedKey->tick);
							if (result) result = m_Edit.OffsetKeyValuesBy(m_SelectedIds, number - currentNumber);
						}
						else
						{
							Vans::VansTimelineKeyValue value = draggedKey->value;
							result = m_Edit.MoveKey(m_CurveDrag.trackId, m_CurveDrag.sectionId,
								m_CurveDrag.channelIndex, m_CurveDrag.keyId, tick);
							if (result && SetNumericKeyValue(value, number))
								result = m_Edit.SetKeyValue(m_CurveDrag.trackId, m_CurveDrag.sectionId,
									m_CurveDrag.channelIndex, m_CurveDrag.keyId, std::move(value));
						}
						if (!result) { SetError(result.message); m_Edit.CancelInteraction(); m_CurveDrag = {}; }
						else RefreshPreview();
					}
					else
					{
						double arrive = draggedKey->arriveTangent;
						double leave = draggedKey->leaveTangent;
						double arriveWeight = draggedKey->arriveWeight;
						double leaveWeight = draggedKey->leaveWeight;
						const float pointX = curveTickToX(draggedKey->tick);
						double currentNumber = 0.0;
						NumericKeyValue(draggedKey->value, currentNumber);
						const float pointY = p.y + size.y - static_cast<float>((currentNumber - minimum) /
							(maximum - minimum)) * size.y;
						const bool arriveHandle = m_CurveDrag.kind == CurveDragKind::ArriveTangent;
						const double tangent = arriveHandle
							? (ImGui::GetIO().MousePos.y - pointY) / 16.0
							: (pointY - ImGui::GetIO().MousePos.y) / 16.0;
						if (arriveHandle) arrive = tangent; else leave = tangent;
						if (draggedKey->tangentMode == Vans::VansTimelineTangentMode::Unified) arrive = leave = tangent;
						if (draggedKey->tangentMode == Vans::VansTimelineTangentMode::Weighted)
						{
							const double weight = std::clamp(std::abs(ImGui::GetIO().MousePos.x - pointX) / 42.0, 0.1, 8.0);
							if (arriveHandle) arriveWeight = weight; else leaveWeight = weight;
						}
						const auto interpolation = draggedKey->interpolation == Vans::VansTimelineInterpolation::Auto ||
							draggedKey->interpolation == Vans::VansTimelineInterpolation::ClampedAuto
							? Vans::VansTimelineInterpolation::Cubic : draggedKey->interpolation;
						const auto result = m_Edit.SetKeyCurve(m_CurveDrag.trackId, m_CurveDrag.sectionId,
							m_CurveDrag.channelIndex, m_CurveDrag.keyId, interpolation, draggedKey->tangentMode,
							arrive, leave, arriveWeight, leaveWeight);
						if (!result) { SetError(result.message); m_Edit.CancelInteraction(); m_CurveDrag = {}; }
						else RefreshPreview();
					}
				}
				else
				{
					const auto result = m_Edit.CommitInteraction();
					if (!result) { SetError(result.message); m_Edit.CancelInteraction(); }
					else m_LastError.clear();
					m_CurveDrag = {};
					RefreshPreview();
				}
			}
			struct CurveSelectionCandidate
			{
				Selection selection;
				ImVec2 point;
			};
			std::vector<CurveSelectionCandidate> curveCandidates;
			bool clickConsumed = false;
			for (std::size_t channelIndex = 0; channelIndex < section->channels.size(); ++channelIndex)
			{
				const auto& channel = section->channels[channelIndex];
				ImVec2 previous{}; bool hasPrevious = false;
				const std::size_t stride = std::max<std::size_t>(1, channel.keys.size() / 2000);
				for (std::size_t keyIndex = 0; keyIndex < channel.keys.size(); ++keyIndex)
				{
					const auto& key = channel.keys[keyIndex];
					const bool explicitlySelected = IsSelected(key.id) ||
						(m_Selection.kind == SelectionKind::Key && m_Selection.id == key.id);
					if (keyIndex % stride != 0 && keyIndex + 1 != channel.keys.size() && !explicitlySelected) continue;
					double value = 0.0; if (!NumericKeyValue(key.value, value)) continue;
					const float x = curveTickToX(key.tick);
					const float y = p.y + size.y - static_cast<float>((value - minimum) / (maximum - minimum)) * size.y;
					const ImVec2 point(x, y);
					curveCandidates.push_back({
						{ SelectionKind::Key, key.id, m_Selection.trackId, section->id, channelIndex }, point });
					if (hasPrevious) draw->AddLine(previous, point, IM_COL32(89, 183, 235, 255), 2.0f);
					const bool selected = explicitlySelected;
					draw->AddCircleFilled(point, selected ? 5.0f : 3.5f,
						selected ? IM_COL32(255, 239, 170, 255) : IM_COL32(131, 205, 244, 255));
					ImVec2 arrive{};
					ImVec2 leave{};
					bool hasTangents = false;
					if (selected && (key.interpolation == Vans::VansTimelineInterpolation::Cubic ||
						key.interpolation == Vans::VansTimelineInterpolation::Bezier ||
						key.interpolation == Vans::VansTimelineInterpolation::Auto ||
						key.interpolation == Vans::VansTimelineInterpolation::ClampedAuto))
					{
						const float arriveLength = key.tangentMode == Vans::VansTimelineTangentMode::Weighted && key.arriveWeight > 0.0
							? static_cast<float>(std::clamp(key.arriveWeight * 42.0, 12.0, 120.0)) : 42.0f;
						const float leaveLength = key.tangentMode == Vans::VansTimelineTangentMode::Weighted && key.leaveWeight > 0.0
							? static_cast<float>(std::clamp(key.leaveWeight * 42.0, 12.0, 120.0)) : 42.0f;
						arrive = ImVec2(point.x - arriveLength, point.y + static_cast<float>(key.arriveTangent * 16.0));
						leave = ImVec2(point.x + leaveLength, point.y - static_cast<float>(key.leaveTangent * 16.0));
						hasTangents = true;
						draw->AddLine(arrive, leave, IM_COL32(245, 208, 92, 210));
						draw->AddCircleFilled(arrive, 3.0f, IM_COL32(245, 208, 92, 255));
						draw->AddCircleFilled(leave, 3.0f, IM_COL32(245, 208, 92, 255));
					}
					if (!clickConsumed && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						auto hit = [&](const ImVec2& candidate, float radius)
						{
							const float dx = ImGui::GetIO().MousePos.x - candidate.x;
							const float dy = ImGui::GetIO().MousePos.y - candidate.y;
							return dx * dx + dy * dy <= radius * radius;
						};
						CurveDragKind dragKind = CurveDragKind::None;
						if (hasTangents && hit(arrive, 7.0f)) dragKind = CurveDragKind::ArriveTangent;
						else if (hasTangents && hit(leave, 7.0f)) dragKind = CurveDragKind::LeaveTangent;
						else if (hit(point, 8.0f)) dragKind = CurveDragKind::MoveKey;
						if (dragKind != CurveDragKind::None)
						{
							const bool additive = ImGui::GetIO().KeyCtrl;
							Selection selection{ SelectionKind::Key, key.id, m_Selection.trackId, section->id, channelIndex };
							if (ImGui::GetIO().KeyShift) SelectKeyRange(std::move(selection));
							else Select(std::move(selection), additive);
							clickConsumed = true;
							if (!additive && !ImGui::GetIO().KeyShift && m_Edit.BeginInteraction())
								m_CurveDrag = { dragKind, m_Selection.trackId, section->id, key.id, channelIndex,
									key.tick, value, key.arriveTangent, key.leaveTangent, key.arriveWeight, key.leaveWeight };
						}
					}
					previous = point; hasPrevious = true;
				}
			}
			if (curveLeftClicked && !clickConsumed && m_CurveDrag.kind == CurveDragKind::None)
			{
				m_CurveMarquee.active = true;
				m_CurveMarquee.additive = ImGui::GetIO().KeyCtrl;
				m_CurveMarquee.originX = m_CurveMarquee.currentX = ImGui::GetIO().MousePos.x;
				m_CurveMarquee.originY = m_CurveMarquee.currentY = ImGui::GetIO().MousePos.y;
				if (!m_CurveMarquee.additive) m_SelectedIds.clear();
			}
			if (m_CurveMarquee.active)
			{
				m_CurveMarquee.currentX = ImGui::GetIO().MousePos.x;
				m_CurveMarquee.currentY = ImGui::GetIO().MousePos.y;
				const ImVec2 boxMinimum(
					std::min(m_CurveMarquee.originX, m_CurveMarquee.currentX),
					std::min(m_CurveMarquee.originY, m_CurveMarquee.currentY));
				const ImVec2 boxMaximum(
					std::max(m_CurveMarquee.originX, m_CurveMarquee.currentX),
					std::max(m_CurveMarquee.originY, m_CurveMarquee.currentY));
				draw->AddRectFilled(boxMinimum, boxMaximum, IM_COL32(67, 142, 185, 42));
				draw->AddRect(boxMinimum, boxMaximum, IM_COL32(105, 191, 239, 220));
				if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
				{
					if (!m_CurveMarquee.additive) m_SelectedIds.clear();
					bool matched = false;
					for (const auto& candidate : curveCandidates)
					{
						if (candidate.point.x < boxMinimum.x || candidate.point.x > boxMaximum.x ||
							candidate.point.y < boxMinimum.y || candidate.point.y > boxMaximum.y)
							continue;
						m_SelectedIds.insert(candidate.selection.id);
						m_Selection = candidate.selection;
						matched = true;
					}
					if (!matched && !m_CurveMarquee.additive) m_Selection = {};
					m_CurveMarquee = {};
				}
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

void VansTimelineEditorWindow::HandleShortcuts()
{
	if (!m_CommandSurfaceFocused || ImGui::GetIO().WantTextInput) return;
	const auto triggered = [&](Vans::VansTimelineCommand command)
	{
		return m_CommandMap.IsTriggered(command);
	};
	if (triggered(Vans::VansTimelineCommand::Save) && m_ActiveAPI)
	{
		const auto result = m_Edit.Save(*m_ActiveAPI); if (!result) SetError(result.message);
		else Vans::VansTimelineEditorStateStore::RemoveRecovery(m_Path);
	}
	else if (triggered(Vans::VansTimelineCommand::Undo))
	{
		const auto result = m_Edit.Undo(); if (!result) SetError(result.message); else RefreshPreview();
	}
	else if (triggered(Vans::VansTimelineCommand::Redo))
	{
		const auto result = m_Edit.Redo(); if (!result) SetError(result.message); else RefreshPreview();
	}
	else if (triggered(Vans::VansTimelineCommand::Copy)) CopySelection();
	else if (triggered(Vans::VansTimelineCommand::Paste)) PasteSelection();
	else if (triggered(Vans::VansTimelineCommand::Duplicate)) DuplicateSelection();
	else if (triggered(Vans::VansTimelineCommand::SplitSection)) SplitSelection();
	else if (triggered(Vans::VansTimelineCommand::DeleteSelection) && m_Selection.kind != SelectionKind::Asset)
		DeleteSelection();
	else if (triggered(Vans::VansTimelineCommand::Stop))
	{
		m_Preview.RestoreAndDetach();
		m_Playhead = m_Edit.Asset().playbackRange.startTick;
	}
	else if (triggered(Vans::VansTimelineCommand::PlayPause))
	{
		EnsurePreview(); std::string error;
		if (m_Preview.State() == Vans::VansTimelinePreviewState::Playing) m_Preview.Pause(error);
		else m_Preview.Play(error);
		if (!error.empty()) SetError(std::move(error));
	}
	else if (triggered(Vans::VansTimelineCommand::PreviousKey)) SeekPreview(AdjacentKeyTick(-1));
	else if (triggered(Vans::VansTimelineCommand::NextKey)) SeekPreview(AdjacentKeyTick(1));
	else if (triggered(Vans::VansTimelineCommand::FrameSelection)) FrameSelection();
	else if (triggered(Vans::VansTimelineCommand::FrameAll)) FrameAll();
	else if (triggered(Vans::VansTimelineCommand::Rename)) BeginRenameSelection();
	else if (triggered(Vans::VansTimelineCommand::SetPlaybackStart)) SetPlaybackBoundary(true);
	else if (triggered(Vans::VansTimelineCommand::SetPlaybackEnd)) SetPlaybackBoundary(false);
	else if (triggered(Vans::VansTimelineCommand::SetSelectionStart)) SetSelectionBoundary(true);
	else if (triggered(Vans::VansTimelineCommand::SetSelectionEnd)) SetSelectionBoundary(false);
	else if (triggered(Vans::VansTimelineCommand::AddMarker)) AddMarkerAtPlayhead();
	else if (triggered(Vans::VansTimelineCommand::ToggleAutoKey))
		m_AutoKeyMode = m_AutoKeyMode == AutoKeyMode::Off ? AutoKeyMode::KeyExisting : AutoKeyMode::Off;
	else if (triggered(Vans::VansTimelineCommand::AddKey))
	{
		Vans::VansTimelineSection* section = SelectedSection();
		Vans::VansTimelineChannel* channel = SelectedChannel();
		if (section && channel)
		{
			Vans::VansTimelineKey key;
			key.tick = std::clamp(m_Playhead - section->startTick, Vans::VansTimelineTick{ 0 },
				std::max<Vans::VansTimelineTick>(0, section->durationTicks - 1));
			key.value = DefaultKeyValue(channel->type);
			key.interpolation = channel->type == Vans::VansTimelineChannelType::Bool ||
				channel->type == Vans::VansTimelineChannelType::String ||
				channel->type == Vans::VansTimelineChannelType::EventPayload
				? Vans::VansTimelineInterpolation::Constant : Vans::VansTimelineInterpolation::Auto;
			const auto result = m_Edit.AddKey(m_Selection.trackId, section->id, m_Selection.channelIndex,
				std::move(key));
			if (!result) SetError(result.message);
			else { Select({ SelectionKind::Key, result.objectId, m_Selection.trackId, section->id,
				m_Selection.channelIndex }); RefreshPreview(); }
		}
	}
}

void VansTimelineEditorWindow::DrawStatusBar()
{
	m_Preview.Poll();
	if (m_Preview.State() != Vans::VansTimelinePreviewState::Detached) m_Playhead = m_Preview.CurrentTick();
	const std::size_t errorCount = std::count_if(m_Edit.Diagnostics().begin(), m_Edit.Diagnostics().end(), [](const auto& item)
	{
		return item.severity == Vans::VansTimelineDiagnosticSeverity::Error;
	});
	const std::size_t warningCount = std::count_if(m_Edit.Diagnostics().begin(), m_Edit.Diagnostics().end(), [](const auto& item)
	{
		return item.severity == Vans::VansTimelineDiagnosticSeverity::Warning;
	});
	ImGui::Text("%s | %s | Tick %lld | %zu errors, %zu warnings",
		m_Edit.IsDirty() ? "Modified" : "Saved", PreviewStateName(m_Preview.State()),
		static_cast<long long>(m_Playhead), errorCount, warningCount);
	if (!m_LastError.empty()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "%s", m_LastError.c_str()); }
	ImGui::SameLine();
	ImGui::SetNextItemWidth(125.0f);
	double zoom = m_PixelsPerTick;
	const double minimumZoom = 0.0002;
	const double maximumZoom = 8.0;
	if (ImGui::SliderScalar("Zoom", ImGuiDataType_Double, &zoom,
		&minimumZoom, &maximumZoom, "%.4f",
		ImGuiSliderFlags_Logarithmic))
		m_PixelsPerTick = zoom;
}

void VansTimelineEditorWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!m_IsOpen) return;
	m_ActiveAPI = &editorAPI;
	bool open = true;
	ImGui::SetNextWindowSize(ImVec2(1240.0f, 760.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Timeline Editor", &open, ImGuiWindowFlags_MenuBar))
	{
		m_CommandSurfaceFocused = false;
		if (ImGui::BeginMenuBar())
		{
			ImGui::Text("Asset: %s", m_Path.c_str());
			ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
			const std::string& previewOwner = m_Preview.ResolvedOwnerEntityGuid().empty()
				? m_InstanceOwnerGuid : m_Preview.ResolvedOwnerEntityGuid();
			if (previewOwner.empty()) ImGui::TextDisabled("Offline isolated preview");
			else ImGui::Text("Scene instance: %s", previewOwner.c_str());
			ImGui::EndMenuBar();
		}
		DrawToolbar();
		ImGui::Separator();
		const float statusHeight = ImGui::GetFrameHeightWithSpacing();
		const float curveHeight = m_ShowCurves ? m_CurveHeight : 0.0f;
		if (ImGui::BeginTable("TimelineWorkspace", 3,
			ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp,
			ImVec2(0.0f, ImGui::GetContentRegionAvail().y - curveHeight - statusHeight)))
		{
			ImGui::TableSetupColumn("Outliner", ImGuiTableColumnFlags_WidthFixed, 280.0f);
			ImGui::TableSetupColumn("Track Canvas", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, 330.0f);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::BeginChild("TimelineOutliner", ImVec2(0, 0), false); DrawOutliner(); ImGui::EndChild();
			ImGui::TableSetColumnIndex(1); ImGui::BeginChild("TimelineCanvasChild", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
			DrawCanvas();
			m_CommandSurfaceFocused = m_CommandSurfaceFocused || ImGui::IsWindowFocused();
			ImGui::EndChild();
			ImGui::TableSetColumnIndex(2); ImGui::BeginChild("TimelineInspector", ImVec2(0, 0), false); DrawInspector(); ImGui::EndChild();
			ImGui::EndTable();
		}
		if (m_ShowCurves)
		{
			ImGui::InvisibleButton("TimelineCurveSplitter", ImVec2(-1.0f, 6.0f));
			if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
			if (ImGui::IsItemActive())
				m_CurveHeight = std::clamp(m_CurveHeight - ImGui::GetIO().MouseDelta.y, 120.0f, 500.0f);
			ImGui::BeginChild("TimelineCurvePanel", ImVec2(0.0f, m_CurveHeight), true);
			DrawCurveAndDopeSheet();
			m_CommandSurfaceFocused = m_CommandSurfaceFocused || ImGui::IsWindowFocused();
			ImGui::EndChild();
		}
		ImGui::Separator(); DrawStatusBar();
		CommitPendingInteraction();
		HandleShortcuts();
		UpdateRecovery();
	}
	ImGui::End();
	if (!open) Close();
	DrawRenamePopup();

	if (m_ShowRecoveryPrompt)
	{
		ImGui::OpenPopup("Timeline Recovery");
		m_ShowRecoveryPrompt = false;
	}
	if (ImGui::BeginPopupModal("Timeline Recovery", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("A newer local recovery snapshot is available for this Timeline.");
		if (ImGui::Button("Restore Recovery"))
		{
			if (m_RecoveryAsset)
			{
				const auto result = m_Edit.ReplaceAsset(*m_RecoveryAsset);
				if (!result) SetError(result.message);
				else { RefreshPreview(); m_RecoveryAsset.reset(); ImGui::CloseCurrentPopup(); }
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Ignore"))
		{
			Vans::VansTimelineEditorStateStore::RemoveRecovery(m_Path);
			m_RecoveryAsset.reset();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (m_CloseRequested)
	{
		ImGui::OpenPopup("Unsaved Timeline");
		m_CloseRequested = false;
	}
	if (ImGui::BeginPopupModal("Unsaved Timeline", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("This Timeline has unsaved changes.");
		if (ImGui::Button("Save"))
		{
			const auto result = m_Edit.Save(editorAPI);
			if (result)
			{
				Vans::VansTimelineEditorStateStore::RemoveRecovery(m_Path);
				ImGui::CloseCurrentPopup();
				Close();
			}
			else SetError(result.message);
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard"))
		{
			const auto result = m_Edit.RevertToSaved();
			if (result)
			{
				Vans::VansTimelineEditorStateStore::RemoveRecovery(m_Path);
				ImGui::CloseCurrentPopup();
				Close();
			}
			else SetError(result.message);
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}
}
