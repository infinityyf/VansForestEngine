#include "VansAudioActionService.h"

#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AudioCore/VansAudioManager.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

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
	VansEngine::VansAudioManager& audio, PositionResolver resolvePosition, ListenerResolver resolveListener)
	: m_World(world), m_Audio(audio), m_ResolvePosition(std::move(resolvePosition)), m_ResolveListener(std::move(resolveListener)) {}

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
			const bool spatial = ReadSerializedBoolField(command.payload, "spatial", true);
			glm::vec3 position(0.0f);
			if (spatial)
			{
				const auto* worldPosition=FindObjectField(command.payload,"position");
				if (worldPosition && !worldPosition->objectFields.empty())
				{
					const auto x=FindObjectField(*worldPosition,"x"), y=FindObjectField(*worldPosition,"y"), z=FindObjectField(*worldPosition,"z");
					if (!x || !y || !z || !ReadSerializedStringField(command.payload,"emitter").empty())
						return Failure("Audio needs exactly one emitter or world position with x, y and z");
					position=glm::vec3(ReadSerializedNumber(*x,NAN),ReadSerializedNumber(*y,NAN),ReadSerializedNumber(*z,NAN));
					if (!std::isfinite(glm::length(position))) return Failure("Audio world position is invalid");
				}
				else
				{
				const auto emitterGuid = ReadSerializedStringField(command.payload, "emitter");
				const auto emitter = emitterGuid.empty() ? command.context.Entity(VansActionContextSlots::Owner)
					: m_World.Entities().FindByGuid(emitterGuid);
				if (!m_World.IsAlive(emitter) || !m_ResolvePosition || !m_ResolvePosition(emitter, position)
					|| !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
					return Failure("Audio emitter has no valid world position");
				}
			}
			float distanceGain=1;
			if (const auto* curve=FindObjectField(command.payload,"volumeByDistance"); curve && !curve->arrayItems.empty())
			{
				glm::vec3 listener(0);
				if (!spatial || !m_ResolveListener || !m_ResolveListener(listener) || !std::isfinite(glm::length(listener))
					|| curve->arrayItems.size()>32) return Failure("Audio distance curve needs a listener and at most 32 keys");
                std::vector<VansEngine::AudioDistanceGainKey> keys;
                for (const auto& key : curve->arrayItems)
                    keys.push_back({Number(key,"distance",NAN),Number(key,"gain",NAN),
                        Number(key,"tangentIn",0),Number(key,"tangentOut",0)});
                distanceGain=VansEngine::ComputeDistanceGain(glm::length(position-listener),keys);
                if (!std::isfinite(distanceGain)) return Failure("Audio distance curve keys are invalid");
			}
			if (distanceGain<=0) return {};
			// 射击已经发生后，尾音由音频管理器持有，不随动作结束或取消截断。
			if (!m_Audio.PlayAssetOneShot(sound, volume*distanceGain, pitch, spatial,
				position.x, position.y, position.z).IsValid())
				return Failure("Audio one-shot could not start: " + sound);
			if (const auto* p=FindObjectField(command.payload,"position"); p && !p->objectFields.empty())
				VANS_LOG("[GAF Audio] WorldOneShot sound=" << sound << " gain=" << volume*distanceGain
					<< " position=" << position.x << "," << position.y << "," << position.z);
			return {};
		}
		auto source = std::make_unique<VansEngine::VansAudioSourceBinding>();
		if (!source->Bind(&m_Audio, sound)) return Failure("Audio loop could not bind: " + sound);
		auto* voice = source->GetVoice();
		voice->SetSpatial(false);
		voice->SetLoop(true);
		voice->SetVolume(asset->GetVolume() * volume);
		voice->SetPitch(asset->GetPitch() * pitch);
		voice->SetBusGain(m_Audio.GetEffectiveBusGain(voice->GetBusName()));
		voice->Play();
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
			loop->source->GetVoice()->SetVolume(Number(command.payload, "volume", 1.0f));
			loop->source->GetVoice()->SetPitch(Number(command.payload, "pitch", 1.0f));
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
				fading.fadeVolume = fading.source->GetVoice()->GetVolume();
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
		auto* voice = loop.source->GetVoice();
		voice->Tick();
		const auto& bus = voice->GetBusName();
		voice->SetBusGain(m_Audio.GetEffectiveBusGain(bus));
		voice->SetBusLowpassHighFrequencyGain(
			m_Audio.GetEffectiveBusLowpassHighFrequencyGain(bus));
		if (loop.fadeRemaining > 0.0f)
		{
			loop.fadeRemaining = std::max(0.0f, loop.fadeRemaining - static_cast<float>(std::max(0.0, deltaSeconds)));
			voice->SetVolume(loop.fadeVolume * loop.fadeRemaining / loop.fadeDuration);
			if (loop.fadeRemaining == 0.0f) completed.push_back(handle);
		}
	});
	for (auto handle : completed) m_Loops.Release(handle);
}
}
