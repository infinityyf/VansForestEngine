#include "VansPostProcessTimelineIntegration.h"

#include "../VansPostProcessProfile.h"
#include "../Storage/VansPostProcessProfileStorage.h"
#include "../../AssetCore/VansAssetResolver.h"
#include "../../TimelineRuntime/VansTimelineEvaluator.h"
#include "../../TimelineRuntime/VansTimelineModuleApplierState.h"

#include <algorithm>
#include <unordered_map>

namespace VansGraphics
{
namespace
{
float Number(const Vans::VansTimelineValue& value, float fallback)
{
	if (const auto* number = std::get_if<float>(&value)) return *number;
	if (const auto* number = std::get_if<double>(&value)) return static_cast<float>(*number);
	if (const auto* number = std::get_if<std::int32_t>(&value)) return static_cast<float>(*number);
	if (const auto* number = std::get_if<std::int64_t>(&value)) return static_cast<float>(*number);
	return fallback;
}

VansPostProcessProfile BlendProfile(
	const VansPostProcessProfile& base,
	const VansPostProcessProfile& target,
	float weight)
{
	const float alpha = std::clamp(weight, 0.0f, 1.0f);
	const auto f = [alpha](float from, float to) { return from + (to - from) * alpha; };
	const auto b = [alpha](bool from, bool to) { return alpha < 0.5f ? from : to; };
	const auto i = [alpha](std::int32_t from, std::int32_t to) { return alpha < 0.5f ? from : to; };
	VansPostProcessProfile result = base;
	result.m_EnablePostProcess = b(base.m_EnablePostProcess, target.m_EnablePostProcess);
	result.m_EnableHDR = b(base.m_EnableHDR, target.m_EnableHDR);
	result.m_EnableAutoExposure = b(base.m_EnableAutoExposure, target.m_EnableAutoExposure);
	result.m_ExposureCompensation = f(base.m_ExposureCompensation, target.m_ExposureCompensation);
	result.m_MinEV100 = f(base.m_MinEV100, target.m_MinEV100);
	result.m_MaxEV100 = f(base.m_MaxEV100, target.m_MaxEV100);
	result.m_AdaptationSpeedUp = f(base.m_AdaptationSpeedUp, target.m_AdaptationSpeedUp);
	result.m_AdaptationSpeedDown = f(base.m_AdaptationSpeedDown, target.m_AdaptationSpeedDown);
	result.m_EnableBloom = b(base.m_EnableBloom, target.m_EnableBloom);
	result.m_BloomThreshold = f(base.m_BloomThreshold, target.m_BloomThreshold);
	result.m_BloomKnee = f(base.m_BloomKnee, target.m_BloomKnee);
	result.m_BloomIntensity = f(base.m_BloomIntensity, target.m_BloomIntensity);
	result.m_BloomScatter = f(base.m_BloomScatter, target.m_BloomScatter);
	result.m_BloomClamp = f(base.m_BloomClamp, target.m_BloomClamp);
	result.m_BloomTintR = f(base.m_BloomTintR, target.m_BloomTintR);
	result.m_BloomTintG = f(base.m_BloomTintG, target.m_BloomTintG);
	result.m_BloomTintB = f(base.m_BloomTintB, target.m_BloomTintB);
	result.m_BloomShapeMode = i(base.m_BloomShapeMode, target.m_BloomShapeMode);
	result.m_BloomShapeIntensity = f(base.m_BloomShapeIntensity, target.m_BloomShapeIntensity);
	result.m_BloomShapeBlend = f(base.m_BloomShapeBlend, target.m_BloomShapeBlend);
	result.m_BloomShapeAngleDeg = f(base.m_BloomShapeAngleDeg, target.m_BloomShapeAngleDeg);
	result.m_BloomAnamorphicStretch = f(base.m_BloomAnamorphicStretch, target.m_BloomAnamorphicStretch);
	result.m_BloomStreakCount = i(base.m_BloomStreakCount, target.m_BloomStreakCount);
	result.m_BloomStreakLength = f(base.m_BloomStreakLength, target.m_BloomStreakLength);
	result.m_BloomStreakAttenuation = f(base.m_BloomStreakAttenuation, target.m_BloomStreakAttenuation);
	result.m_ToneMapperType = i(base.m_ToneMapperType, target.m_ToneMapperType);
	result.m_WhitePoint = f(base.m_WhitePoint, target.m_WhitePoint);
	result.m_EnableColorGrading = b(base.m_EnableColorGrading, target.m_EnableColorGrading);
	result.m_Contrast = f(base.m_Contrast, target.m_Contrast);
	result.m_Saturation = f(base.m_Saturation, target.m_Saturation);
	result.m_HueShift = f(base.m_HueShift, target.m_HueShift);
	result.m_Temperature = f(base.m_Temperature, target.m_Temperature);
	result.m_Tint = f(base.m_Tint, target.m_Tint);
	result.m_EnableDOF = b(base.m_EnableDOF, target.m_EnableDOF);
	result.m_FocusDistance = f(base.m_FocusDistance, target.m_FocusDistance);
	result.m_FocalLengthMm = f(base.m_FocalLengthMm, target.m_FocalLengthMm);
	result.m_FStop = f(base.m_FStop, target.m_FStop);
	result.m_SensorHeightMm = f(base.m_SensorHeightMm, target.m_SensorHeightMm);
	result.m_MaxCoC = f(base.m_MaxCoC, target.m_MaxCoC);
	result.m_DOFBlurTransmissionBackground = b(base.m_DOFBlurTransmissionBackground, target.m_DOFBlurTransmissionBackground);
	result.m_EnableMotionBlur = b(base.m_EnableMotionBlur, target.m_EnableMotionBlur);
	result.m_ShutterScale = f(base.m_ShutterScale, target.m_ShutterScale);
	result.m_MotionBlurSamples = i(base.m_MotionBlurSamples, target.m_MotionBlurSamples);
	result.m_EnableChromaticAberration = b(base.m_EnableChromaticAberration, target.m_EnableChromaticAberration);
	result.m_ChromaticAberrationIntensity = f(base.m_ChromaticAberrationIntensity, target.m_ChromaticAberrationIntensity);
	result.m_EnableSharpen = b(base.m_EnableSharpen, target.m_EnableSharpen);
	result.m_SharpenIntensity = f(base.m_SharpenIntensity, target.m_SharpenIntensity);
	result.m_IsDirty = true;
	return result;
}

struct PostProcessRestoreState
{
	Vans::VansTimelineWriterHandle writer;
	VansPostProcessProfile previous;
};

class FadeTimelineApplier final : public Vans::IVansTimelineOutputApplier
{
public:
	FadeTimelineApplier(VansPostProcessProfile& profile, std::shared_ptr<Vans::VansAssetResolver> resolver)
		: m_Profile(profile), m_Resolver(std::move(resolver)) {}
	Vans::VansTimelineOutputTypeId OutputType() const override
	{
		return Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
			std::string(Vans::TimelineNames::FadePostProcess) + ".Output");
	}
	std::string_view StableName() const override { return "Render.FadePostProcessTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(Vans::VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(Vans::VansTimelineSampleOutput); }
	Vans::VansTimelineApplyResult Apply(const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget&, Vans::VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<Vans::VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.section) return { Vans::VansTimelineApplyStatus::Ignored };
		const Vans::VansTimelineCompiledDataReader reader(
			context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const Vans::VansTimelineValue* modeValue = reader.ValueAt(context.section->extensionData, 0);
		const auto* mode = modeValue ? std::get_if<std::string>(modeValue) : nullptr;
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			return PostProcessRestoreState{ context.writer, m_Profile };
		});
		(void)state;
		float value = static_cast<float>(sample->weight);
		for (const Vans::VansTimelineChannel& channel : context.section->channels)
			if (channel.name == "weight")
				if (const auto sampled = Vans::VansTimelineEvaluator::SampleChannel(channel, sample->localTick))
					value *= Number(*sampled, 1.0f);
		value = std::clamp(value, 0.0f, 1.0f);
		if (!mode || *mode == "Fade")
		{
		if (const Vans::VansTimelineValue* colorValue = reader.ValueAt(context.section->extensionData, 1))
			if (const auto* color = std::get_if<Vans::VansTimelineColorLinear>(colorValue))
			{
				m_Profile.m_TimelineFadeColorR = static_cast<float>(color->value[0]);
				m_Profile.m_TimelineFadeColorG = static_cast<float>(color->value[1]);
				m_Profile.m_TimelineFadeColorB = static_cast<float>(color->value[2]);
			}
		m_Profile.m_TimelineFadeOpacity = value;
		m_Profile.m_IsDirty = true;
		}
		else
		{
			if (context.section->assetGuid.empty())
				return { Vans::VansTimelineApplyStatus::Failed, {}, "PostProcess mode requires a profile asset" };
			const Vans::VansResolvedAsset asset = m_Resolver->Resolve(
				context.section->assetGuid, Vans::VansAssetType::PostProcessProfile);
			if (!asset.valid) return { Vans::VansTimelineApplyStatus::Failed, {}, asset.error };
			auto found = m_Profiles.find(context.section->assetGuid);
			if (found == m_Profiles.end())
			{
				VansPostProcessProfile loaded; std::string loadError;
				if (!VansPostProcessProfileStorage::Load(asset.readPath, loaded, loadError))
					return { Vans::VansTimelineApplyStatus::Failed, {}, loadError };
				found = m_Profiles.emplace(context.section->assetGuid, std::move(loaded)).first;
			}
			m_Profile = BlendProfile(state->previous, found->second, value);
		}
		const Vans::VansTimelineResourceId resource{ Vans::VansStableHash64("Render.PostProcess"),
			static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&m_Profile)) };
		return { Vans::VansTimelineApplyStatus::Applied, { restore, {}, {}, resource } };
	}
	bool Restore(Vans::VansTimelineRestoreToken token) override
	{
		PostProcessRestoreState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		m_Profile = state->previous;
		m_Profile.m_IsDirty = true;
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(Vans::VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansPostProcessProfile& m_Profile;
	std::shared_ptr<Vans::VansAssetResolver> m_Resolver;
	std::unordered_map<std::string, VansPostProcessProfile> m_Profiles;
	Vans::VansTimelineModuleApplierState<PostProcessRestoreState> m_State;
};
}

bool VansRegisterPostProcessTimelineIntegration(
	VansPostProcessProfile& profile,
	std::shared_ptr<Vans::VansAssetResolver> resolver,
	Vans::VansTimelineApplierRegistry& registry,
	std::string& error)
{
	if (!resolver) { error = "PostProcess Timeline integration requires an asset resolver"; return false; }
	return registry.Register(std::make_shared<FadeTimelineApplier>(profile, std::move(resolver)), error);
}
}
