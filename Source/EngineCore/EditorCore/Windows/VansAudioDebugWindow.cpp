#include "VansAudioDebugWindow.h"

#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace
{
	constexpr std::array<const char*, 6> kDefaultBuses{
		"Master", "SFX", "Music", "UI", "Ambient", "Voice"
	};

	Vans::EditorAPI::AudioBusDebugState FindOrDefaultBus(
		const Vans::EditorAPI::AudioBusDebugSnapshot& snapshot,
		const char* name)
	{
		for (const Vans::EditorAPI::AudioBusDebugState& bus : snapshot.buses)
		{
			if (bus.name == name)
				return bus;
		}

		Vans::EditorAPI::AudioBusDebugState fallback;
		fallback.name = name;
		return fallback;
	}

	std::vector<Vans::EditorAPI::AudioBusDebugState> BuildBusRows(
		const Vans::EditorAPI::AudioBusDebugSnapshot& snapshot)
	{
		std::vector<Vans::EditorAPI::AudioBusDebugState> rows;
		rows.reserve(snapshot.buses.size() + kDefaultBuses.size());

		for (const char* busName : kDefaultBuses)
			rows.push_back(FindOrDefaultBus(snapshot, busName));

		for (const Vans::EditorAPI::AudioBusDebugState& bus : snapshot.buses)
		{
			const auto isDefault = [&bus](const char* defaultName)
			{
				return bus.name == defaultName;
			};
			if (std::any_of(kDefaultBuses.begin(), kDefaultBuses.end(), isDefault))
				continue;
			rows.push_back(bus);
		}

		return rows;
	}
}

namespace VansGraphics
{
	void VansAudioDebugWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (!VansEditorWindow::m_AudioDebugWindowOpen)
			return;

		if (!ImGui::Begin("Audio Debug", &VansEditorWindow::m_AudioDebugWindowOpen))
		{
			ImGui::End();
			return;
		}

		const Vans::EditorAPI::AudioBusDebugSnapshot snapshot =
			editorAPI.GetAudioBusDebugSnapshot();
		if (!snapshot.available)
		{
			ImGui::TextDisabled("Runtime scene is not available");
			ImGui::End();
			return;
		}

		ImGui::Text("Runtime");
		ImGui::Separator();
		ImGui::Text("Audio System: %s", snapshot.audioSystemInitialized ? "Initialized" : "Not Initialized");
		ImGui::Text("OpenAL EFX: %s", snapshot.efxSupported ? "Available" : "Unavailable");
		ImGui::Text("Default Reverb Preset: %s", snapshot.defaultReverbPreset.c_str());
		ImGui::Text("Default Reverb Wet: %.2f", snapshot.defaultReverbWetGain);
		if (snapshot.listenerAvailable)
		{
			ImGui::Text("Listener: %.2f, %.2f, %.2f",
				snapshot.listenerPosition.x,
				snapshot.listenerPosition.y,
				snapshot.listenerPosition.z);
		}
		ImGui::Text("Sources: %d total, %d bound, %d playing, %d spatial, %d virtualized, %d hardware",
			snapshot.sourceCount,
			snapshot.boundSourceCount,
			snapshot.playingSourceCount,
			snapshot.spatialSourceCount,
			snapshot.virtualizedSourceCount,
			snapshot.hardwareVoiceActiveCount);
		ImGui::Text("Source Pool: %d active leases, %d pooled",
			snapshot.activeSourceLeaseCount,
			snapshot.pooledSourceCount);
		ImGui::Text("Voice Lease Events: %d suspended, %d resumed",
			snapshot.hardwareVoiceSuspendedThisFrame,
			snapshot.hardwareVoiceResumedThisFrame);
		int maxActiveVoices = std::max(1, snapshot.maxActiveVoices);
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::SliderInt("Max Active Voices", &maxActiveVoices, 1, 128))
			editorAPI.SetAudioMaxActiveVoices(maxActiveVoices);
		ImGui::Text("Reverb Zones: %d total, %d affecting listener",
			snapshot.reverbZoneCount,
			snapshot.affectingReverbZoneCount);
		ImGui::Spacing();

		ImGui::Text("Audio Buses");
		ImGui::Separator();

		const std::vector<Vans::EditorAPI::AudioBusDebugState> rows = BuildBusRows(snapshot);
		if (ImGui::BeginTable("AudioBusDebugTable", 7,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("Gain");
			ImGui::TableSetupColumn("Duck", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Effective", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Voices", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Mute", ImGuiTableColumnFlags_WidthFixed, 56.0f);
			ImGui::TableSetupColumn("Solo", ImGuiTableColumnFlags_WidthFixed, 56.0f);
			ImGui::TableHeadersRow();

			for (const Vans::EditorAPI::AudioBusDebugState& row : rows)
			{
				ImGui::PushID(row.name.c_str());
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(row.name.c_str());

				ImGui::TableSetColumnIndex(1);
				float gain = std::clamp(row.gain, 0.0f, 4.0f);
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::SliderFloat("##gain", &gain, 0.0f, 4.0f, "%.2f"))
					editorAPI.SetAudioBusGain(row.name, gain);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.2f", row.duckingGain);

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%.2f", row.effectiveGain);

				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%d", row.activeVoiceCount);

				ImGui::TableSetColumnIndex(5);
				bool muted = row.muted;
				if (ImGui::Checkbox("##mute", &muted))
					editorAPI.SetAudioBusMuted(row.name, muted);

				ImGui::TableSetColumnIndex(6);
				bool soloed = row.soloed;
				if (ImGui::Checkbox("##solo", &soloed))
					editorAPI.SetAudioBusSoloed(row.name, soloed);

				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		if (ImGui::Button("Reset Buses"))
		{
			for (const Vans::EditorAPI::AudioBusDebugState& row : rows)
			{
				editorAPI.SetAudioBusGain(row.name, 1.0f);
				editorAPI.SetAudioBusMuted(row.name, false);
				editorAPI.SetAudioBusSoloed(row.name, false);
			}
		}

		ImGui::Spacing();
		ImGui::Text("Ducking Rules");
		ImGui::Separator();
		if (snapshot.duckingRules.empty())
		{
			ImGui::TextDisabled("No ducking rules registered");
		}
		else if (ImGui::BeginTable("AudioDuckingRuleDebugTable", 6,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Trigger", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Gain", ImGuiTableColumnFlags_WidthFixed, 56.0f);
			ImGui::TableSetupColumn("Attack", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Release", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableHeadersRow();

			for (const Vans::EditorAPI::AudioDuckingRuleDebugState& rule : snapshot.duckingRules)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(rule.triggerBusName.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(rule.targetBusName.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.2f", rule.targetGain);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%.2f", rule.attackSeconds);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%.2f", rule.releaseSeconds);
				ImGui::TableSetColumnIndex(5);
				ImGui::TextUnformatted(!rule.enabled ? "Disabled" : (rule.active ? "Active" : "Idle"));
			}
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::Text("Audio Sources");
		ImGui::Separator();
		if (snapshot.sources.empty())
		{
			ImGui::TextDisabled("No Audio components in the runtime scene");
		}
		else if (ImGui::BeginTable("AudioSourceDebugTable", 13,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 96.0f);
			ImGui::TableSetupColumn("Bind", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Vol", ImGuiTableColumnFlags_WidthFixed, 56.0f);
			ImGui::TableSetupColumn("BusGain", ImGuiTableColumnFlags_WidthFixed, 72.0f);
			ImGui::TableSetupColumn("Occ", ImGuiTableColumnFlags_WidthFixed, 56.0f);
			ImGui::TableSetupColumn("HF", ImGuiTableColumnFlags_WidthFixed, 48.0f);
			ImGui::TableSetupColumn("Q", ImGuiTableColumnFlags_WidthFixed, 48.0f);
			ImGui::TableSetupColumn("Mat", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableHeadersRow();

			for (const Vans::EditorAPI::AudioSourceDebugState& source : snapshot.sources)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(source.objectName.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(source.sourceName.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(source.busName.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(source.playing ? "Playing" : (source.paused ? "Paused" : "Stopped"));
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(source.bound
					? (source.usesInstance ? "Instance" : (source.usesPrivateNode ? "Streaming" : "Node"))
					: "Unbound");
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%.1f", source.listenerDistance);
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%.2f", source.volume);
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%.2f", source.effectiveBusGain);
				ImGui::TableSetColumnIndex(8);
				ImGui::Text("%.2f", source.occlusionGain);
				ImGui::TableSetColumnIndex(9);
				ImGui::Text("%.2f", source.occlusionHighFrequencyGain);
				ImGui::TableSetColumnIndex(10);
				ImGui::Text("%.2f", source.occlusionQueryTimer);
				ImGui::TableSetColumnIndex(11);
				ImGui::Text("%s %.2f",
					source.occlusionMaterial.c_str(),
					source.occlusionMaterialThickness);
				ImGui::TableSetColumnIndex(12);
				std::string flags;
				if (!source.objectActive) flags += "Inactive ";
				if (!source.componentEnabled) flags += "Disabled ";
				if (source.virtualized) flags += "Virt ";
				if (!source.hardwareVoiceActive) flags += "NoHW ";
				if (source.spatial) flags += "3D ";
				if (source.occlusionEnabled) flags += "Occ ";
				if (source.occlusionBlocked) flags += "Blocked ";
				if (source.dopplerEnabled) flags += "Doppler ";
				if (flags.empty()) flags = "-";
				ImGui::TextUnformatted(flags.c_str());
			}
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::Text("Reverb Zones");
		ImGui::Separator();
		if (snapshot.reverbZones.empty())
		{
			ImGui::TextDisabled("No AudioVolume or AudioReverbZone components in the runtime scene");
		}
		else if (ImGui::BeginTable("AudioReverbZoneDebugTable", 8,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGui::TableSetupColumn("Shape", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Preset", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Blend", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Wet", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Effective", ImGuiTableColumnFlags_WidthFixed, 74.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableHeadersRow();

			for (const Vans::EditorAPI::AudioReverbZoneDebugState& zone : snapshot.reverbZones)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				std::string name = zone.objectName;
				if (zone.componentType == "AudioVolume")
					name += " (Volume)";
				ImGui::TextUnformatted(name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(zone.shape.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(zone.preset.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%d", zone.priority);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%.2f", zone.blend);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%.2f", zone.wetGain);
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%.2f", zone.effectiveWetGain);
				ImGui::TableSetColumnIndex(7);
				ImGui::TextUnformatted(zone.selected ? "Selected" : (zone.affectsListener ? "Affects" : "-"));
			}
			ImGui::EndTable();
		}

		ImGui::End();
	}
}
