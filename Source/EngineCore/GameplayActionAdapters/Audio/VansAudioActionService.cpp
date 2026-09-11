#include "VansAudioActionService.h"

#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AudioCore/VansAudioManager.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
float Number(const VansSerializedValue& payload, const char* field, float fallback)
{
	const auto* value = FindObjectField(payload, field);
	return value ? static_cast<float>(ReadSerializedNumber(*value, fallback)) : fallback;
}

VansActionCommandResult Failure(std::string message)
{
	return { VansActionError::Dependency, {}, VansSerializedValue::Object({}), std::move(message) };
}

bool ReadResource(const VansSerializedValue& payload, VansGenerationHandle& resource)
{
	const auto* value = FindObjectField(payload, "resource");
	if (!value || value->kind != VansSerializedValue::Kind::Object) return false;
	const auto index = ReadSerializedIntField(*value, "index", -1);
	const auto generation = ReadSerializedIntField(*value, "generation", 0);
	if (index < 0 || index > UINT32_MAX || generation <= 0 || generation > UINT32_MAX) return false;
	resource = { static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(generation) };
	return true;
}
}

VansAudioActionService::VansAudioActionService(VansRuntimeWorld& world,
	VansEngine::VansAudioManager& audio, PositionResolver resolvePosition)
	: m_World(world), m_Audio(audio), m_ResolvePosition(std::move(resolvePosition)) {}

VansActionCommandResult VansAudioActionService::Execute(const VansActionCommand& command)
{
	const auto is = [&](const char* name)
	{
		return command.command == VansMakeStableId<VansActionFieldIdTag>(name);
	};
	if (is("Audio.OneShot") || is("Audio.Loop"))
	{
		const auto sound = ReadSerializedStringField(command.payload, "sound");
		auto* asset = m_Audio.Get(sound);
		if (!asset) return Failure("Audio asset is not loaded: " + sound);
		const float volume = Number(command.payload, "volume", 1.0f);
		const float pitch = Number(command.payload, "pitch", 1.0f);
		if (is("Audio.OneShot"))
		{
			VansEngine::VansAudioOneShotRequest request;
			request.sourceName = sound;
			request.volume = asset->GetVolume() * volume;
			request.pitch = asset->GetPitch() * pitch;
			request.spatial = ReadSerializedBoolField(command.payload, "spatial", true);
			request.bus = asset->GetBusName();
			request.referenceDistance = asset->GetRefDist();
			request.maxDistance = asset->GetMaxDist();
			request.rolloff = asset->GetRolloff();
			request.reverbSend = asset->GetReverbSend();
			if (request.spatial)
			{
				const auto emitterGuid = ReadSerializedStringField(command.payload, "emitter");
				const auto emitter = emitterGuid.empty() ? command.context.Entity(VansActionContextSlots::Owner)
					: m_World.Entities().FindByGuid(emitterGuid);
				glm::vec3 position(0.0f);
				if (!m_World.IsAlive(emitter) || !m_ResolvePosition || !m_ResolvePosition(emitter, position)
					|| !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
					return Failure("Audio emitter has no valid world position");
				request.positionX = position.x;
				request.positionY = position.y;
				request.positionZ = position.z;
			}
			// 射击已经发生后，尾音由音频管理器持有，不随动作结束或取消截断。
			if (!m_Audio.PlayOneShot(request).IsValid()) return Failure("Audio one-shot could not start: " + sound);
			return {};
		}
		auto source = std::make_unique<VansEngine::VansAudioSourceBinding>();
		if (!source->Bind(&m_Audio, sound) || !source->UsesIndependentPlayback()) return Failure("Audio loop could not bind: " + sound);
		source->SetSpatial(false);
		source->SetLoop(true);
		source->SetVolume(asset->GetVolume() * volume);
		source->SetPitch(asset->GetPitch() * pitch);
		source->SetBusGain(m_Audio.GetEffectiveBusGain(source->GetBusName()));
		source->Play();
		return { VansActionError::None, m_Loops.Emplace(Loop{ std::move(source) }) };
	}
	if (is("Audio.Update") || is("Audio.Stop"))
	{
		VansGenerationHandle handle;
		if (!ReadResource(command.payload, handle)) return Failure("Audio resource is invalid");
		auto* loop = m_Loops.Resolve(handle);
		if (!loop || loop->fadeRemaining > 0.0f) return Failure("Audio resource is stale or stopping");
		if (is("Audio.Update"))
		{
			loop->source->SetVolume(Number(command.payload, "volume", 1.0f));
			loop->source->SetPitch(Number(command.payload, "pitch", 1.0f));
		}
		else
		{
			const float fade = Number(command.payload, "fadeOut", 0.0f);
			if (fade <= 0.0f) { std::string error; Release(handle, error); }
			else
			{
				// Stop 已归还 GAF 资源；渐隐尾部移交内部实例，由 Tick 回收。
				Loop fading = std::move(*loop);
				m_Loops.Release(handle);
				fading.fadeDuration = fading.fadeRemaining = fade;
				fading.fadeVolume = fading.source->GetVolume();
				m_Loops.Emplace(std::move(fading));
			}
		}
		return {};
	}
	return Failure("Audio command is undeclared");
}

bool VansAudioActionService::Release(VansGenerationHandle resource, std::string& error)
{
	if (!m_Loops.Release(resource)) { error = "Audio resource is stale"; return false; }
	return true;
}

void VansAudioActionService::Tick(double deltaSeconds)
{
	std::vector<VansGenerationHandle> completed;
	m_Loops.ForEach([&](VansGenerationHandle handle, Loop& loop)
	{
		loop.source->Tick();
		const auto& bus = loop.source->GetBusName();
		loop.source->SetBusGain(m_Audio.GetEffectiveBusGain(bus));
		loop.source->SetBusLowpassHighFrequencyGain(m_Audio.GetBusState("Master").lowpassHighFrequencyGain *
			(bus == "Master" ? 1.0f : m_Audio.GetBusState(bus).lowpassHighFrequencyGain));
		if (loop.fadeRemaining > 0.0f)
		{
			loop.fadeRemaining = std::max(0.0f, loop.fadeRemaining - static_cast<float>(std::max(0.0, deltaSeconds)));
			loop.source->SetVolume(loop.fadeVolume * loop.fadeRemaining / loop.fadeDuration);
			if (loop.fadeRemaining == 0.0f) completed.push_back(handle);
		}
	});
	for (auto handle : completed) m_Loops.Release(handle);
}
}
