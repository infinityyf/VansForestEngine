#include "TimelineRefactorContractTests.h"

#include "../EngineCore/TimelineCore/VansTimelineCompiler.h"
#include "../EngineCore/TimelineCore/VansTimelineSerialization.h"
#include "../EngineCore/TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineApplierRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineClockRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineEvaluator.h"
#include "../EngineCore/TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../EngineCore/TimelineRuntime/VansTimelinePreAnimatedState.h"
#include "../EngineCore/TimelineRuntime/VansTimelineSessionService.h"
#include "../EngineCore/TimelineRuntime/VansTimelineSampleExtension.h"
#include "../EngineCore/Timeline/VansTimelineExtensionContributors.h"
#include "../EngineCore/TimelineRuntime/Events/VansTimelineRuntimeEvents.h"
#include "../EngineCore/EventCore/VansEventBus.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineTrackDescriptorRegistry.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineEditService.h"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace
{
bool ExpectTimeline(bool value, const char* message)
{
	if (!value) std::cerr << "[TimelineRefactor] " << message << '\n';
	return value;
}

Vans::VansTimelineAsset MakeProbeAsset(std::string_view typeName = Vans::TimelineNames::Transform)
{
	Vans::VansTimelineAsset asset;
	asset.durationTicks = 1000;
	asset.playbackRange = { 0, 1000 };
	asset.workRange = asset.playbackRange;
	Vans::VansTimelineBinding binding;
	binding.id = "probe-binding";
	binding.stableId = Vans::VansMakeStableId<Vans::VansTimelineBindingTag>(binding.id);
	binding.targetGuid = "probe-entity";
	asset.bindings.push_back(binding);
	Vans::VansTimelineTrack track;
	track.id = "probe-track";
	track.type = Vans::VansTimelineTrackTypeRef::FromName(std::string(typeName));
	track.bindingId = binding.id;
	track.extensionData = Vans::VansSerializedValue::Object({});
	Vans::VansTimelineSection section;
	section.id = "probe-section";
	section.durationTicks = 1000;
	section.sourceOutTick = 1000;
	Vans::VansTimelineChannel channel;
	channel.id = "probe-channel";
	channel.name = "position";
	channel.type = Vans::VansTimelineValueType::Vec3;
	channel.keys.push_back({ "probe-key", 0, Vans::VansTimelineVec3{ { 1.0, 2.0, 3.0 } },
		Vans::VansTimelineInterpolation::Constant });
	section.channels.push_back(std::move(channel));
	track.sections.push_back(std::move(section));
	asset.tracks.push_back(std::move(track));
	return asset;
}

class ProbeSampleApplier final : public Vans::IVansTimelineOutputApplier
{
public:
	struct State { Vans::VansTimelineWriterHandle writer; double previous = 0.0; };

	Vans::VansTimelineOutputTypeId OutputType() const override { return m_Type; }
	std::string_view StableName() const override { return "Test.SampleOutput"; }
	std::uint32_t PayloadSize() const override { return sizeof(Vans::VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(Vans::VansTimelineSampleOutput); }
	Vans::VansTimelineApplyResult Apply(const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target, Vans::VansTimelineOutputPayloadView payload) override
	{
		const auto* sample = payload.As<Vans::VansTimelineSampleOutput>();
		if (!sample) return { Vans::VansTimelineApplyStatus::Failed, {}, "invalid payload" };
		if (!sample->active) return { Vans::VansTimelineApplyStatus::Ignored };
		if (failApply || (!failTrackId.empty() && context.track.id == failTrackId))
			return { Vans::VansTimelineApplyStatus::Failed, {}, "requested probe failure" };
		const auto [handle, state] = m_States.Acquire(context.writer, [&]
		{ return State{ context.writer, value }; });
		value = sample->weight;
		lastTarget = target.object;
		++applyCount;
		return { Vans::VansTimelineApplyStatus::Applied, { handle } };
	}
	bool Restore(Vans::VansTimelineRestoreToken token) override
	{
		State* state = m_States.Resolve(token.handle);
		if (!state) return false;
		value = state->previous;
		++restoreCount;
		return m_States.Release(token.handle);
	}
	void DeactivateWriter(Vans::VansTimelineWriterHandle) override { ++deactivateCount; }
	void ReleaseWriter(Vans::VansTimelineWriterHandle writer) override { m_States.ReleaseWriter(writer); }
	void ReleaseAll() override { m_States.Clear(); }
	Vans::VansTimelineRestoreToken Capture(
		Vans::VansTimelineWriterHandle writer,
		double next,
		Vans::VansTimelineResourceId resource)
	{
		const auto [handle, state] = m_States.Acquire(writer, [&]
		{ return State{ writer, value }; });
		(void)state;
		value = next;
		return { handle, {}, writer, resource };
	}

	Vans::VansTimelineOutputTypeId m_Type;
	double value = 0.0;
	int applyCount = 0;
	int restoreCount = 0;
	int deactivateCount = 0;
	bool failApply = false;
	std::string failTrackId;
	Vans::VansGenerationHandle lastTarget;

private:
	Vans::VansTimelineModuleApplierState<State> m_States;
};

class ProbePointApplier final : public Vans::IVansTimelineOutputApplier
{
public:
	Vans::VansTimelineOutputTypeId OutputType() const override { return type; }
	std::string_view StableName() const override { return "Test.PointOutput"; }
	std::uint32_t PayloadSize() const override { return sizeof(Vans::VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(Vans::VansTimelineSampleOutput); }
	Vans::VansTimelineApplyResult Apply(const Vans::VansTimelineApplyContext&,
		const Vans::VansResolvedTimelineTarget&, Vans::VansTimelineOutputPayloadView payload) override
	{
		const auto* sample = payload.As<Vans::VansTimelineSampleOutput>();
		if (!sample || !sample->entered)
			return { Vans::VansTimelineApplyStatus::Failed, {}, "point payload is invalid" };
		++applyCount;
		return { Vans::VansTimelineApplyStatus::Applied };
	}
	bool Restore(Vans::VansTimelineRestoreToken) override { return false; }
	void ReleaseWriter(Vans::VansTimelineWriterHandle) override { ++releaseCount; }
	void ReleaseAll() override {}
	Vans::VansTimelineOutputTypeId type;
	int applyCount = 0;
	int releaseCount = 0;
};

struct ParameterCurveCompiled
{
	std::uint32_t parameterSlot = UINT32_MAX;
};

struct ParameterCurveOutput
{
	Vans::VansGenerationHandle target;
	float value = 0.0f;
};

bool CompileParameterCurve(
	const Vans::VansTimelineExtensionCompileContext& context,
	const Vans::VansTimelineTrack& track,
	const Vans::VansTimelineSourceSchema& schema,
	Vans::VansTimelineCompiledDataWriter& writer,
	Vans::VansTimelineCompiledDataView& trackData,
	std::vector<Vans::VansTimelineCompiledDataView>& sectionData,
	Vans::VansTimelineDiagnostics& diagnostics)
{
	Vans::VansTimelineCompiledDataView ignored;
	std::vector<Vans::VansTimelineCompiledDataView> ignoredSections;
	if (!Vans::VansCompileTimelineExtensionSchema(context, track, schema, writer,
		ignored, ignoredSections, diagnostics)) return false;
	const Vans::VansSerializedValue* field =
		Vans::VansTimelineFindSourceField(track.extensionData, "parameterId");
	if (!field || field->kind != Vans::VansSerializedValue::Kind::Int) return false;
	const std::uint32_t slot = context.ParameterSlot(
		Vans::VansTimelineParameterId{ static_cast<std::uint64_t>(field->intValue) });
	if (slot == UINT32_MAX)
	{
		diagnostics.push_back({ Vans::VansTimelineDiagnosticSeverity::Error,
			"Timeline.ParameterMissing", {}, track.id, "parameterId",
			"Synthetic parameter extension references an unknown ParameterId" });
		return false;
	}
	trackData = writer.Write(ParameterCurveCompiled{ slot });
	sectionData.assign(track.sections.size(), trackData);
	return true;
}

void EvaluateParameterCurve(Vans::VansTimelineExtensionEvaluationContext& context)
{
	if (!context.section || !Vans::VansTimelineEvaluator::IsInside(
		*context.section, context.traversal.currentTick)) return;
	const auto* compiled = context.compiledData.Read<ParameterCurveCompiled>(
		context.track.extensionData);
	const Vans::VansTimelineValue* parameter = compiled
		? context.parameters.Get(compiled->parameterSlot) : nullptr;
	const auto* base = parameter ? std::get_if<float>(parameter) : nullptr;
	if (!base) return;
	float sampled = 1.0f;
	if (!context.section->channels.empty())
		if (const auto value = Vans::VansTimelineEvaluator::SampleChannel(
			context.section->channels.front(), context.traversal.currentTick - context.section->startTick))
			if (const auto* typed = std::get_if<float>(&*value)) sampled = *typed;
	context.Emit(Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		"Test.ParameterCurve.Output"), Vans::VansInvalidTimelineRegistrySlot,
		ParameterCurveOutput{ context.target.object, *base * sampled }, context.section->id);
}

void CollectParameterCurveDependencies(
	const Vans::VansTimelineTrack& track,
	std::vector<Vans::VansTimelineDependency>& dependencies)
{
	dependencies.push_back({ Vans::VansTimelineDependencyKind::ServiceCapability,
		"Test.TypedSink", {}, {}, track.id });
}

class ParameterCurveApplier final : public Vans::IVansTimelineOutputApplier
{
public:
	Vans::VansTimelineOutputTypeId OutputType() const override
	{ return Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>("Test.ParameterCurve.Output"); }
	std::string_view StableName() const override { return "Test.ParameterCurveApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(ParameterCurveOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(ParameterCurveOutput); }
	Vans::VansTimelineApplyResult Apply(const Vans::VansTimelineApplyContext&,
		const Vans::VansResolvedTimelineTarget& target,
		Vans::VansTimelineOutputPayloadView payload) override
	{
		const auto* typed = payload.As<ParameterCurveOutput>();
		if (!typed || typed->target != target.object)
			return { Vans::VansTimelineApplyStatus::Failed, {}, "typed target was not preserved" };
		targetHandle = typed->target;
		value = typed->value;
		return { Vans::VansTimelineApplyStatus::Applied };
	}
	bool Restore(Vans::VansTimelineRestoreToken) override { return true; }
	void ReleaseWriter(Vans::VansTimelineWriterHandle) override {}
	void ReleaseAll() override {}
	Vans::VansGenerationHandle targetHandle;
	float value = 0.0f;
};

bool RegisterContributorProbe(
	Vans::VansTimelineTrackExtensionRegistry& registry,
	std::string& error)
{
	return registry.Register(Vans::VansMakeTimelinePointExtension(
		"Test.Contributor", "Contributor", "Test",
		Vans::VansTimelineEvaluationPhase::PostScript,
		Vans::VansTimelineBindingRequirement::None, {}), error);
}
}

bool TestTimelineRegistryContract()
{
	Vans::VansTimelineTrackExtensionRegistry registry;
	Vans::VansTimelineTrackExtensionDescriptor descriptor;
	descriptor.stableName = "Test.Continuous";
	descriptor.typeId = Vans::VansMakeStableId<Vans::VansTimelineTrackTypeTag>(descriptor.stableName);
	descriptor.compile = Vans::VansCompileTimelineExtensionSchema;
	descriptor.evaluate = [](Vans::VansTimelineExtensionEvaluationContext&) {};
	descriptor.outputs.push_back({ Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>("Test.Output"),
		"Test.Output", sizeof(std::uint32_t), alignof(std::uint32_t), true });
	std::string error;
	if (!ExpectTimeline(registry.Register(descriptor, error), error.c_str())) return false;
	if (!ExpectTimeline(!registry.Register(descriptor, error), "duplicate extension was accepted")) return false;
	if (!ExpectTimeline(registry.Seal(error), error.c_str())) return false;
	if (!ExpectTimeline(!registry.Register(std::move(descriptor), error), "sealed registry accepted registration")) return false;
	const auto& builtIns = Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	const std::unordered_set<std::string_view> expected{
		Vans::TimelineNames::Transform, Vans::TimelineNames::Property,
		Vans::TimelineNames::Activation, Vans::TimelineNames::Constraint,
		Vans::TimelineNames::AnimationClip, Vans::TimelineNames::AnimatorParameter,
		Vans::TimelineNames::BoneOverride, Vans::TimelineNames::Audio,
		Vans::TimelineNames::Media, Vans::TimelineNames::Particle,
		Vans::TimelineNames::CameraCut, Vans::TimelineNames::CameraProperty,
		Vans::TimelineNames::CameraShake, Vans::TimelineNames::FadePostProcess,
		Vans::TimelineNames::Light, Vans::TimelineNames::MaterialParameter,
		Vans::TimelineNames::MaterialSwitch, Vans::TimelineNames::UIState,
		Vans::TimelineNames::EventSignal, Vans::TimelineNames::SubTimeline,
		Vans::TimelineNames::TimeScale };
	if (!ExpectTimeline(builtIns.All().size() >= expected.size() && builtIns.ManifestHash() != 0,
		"built-in registry size or manifest is wrong")) return false;
	for (std::string_view name : expected)
		if (!ExpectTimeline(builtIns.Resolve(name) != nullptr,
			"built-in registry is missing a declared capability")) return false;
	if (!ExpectTimeline(!builtIns.Resolve("Timeline.Spawnable") &&
		!builtIns.Resolve("Timeline.SceneState"),
		"removed no-op Timeline capabilities are still registered")) return false;
	Vans::VansTimelineExtensionContributors contributors;
	Vans::VansTimelineTrackExtensionRegistry contributedRegistry;
	if (!ExpectTimeline(contributors.Register("Test.Module", RegisterContributorProbe, error),
		error.c_str())) return false;
	if (!ExpectTimeline(!contributors.Register("Test.Module", RegisterContributorProbe, error),
		"duplicate extension contributor was accepted")) return false;
	if (!ExpectTimeline(contributors.ApplyAndSeal(contributedRegistry, error) &&
		contributedRegistry.Resolve("Test.Contributor"), error.c_str())) return false;
	return ExpectTimeline(!contributors.Register("Test.Late", RegisterContributorProbe, error),
		"extension contributor accepted a late registration after sealing");
}

bool TestTimelineSerializationContract()
{
	using Json = nlohmann::ordered_json;
	const Json current = {
		{ "assetKind", "Timeline" }, { "durationTicks", 1000 },
		{ "playbackRange", { { "startTick", 0 }, { "endTick", 1000 } } },
		{ "workRange", { { "startTick", 0 }, { "endTick", 1000 } } },
		{ "parameters", Json::array() },
		{ "bindings", Json::array() }, { "groups", Json::array() }, { "markers", Json::array() },
		{ "tracks", Json::array({ {
			{ "id", "track" }, { "type", "Timeline.FadePostProcess" },
			{ "condition", { { "parameterId", 0 }, { "expectedValue", nullptr }, { "negate", false } } },
			{ "extensionData", { { "mode", "Fade" } } }, { "sections", Json::array() }
		} }) }
	};
	Vans::VansTimelineAsset decoded;
	std::string error;
	if (!ExpectTimeline(Vans::VansTimelineSerialization::Decode(current, decoded, error), error.c_str())) return false;
	const Json encoded = Vans::VansTimelineSerialization::Encode(decoded);
	if (!ExpectTimeline(encoded["tracks"][0].contains("extensionData") &&
		!encoded["tracks"][0].contains("config"),
		"canonical Timeline did not preserve the direct extension-data format")) return false;
	Vans::VansTimelineAsset roundTrip;
	if (!ExpectTimeline(Vans::VansTimelineSerialization::Decode(encoded, roundTrip, error), error.c_str())) return false;
	if (!ExpectTimeline(Vans::VansTimelineSerialization::Encode(roundTrip) == encoded,
		"canonical Timeline roundtrip is unstable")) return false;
	return true;
}

bool TestTimelineEditorInteractionContract()
{
	Vans::VansTimelineEditService edit;
	Vans::VansTimelineAsset asset = MakeProbeAsset();
	if (!ExpectTimeline(static_cast<bool>(edit.BeginInteraction()), "Timeline editor interaction did not begin")) return false;
	if (!ExpectTimeline(static_cast<bool>(edit.ReplaceAsset(asset)), "Timeline editor could not install its working asset")) return false;
	if (!ExpectTimeline(static_cast<bool>(edit.MoveSection("probe-track", "probe-section", 100)),
		"Timeline editor section move was rejected")) return false;
	if (!ExpectTimeline(static_cast<bool>(edit.TrimSection("probe-track", "probe-section", 100, 800)),
		"Timeline editor section trim was rejected")) return false;
	if (!ExpectTimeline(static_cast<bool>(edit.AddKey("probe-track", "probe-section", 0,
		{ "probe-key-2", 400, Vans::VansTimelineVec3{ { 4.0, 5.0, 6.0 } },
			Vans::VansTimelineInterpolation::Linear })),
		"Timeline editor key insertion was rejected")) return false;
	if (!ExpectTimeline(static_cast<bool>(edit.MoveKey("probe-track", "probe-section", 0, "probe-key", 120)),
		"Timeline editor key move was rejected")) return false;
	const auto& edited = edit.Asset();
	if (!ExpectTimeline(edited.tracks.size() == 1 && edited.tracks.front().sections.size() == 1,
		"Timeline editor interaction changed track topology unexpectedly")) return false;
	const auto& section = edited.tracks.front().sections.front();
	if (!ExpectTimeline(section.startTick == 100 && section.durationTicks == 800 &&
		section.channels.size() == 1 && section.channels.front().keys.size() == 2,
		"Timeline editor section/key edits did not persist in the working copy")) return false;
	const auto moved = std::find_if(section.channels.front().keys.begin(),
		section.channels.front().keys.end(), [](const auto& key) { return key.id == "probe-key"; });
	if (!ExpectTimeline(moved != section.channels.front().keys.end() && moved->tick == 120,
		"Timeline editor key move did not update the requested key")) return false;
	if (!ExpectTimeline(static_cast<bool>(edit.CancelInteraction()), "Timeline editor interaction did not cancel cleanly")) return false;
	return ExpectTimeline(!edit.IsInteracting(), "Timeline editor interaction remained active after cancel");
}

bool TestTimelineDemoHallAssetContract()
{
	namespace fs = std::filesystem;
	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 6 && !fs::exists(workspace / "DemoHallProject"); ++depth)
		workspace = workspace.parent_path();
	const fs::path source = workspace / "DemoHallProject" / "Assets" /
		"Cinematics" / "GlassBreakImpact.vtimeline";
	if (!ExpectTimeline(fs::is_regular_file(source),
		"DemoHall Timeline asset was not found for direct validation")) return false;
	Vans::VansTimelineAsset asset;
	std::string error;
	if (!ExpectTimeline(Vans::VansTimelineSerialization::Load(source, asset, error),
		error.c_str())) return false;
	bool hasCameraShake = false;
	bool hasVirtualCameraParameters = false;
	bool hasVisibleRedFade = false;
	bool hasReusableCameraTransition = false;
	bool hasPlayerRelativeShoulder = false;
	bool hasPlayerForwardLookAt = false;
	Vans::VansTimelineTick cameraTransitionStart = 0;
	Vans::VansTimelineTick cameraTransitionEnd = 0;
	Vans::VansTimelineTick cameraBlendInTicks = 0;
	Vans::VansTimelineTick cameraBlendOutTicks = 0;
	Vans::VansTimelineTick cameraShakeStart = 0;
	Vans::VansTimelineTick cameraShakeEnd = 0;
	const auto serializedNumber = [](const Vans::VansSerializedValue& value)
	{
		if (value.kind == Vans::VansSerializedValue::Kind::Float) return value.floatValue;
		if (value.kind == Vans::VansSerializedValue::Kind::Int) return static_cast<double>(value.intValue);
		return 0.0;
	};
	const Vans::VansTimelineChannel* shakePosition = nullptr;
	const Vans::VansTimelineChannel* shakeRotation = nullptr;
	const Vans::VansTimelineChannel* fadeWeight = nullptr;
	for (const Vans::VansTimelineTrack& track : asset.tracks)
	{
		if (track.type.stableName == Vans::TimelineNames::CameraProperty)
		{
			bool hasLensChannel = false;
			bool hasPoseChannel = false;
			for (const Vans::VansTimelineSection& section : track.sections)
				for (const Vans::VansTimelineChannel& channel : section.channels)
				{
					hasLensChannel |= channel.name == "fieldOfView" ||
						channel.name == "nearClip" || channel.name == "farClip";
					hasPoseChannel |= channel.name == "position" || channel.name == "rotation";
				}
			hasVirtualCameraParameters =
				track.bindingId == "binding-impact-virtual-camera" && hasLensChannel && !hasPoseChannel;
		}
		if (track.type.stableName == Vans::TimelineNames::CameraShake)
		{
			bool hasPositionOffset = false;
			bool hasRotationOffset = false;
			for (const Vans::VansTimelineSection& section : track.sections)
			{
				cameraShakeStart = section.startTick;
				cameraShakeEnd = section.startTick + section.durationTicks;
				for (const Vans::VansTimelineChannel& channel : section.channels)
				{
					hasPositionOffset |= channel.name == "positionOffset" && channel.keys.size() >= 2;
					hasRotationOffset |= channel.name == "rotationOffset" && channel.keys.size() >= 2;
					if (channel.name == "positionOffset") shakePosition = &channel;
					if (channel.name == "rotationOffset") shakeRotation = &channel;
				}
			}
			hasCameraShake = track.bindingId.empty() && hasPositionOffset && hasRotationOffset;
		}
		if (track.type.stableName == Vans::TimelineNames::CameraCut)
			for (const Vans::VansTimelineSection& section : track.sections)
			{
				if (!section.extensionData) continue;
				const Vans::VansSerializedValue* blendIn = Vans::VansTimelineFindSourceField(
					*section.extensionData, "blendDurationTicks");
				const Vans::VansSerializedValue* blendOut = Vans::VansTimelineFindSourceField(
					*section.extensionData, "blendOutDurationTicks");
				const Vans::VansSerializedValue* suppressLook = Vans::VansTimelineFindSourceField(
					*section.extensionData, "suppressUserLook");
				cameraBlendInTicks = blendIn && blendIn->kind == Vans::VansSerializedValue::Kind::Int
					? blendIn->intValue : 0;
				cameraBlendOutTicks = blendOut && blendOut->kind == Vans::VansSerializedValue::Kind::Int
					? blendOut->intValue : 0;
				cameraTransitionStart = section.startTick;
				cameraTransitionEnd = section.startTick + section.durationTicks;
				hasReusableCameraTransition = cameraBlendInTicks > 0 && cameraBlendOutTicks > 0 &&
					suppressLook && suppressLook->kind == Vans::VansSerializedValue::Kind::Bool &&
					suppressLook->boolValue;
			}
		if (track.type.stableName == Vans::TimelineNames::Constraint)
			for (const Vans::VansTimelineSection& section : track.sections)
			{
				if (!section.extensionData) continue;
				const auto* kind = Vans::VansTimelineFindSourceField(*section.extensionData, "constraintType");
				const auto* sourceBinding = Vans::VansTimelineFindSourceField(
					*section.extensionData, "sourceBindingId");
				const auto* basis = Vans::VansTimelineFindSourceField(*section.extensionData, "offsetBasis");
				const auto* positionOffset = Vans::VansTimelineFindSourceField(
					*section.extensionData, "offsetPosition");
				const auto* lookAtOffset = Vans::VansTimelineFindSourceField(
					*section.extensionData, "lookAtOffset");
				const auto* rotationConvention = Vans::VansTimelineFindSourceField(
					*section.extensionData, "rotationConvention");
				const bool playerYawRelative = sourceBinding && basis &&
					sourceBinding->kind == Vans::VansSerializedValue::Kind::String &&
					sourceBinding->stringValue == "binding-impact-player" &&
					basis->kind == Vans::VansSerializedValue::Kind::String && basis->stringValue == "YawOnly";
				if (kind && kind->kind == Vans::VansSerializedValue::Kind::String && playerYawRelative)
				{
					hasPlayerRelativeShoulder |= kind->stringValue == "Position" && positionOffset &&
						positionOffset->kind == Vans::VansSerializedValue::Kind::Array &&
						positionOffset->arrayItems.size() == 3 &&
						serializedNumber(positionOffset->arrayItems[1]) > 1.0 &&
						serializedNumber(positionOffset->arrayItems[2]) < -1.0;
					hasPlayerForwardLookAt |= kind->stringValue == "LookAt" && lookAtOffset &&
						lookAtOffset->kind == Vans::VansSerializedValue::Kind::Array &&
						lookAtOffset->arrayItems.size() == 3 &&
						serializedNumber(lookAtOffset->arrayItems[2]) > 1.0 &&
						rotationConvention &&
						rotationConvention->kind == Vans::VansSerializedValue::Kind::String &&
						rotationConvention->stringValue == "CameraEuler";
				}
			}
		if (track.type.stableName == Vans::TimelineNames::FadePostProcess)
		{
			const Vans::VansSerializedValue* color = Vans::VansTimelineFindSourceField(
				track.extensionData, "color");
			hasVisibleRedFade = color && color->kind == Vans::VansSerializedValue::Kind::Array &&
				color->arrayItems.size() == 4 &&
				color->arrayItems[0].kind == Vans::VansSerializedValue::Kind::Float &&
				color->arrayItems[0].floatValue >= 0.2;
			for (const Vans::VansTimelineSection& section : track.sections)
				for (const Vans::VansTimelineChannel& channel : section.channels)
					if (channel.name == "weight") fadeWeight = &channel;
		}
	}
	if (!ExpectTimeline(hasCameraShake,
		"DemoHall impact Timeline does not contain an authored camera-local shake")) return false;
	if (!ExpectTimeline(hasVirtualCameraParameters,
		"DemoHall virtual camera must keep lens parameters without duplicating pose or a Camera component")) return false;
	if (!ExpectTimeline(hasReusableCameraTransition &&
		cameraShakeStart >= cameraTransitionStart + cameraBlendInTicks &&
		cameraShakeEnd <= cameraTransitionEnd - cameraBlendOutTicks,
		"DemoHall camera shake must run between reusable CameraCut enter and exit blends")) return false;
	if (!ExpectTimeline(hasPlayerRelativeShoulder && hasPlayerForwardLookAt,
		"DemoHall virtual camera must use editable player-relative shoulder and forward-look constraints")) return false;
	if (!ExpectTimeline(hasVisibleRedFade,
		"DemoHall impact Timeline red fade is missing or visually negligible")) return false;
	const auto fadeAt = [&](Vans::VansTimelineTick tick)
	{
		const auto value = fadeWeight
			? Vans::VansTimelineEvaluator::SampleChannel(*fadeWeight, tick) : std::nullopt;
		const auto* number = value ? std::get_if<float>(&*value) : nullptr;
		return number ? *number : -1.0f;
	};
	const auto shakeAt = [](const Vans::VansTimelineChannel* channel, Vans::VansTimelineTick tick)
	{
		const auto value = channel
			? Vans::VansTimelineEvaluator::SampleChannel(*channel, tick) : std::nullopt;
		const auto* vector = value ? std::get_if<Vans::VansTimelineVec3>(&*value) : nullptr;
		if (!vector) return 0.0;
		return std::sqrt(vector->value[0] * vector->value[0] +
			vector->value[1] * vector->value[1] + vector->value[2] * vector->value[2]);
	};
	if (!ExpectTimeline(fadeAt(0) == 0.0f && fadeAt(4000) >= 0.4f &&
		fadeAt(15000) > fadeAt(42000) && fadeAt(42000) == 0.0f,
		"DemoHall red pulse does not rise, decay, and return to zero")) return false;
	if (!ExpectTimeline(shakeAt(shakePosition, 1500) > 0.02 &&
		shakeAt(shakeRotation, 1500) > 0.2 && shakeAt(shakePosition, 41999) < 0.0001 &&
		shakeAt(shakeRotation, 41999) < 0.0001,
		"DemoHall camera shake does not contain a visible impulse and zero tail")) return false;
	Vans::VansTimelineCompileOptions options;
	options.extensions = &Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!compiled)
	{
		for (const auto& diagnostic : compiled.diagnostics)
			std::cerr << "[TimelineRefactor] DemoHall " << diagnostic.code << ": "
				<< diagnostic.message << '\n';
		return false;
	}
	if (!ExpectTimeline(compiled.timeline->ContentHash() != 0 &&
		compiled.timeline->Tracks(Vans::VansTimelineEvaluationPhase::PostScript).size() == 5 &&
		compiled.timeline->Tracks(Vans::VansTimelineEvaluationPhase::Camera).size() == 3,
		"DemoHall Timeline did not compile its complete two-phase track set")) return false;
	Vans::VansTimelineBindingResolver bindings;
	std::vector<Vans::VansTimelineRuntimeBinding> runtimeBindings;
	std::uint32_t objectIndex = 1;
	for (const Vans::VansTimelineBinding& binding : asset.bindings)
		runtimeBindings.push_back({ Vans::VansMakeStableId<Vans::VansTimelineBindingTag>(binding.id),
			{}, { objectIndex++, 1 }, 1 });
	bindings.SetRuntimeBindings(runtimeBindings);
	Vans::VansTimelineParameterBlock parameters;
	Vans::VansTimelineDiagnostics diagnostics;
	if (!parameters.Initialize(*compiled.timeline, {}, diagnostics)) return false;
	Vans::VansTimelineOutputArena arena;
	std::vector<Vans::VansTimelineEvaluationOutput> postScriptOutputs;
	std::vector<Vans::VansTimelineEvaluationOutput> cameraOutputs;
	const std::vector<Vans::VansTimelineTraversalSegment> firstImpactFrame{ {
		0, 1500, Vans::VansTimelineEvaluationReason::Playback,
		Vans::VansTimelineSeekPolicy::AllEdges, 1, 0, 1, false, true } };
	Vans::VansTimelineEvaluator::Evaluate(*compiled.timeline,
		Vans::VansTimelineEvaluationPhase::PostScript, firstImpactFrame,
		parameters, bindings, { 0, 1 }, { 0, 1 }, 0, arena, postScriptOutputs, diagnostics);
	Vans::VansTimelineEvaluator::Evaluate(*compiled.timeline,
		Vans::VansTimelineEvaluationPhase::Camera, firstImpactFrame,
		parameters, bindings, { 0, 1 }, { 0, 1 }, 0, arena, cameraOutputs, diagnostics);
	if (!ExpectTimeline(postScriptOutputs.size() == 5 && cameraOutputs.size() == 2 &&
		!Vans::VansTimelineValidator::HasErrors(diagnostics),
		"DemoHall first impact frame did not dispatch every active PostScript and Camera output")) return false;

	const fs::path scenePath = workspace / "DemoHallProject" / "Scenes" / "DemoHall.json";
	std::ifstream sceneInput(scenePath);
	nlohmann::json sceneJson;
	if (!sceneInput || !(sceneInput >> sceneJson)) return false;
	bool virtualCameraHasOnlyTransform = false;
	bool virtualCameraHasCorrectBasePose = false;
	bool characterHasExpectedPreTransform = false;
	bool timelineComponentReferencesAsset = false;
	for (const auto& object : sceneJson.value("entities", nlohmann::json::array()))
	{
		const std::string name = object.value("name", "");
		if (name == "GlassBreakImpactVirtualCamera")
		{
			const auto components = object.value("components", nlohmann::json::array());
			virtualCameraHasOnlyTransform = components.size() == 1 &&
				components.front().value("type", "") == "Transform";
			if (virtualCameraHasOnlyTransform)
			{
				const auto& data = components.front()["data"];
				const auto position = data.value("position", std::vector<double>{});
				const auto rotation = data.value("rotation", std::vector<double>{});
				virtualCameraHasCorrectBasePose = position.size() == 3 && rotation.size() == 4 &&
					std::abs(position[0] + 3.55) < 0.0001 &&
					std::abs(position[1] - 1.55) < 0.0001 &&
					std::abs(position[2] + 0.8) < 0.0001 &&
					std::abs(rotation[3] - 0.739703823562593) < 0.0001;
			}
		}
		if (name == "AnimatedCharacter")
			for (const auto& component : object.value("components", nlohmann::json::array()))
				if (component.value("type", "") == "Transform")
				{
					const auto& data = component["data"];
					const auto rotation = data.value("rotation", std::vector<double>{});
					const auto scale = data.value("scale", std::vector<double>{});
					characterHasExpectedPreTransform = rotation.size() == 4 && scale.size() == 3 &&
						std::abs(rotation[0] + 0.7071067811865475) < 0.0001 &&
						std::abs(rotation[3] - 0.7071067811865476) < 0.0001 &&
						std::abs(scale[0] - 0.01) < 0.0001;
				}
		if (name == "GlassBreakImpactTimeline")
			for (const auto& component : object.value("components", nlohmann::json::array()))
				if (component.value("type", "") == "Timeline")
					timelineComponentReferencesAsset = component["data"]["timeline"].value("guid", "") ==
						"8d2df4b5-3c7e-4c69-9f76-9b6e63fac850";
	}
	return ExpectTimeline(virtualCameraHasOnlyTransform && virtualCameraHasCorrectBasePose &&
		characterHasExpectedPreTransform && timelineComponentReferencesAsset,
		"DemoHall scene camera pose, character pre-transform, or impact Timeline binding is invalid");
}

bool TestTimelineCompileEvaluateContract()
{
	Vans::VansTimelineAsset asset = MakeProbeAsset();
	Vans::VansTimelineCompileOptions options;
	options.extensions = &Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	const Vans::VansTimelineCompileResult compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!ExpectTimeline(static_cast<bool>(compiled), compiled.diagnostics.empty()
		? "compile failed" : compiled.diagnostics.front().message.c_str())) return false;
	if (!ExpectTimeline(compiled.timeline->ContentHash() != 0 && compiled.timeline->RegistryManifestHash() != 0,
		"compiled Timeline does not carry stable manifests")) return false;
	Vans::VansTimelineBindingResolver bindings;
	bindings.SetRuntimeBindings({ { Vans::VansMakeStableId<Vans::VansTimelineBindingTag>("probe-binding"),
		{}, { 0, 1 }, 1 } });
	Vans::VansTimelineParameterBlock parameters;
	Vans::VansTimelineDiagnostics diagnostics;
	if (!parameters.Initialize(*compiled.timeline, {}, diagnostics)) return false;
	Vans::VansTimelineOutputArena arena;
	std::vector<Vans::VansTimelineEvaluationOutput> outputs;
	Vans::VansTimelineEvaluator::Evaluate(*compiled.timeline, Vans::VansTimelineEvaluationPhase::PostScript,
		{ { 0, 500, Vans::VansTimelineEvaluationReason::Playback,
			Vans::VansTimelineSeekPolicy::AllEdges, 1, 0, 1, false } }, parameters, bindings,
		{ 0, 1 }, { 0, 1 }, 0, arena, outputs, diagnostics);
	return ExpectTimeline(outputs.size() == 1 &&
		outputs.front().payload.As<Vans::VansTimelineSampleOutput>() != nullptr,
		"registry evaluator did not emit a typed output view");
}

bool TestTimelineGenericExtensionContract()
{
	Vans::VansTimelineTrackExtensionRegistry synthetic;
	Vans::VansTimelineTrackExtensionDescriptor parameterCurve;
	parameterCurve.stableName = "Test.ParameterCurve";
	parameterCurve.typeId = Vans::VansMakeStableId<Vans::VansTimelineTrackTypeTag>(
		parameterCurve.stableName);
	parameterCurve.displayName = "Parameter Curve";
	parameterCurve.category = "Test";
	parameterCurve.flags = Vans::VansTimelineContinuousTrackFlags();
	parameterCurve.binding = Vans::VansTimelineBindingRequirement::Required;
	parameterCurve.sourceSchema = {
		{ { Vans::VansMakeStableId<Vans::VansTimelineFieldTag>("parameterId"),
			"parameterId", Vans::VansTimelineValueType::Int64, std::int64_t{}, true, {} } },
		{ Vans::VansMakeTimelineChannelSchema("scale", Vans::VansTimelineValueType::Float, true) },
		false, false };
	parameterCurve.compile = CompileParameterCurve;
	parameterCurve.evaluate = EvaluateParameterCurve;
	parameterCurve.collectDependencies = CollectParameterCurveDependencies;
	parameterCurve.outputs.push_back({ Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		"Test.ParameterCurve.Output"), "Test.ParameterCurve.Output",
		sizeof(ParameterCurveOutput), alignof(ParameterCurveOutput), true });
	std::string error;
	if (!synthetic.Register(parameterCurve, error) || !synthetic.Seal(error)) return false;

	Vans::VansTimelineAsset syntheticAsset;
	syntheticAsset.durationTicks = 20;
	syntheticAsset.playbackRange = { 0, 20 };
	syntheticAsset.workRange = syntheticAsset.playbackRange;
	Vans::VansTimelineParameterDescriptor parameter;
	parameter.id = Vans::VansMakeStableId<Vans::VansTimelineParameterTag>("Test.Strength");
	parameter.name = "Strength";
	parameter.type = Vans::VansTimelineValueType::Float;
	parameter.defaultValue = 2.0f;
	syntheticAsset.parameters.push_back(parameter);
	Vans::VansTimelineBinding syntheticBinding;
	syntheticBinding.id = "synthetic-target";
	syntheticBinding.kind = Vans::VansTimelineBindingKind::RuntimeObject;
	syntheticAsset.bindings.push_back(syntheticBinding);
	Vans::VansTimelineTrack syntheticTrack;
	syntheticTrack.id = "parameter-track";
	syntheticTrack.type = Vans::VansTimelineTrackTypeRef::FromName("Test.ParameterCurve");
	syntheticTrack.bindingId = syntheticBinding.id;
	syntheticTrack.extensionData = Vans::VansSerializedValue::Object({
		{ "parameterId", Vans::VansSerializedValue::Int(
			static_cast<std::int64_t>(parameter.id.value)) } });
	Vans::VansTimelineSection syntheticSection;
	syntheticSection.id = "parameter-section";
	syntheticSection.durationTicks = 20;
	syntheticSection.sourceOutTick = 20;
	Vans::VansTimelineChannel scaleChannel;
	scaleChannel.id = "scale-channel";
	scaleChannel.name = "scale";
	scaleChannel.type = Vans::VansTimelineValueType::Float;
	scaleChannel.keys.push_back({ "scale-key", 0, 3.0f,
		Vans::VansTimelineInterpolation::Constant });
	syntheticSection.channels.push_back(std::move(scaleChannel));
	syntheticTrack.sections.push_back(std::move(syntheticSection));
	syntheticAsset.tracks.push_back(std::move(syntheticTrack));
	Vans::VansTimelineCompileOptions syntheticOptions;
	syntheticOptions.extensions = &synthetic;
	const auto syntheticCompiled = Vans::VansTimelineCompiler::Compile(
		syntheticAsset, syntheticOptions);
	if (!ExpectTimeline(static_cast<bool>(syntheticCompiled),
		"synthetic parameter extension failed compilation")) return false;
	const auto& compiledTrack = syntheticCompiled.timeline->Tracks(
		Vans::VansTimelineEvaluationPhase::PostScript).front();
	Vans::VansTimelineCompiledDataReader syntheticReader(
		syntheticCompiled.timeline->CompiledBytes(), syntheticCompiled.timeline->CompiledValues());
	const auto* compiledParameter = syntheticReader.Read<ParameterCurveCompiled>(
		compiledTrack.extensionData);
	if (!ExpectTimeline(compiledParameter && compiledParameter->parameterSlot == 0,
		"ParameterId was not compiled to a parameter slot")) return false;
	Vans::VansTimelineDiagnostics dependencyDiagnostics;
	const auto dependencies = Vans::VansTimelineDependencyBuilder::CollectDirect(
		syntheticAsset, synthetic, dependencyDiagnostics);
	const auto editorDescriptors = Vans::VansTimelineTrackDescriptorRegistry::Build(synthetic);
	if (!ExpectTimeline(dependencies.size() == 1 &&
		dependencies.front().stableType == "Test.TypedSink" && editorDescriptors.size() == 1 &&
		editorDescriptors.front().stableName == "Test.ParameterCurve" &&
		editorDescriptors.front().supportsChannels,
		"extension dependency or editor metadata was not derived from its registry")) return false;
	auto sink = std::make_shared<ParameterCurveApplier>();
	Vans::VansTimelineApplierRegistry syntheticAppliers;
	if (!syntheticAppliers.Register(sink, error) || !syntheticAppliers.Seal(error)) return false;
	Vans::VansTimelineSessionService syntheticSessions(
		Vans::VansTimelineClockRegistry::BuiltIns(), syntheticAppliers);
	Vans::VansTimelineSessionDesc syntheticDesc;
	syntheticDesc.timeline = syntheticCompiled.timeline;
	syntheticDesc.clockType = std::string(Vans::TimelineClockNames::Manual);
	const Vans::VansGenerationHandle runtimeTarget{ 11, 4 };
	syntheticDesc.runtimeBindings.push_back({
		Vans::VansMakeStableId<Vans::VansTimelineBindingTag>(syntheticBinding.id),
		Vans::VansMakeStableId<Vans::VansRuntimeObjectTypeTag>("Test.Target"), runtimeTarget, 9 });
	const auto syntheticCreated = syntheticSessions.Create(syntheticDesc);
	if (!syntheticCreated || !syntheticSessions.Play(syntheticCreated.handle)) return false;
	syntheticSessions.Advance(syntheticCreated.handle, 1.0 / 60000.0);
	syntheticSessions.Evaluate(syntheticCreated.handle,
		Vans::VansTimelineEvaluationPhase::PostScript);
	if (!ExpectTimeline(sink->targetHandle == runtimeTarget && std::abs(sink->value - 6.0f) < 0.001f,
		"typed parameter slot or injected runtime handle did not reach the external sink")) return false;
	if (!syntheticSessions.Release(syntheticCreated.handle)) return false;

	Vans::VansTimelineTrackExtensionRegistry missingExtension;
	Vans::VansTimelineTrackExtensionDescriptor placeholder;
	placeholder.stableName = "Test.Other";
	placeholder.typeId = Vans::VansMakeStableId<Vans::VansTimelineTrackTypeTag>(placeholder.stableName);
	placeholder.compile = Vans::VansCompileTimelineExtensionSchema;
	placeholder.evaluate = [](Vans::VansTimelineExtensionEvaluationContext&) {};
	if (!missingExtension.Register(std::move(placeholder), error) || !missingExtension.Seal(error)) return false;
	syntheticOptions.extensions = &missingExtension;
	const auto missingResult = Vans::VansTimelineCompiler::Compile(syntheticAsset, syntheticOptions);
	bool stableMissingError = false;
	for (const auto& diagnostic : missingResult.diagnostics)
		if (diagnostic.code == "Timeline.TrackExtensionMissing") stableMissingError = true;
	if (!ExpectTimeline(!missingResult && stableMissingError,
		"removing an extension did not produce a stable compile error")) return false;
	Vans::VansTimelineTrackExtensionRegistry nonReversibleRegistry;
	Vans::VansTimelineTrackExtensionDescriptor nonReversible = parameterCurve;
	nonReversible.stableName = "Test.NonReversible";
	nonReversible.typeId = Vans::VansMakeStableId<Vans::VansTimelineTrackTypeTag>(
		nonReversible.stableName);
	nonReversible.flags = nonReversible.flags &
		static_cast<Vans::VansTimelineTrackFlags>(~static_cast<std::uint32_t>(
			Vans::VansTimelineTrackFlags::Reversible));
	if (!nonReversibleRegistry.Register(std::move(nonReversible), error) ||
		!nonReversibleRegistry.Seal(error)) return false;
	Vans::VansTimelineAsset rollbackAsset = syntheticAsset;
	rollbackAsset.tracks.front().type = Vans::VansTimelineTrackTypeRef::FromName(
		"Test.NonReversible");
	Vans::VansTimelineCompileOptions rollbackOptions;
	rollbackOptions.extensions = &nonReversibleRegistry;
	rollbackOptions.validation.rollbackCapable = true;
	const auto rollback = Vans::VansTimelineCompiler::Compile(rollbackAsset, rollbackOptions);
	bool rollbackBlocked = false;
	for (const auto& diagnostic : rollback.diagnostics)
		if (diagnostic.code == "Timeline.NonReversiblePath") rollbackBlocked = true;
	if (!ExpectTimeline(!rollback && rollbackBlocked,
		"rollback-capable compile did not block a non-reversible extension")) return false;

	Vans::VansTimelineAsset asset;
	asset.durationTicks = 20;
	asset.playbackRange = { 0, 20 };
	asset.workRange = asset.playbackRange;
	Vans::VansTimelineTrack track;
	track.id = "ui-track";
	track.type = Vans::VansTimelineTrackTypeRef::FromName(std::string(Vans::TimelineNames::UIState));
	track.extensionData = Vans::VansSerializedValue::Object({
		{ "screen", Vans::VansSerializedValue::String("HUD") },
		{ "targetKind", Vans::VansSerializedValue::String("Screen") },
		{ "element", Vans::VansSerializedValue::String("") },
		{ "descriptorId", Vans::VansSerializedValue::String("") },
		{ "valueType", Vans::VansSerializedValue::String("Float") } });
	Vans::VansTimelineSection section;
	section.id = "ui-section";
	section.durationTicks = 20;
	section.sourceOutTick = 20;
	section.extensionData = Vans::VansSerializedValue::Object({
		{ "valueType", Vans::VansSerializedValue::String("Int32") } });
	Vans::VansTimelineChannel channel;
	channel.id = "ui-value";
	channel.name = "value";
	channel.type = Vans::VansTimelineValueType::Int32;
	channel.keys.push_back({ "ui-value-key", 0, std::int32_t{ 7 },
		Vans::VansTimelineInterpolation::Constant });
	section.channels.push_back(std::move(channel));
	track.sections.push_back(std::move(section));
	asset.tracks.push_back(std::move(track));
	Vans::VansTimelineCompileOptions options;
	options.extensions = &Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!ExpectTimeline(static_cast<bool>(compiled), compiled.diagnostics.empty()
		? "dynamic-channel compile failed" : compiled.diagnostics.front().message.c_str())) return false;
	const auto& runtimeTrack = compiled.timeline->Tracks(
		Vans::VansTimelineEvaluationPhase::PostScript).front();
	Vans::VansTimelineCompiledDataReader reader(
		compiled.timeline->CompiledBytes(), compiled.timeline->CompiledValues());
	const auto* inheritedScreen = reader.ValueAt(runtimeTrack.sections.front().extensionData, 0);
	const auto* trackType = reader.ValueAt(runtimeTrack.extensionData, 4);
	const auto* sectionType = reader.ValueAt(runtimeTrack.sections.front().extensionData, 4);
	if (!ExpectTimeline(inheritedScreen && std::get<std::string>(*inheritedScreen) == "HUD" &&
		trackType && std::get<std::string>(*trackType) == "Float" && sectionType &&
		std::get<std::string>(*sectionType) == "Int32",
		"section extension override did not preserve inherited fields")) return false;

	Vans::VansTimelineAsset missingChannel = asset;
	missingChannel.tracks.front().sections.front().channels.clear();
	const auto missing = Vans::VansTimelineCompiler::Compile(missingChannel, options);
	bool foundRequiredError = false;
	for (const auto& diagnostic : missing.diagnostics)
		if (diagnostic.code == "Timeline.RequiredChannelMissing") foundRequiredError = true;
	if (!ExpectTimeline(!missing && foundRequiredError,
		"required extension channel was not enforced")) return false;

	Vans::VansTimelineAsset reverseMedia;
	reverseMedia.durationTicks = 20;
	reverseMedia.playbackRange = { 0, 20 };
	reverseMedia.workRange = reverseMedia.playbackRange;
	Vans::VansTimelineBinding mediaBinding;
	mediaBinding.id = "media-binding";
	mediaBinding.targetGuid = "media-target";
	reverseMedia.bindings.push_back(mediaBinding);
	Vans::VansTimelineTrack mediaTrack;
	mediaTrack.id = "media-track";
	mediaTrack.type = Vans::VansTimelineTrackTypeRef::FromName(std::string(Vans::TimelineNames::Media));
	mediaTrack.bindingId = mediaBinding.id;
	mediaTrack.extensionData = Vans::VansSerializedValue::Object({
		{ "syncMode", Vans::VansSerializedValue::String("TimelineClock") } });
	Vans::VansTimelineSection mediaSection;
	mediaSection.id = "media-section";
	mediaSection.durationTicks = 20;
	mediaSection.sourceOutTick = 20;
	mediaSection.reverse = true;
	mediaTrack.sections.push_back(std::move(mediaSection));
	reverseMedia.tracks.push_back(std::move(mediaTrack));
	const auto reverse = Vans::VansTimelineCompiler::Compile(reverseMedia, options);
	bool foundReverseError = false;
	for (const auto& diagnostic : reverse.diagnostics)
		if (diagnostic.code == "Timeline.ReverseUnsupported") foundReverseError = true;
	return ExpectTimeline(!reverse && foundReverseError,
		"unsupported reverse playback was not rejected by the extension contract");
}

bool TestTimelinePointAndRangeContract()
{
	Vans::VansTimelineTrackExtensionRegistry extensions;
	std::string error;
	if (!extensions.Register(Vans::VansMakeTimelinePointExtension(
		"Test.Point", "Point", "Test", Vans::VansTimelineEvaluationPhase::PostScript,
		Vans::VansTimelineBindingRequirement::None, {}), error) || !extensions.Seal(error))
		return false;
	Vans::VansTimelineAsset asset;
	asset.durationTicks = 10;
	asset.playbackRange = { 0, 10 };
	asset.workRange = asset.playbackRange;
	Vans::VansTimelineTrack track;
	track.id = "point-track";
	track.type = Vans::VansTimelineTrackTypeRef::FromName("Test.Point");
	track.extensionData = Vans::VansSerializedValue::Object({});
	Vans::VansTimelineSection section;
	section.id = "point-section";
	section.durationTicks = 1;
	section.sourceOutTick = 1;
	track.sections.push_back(std::move(section));
	asset.tracks.push_back(std::move(track));
	Vans::VansTimelineCompileOptions options;
	options.extensions = &extensions;
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!ExpectTimeline(static_cast<bool>(compiled), "point Timeline failed compilation")) return false;
	auto applier = std::make_shared<ProbePointApplier>();
	applier->type = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>("Test.Point.Output");
	Vans::VansTimelineApplierRegistry appliers;
	if (!appliers.Register(applier, error) || !appliers.Seal(error)) return false;
	Vans::VansTimelineSessionService sessions(Vans::VansTimelineClockRegistry::BuiltIns(), appliers);
	Vans::VansTimelineSessionDesc desc;
	desc.timeline = compiled.timeline;
	desc.clockType = std::string(Vans::TimelineClockNames::Manual);
	const auto created = sessions.Create(desc);
	if (!created || !sessions.ConfigurePlayback(created.handle, 1.0, 1,
		Vans::VansTimelineLoopMode::Loop, 3) || !sessions.Play(created.handle)) return false;
	sessions.Advance(created.handle, 1.0 / 60000.0);
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	if (!ExpectTimeline(applier->applyCount == 1 && applier->releaseCount == 1 &&
		sessions.WriterCount() == 0 && sessions.RestoreTokenCount() == 0,
		"start-tick point output did not fire once and release immediately")) return false;
	sessions.Advance(created.handle, 10.0 / 60000.0);
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	const auto view = sessions.Query(created.handle);
	if (!ExpectTimeline(applier->applyCount == 2 && applier->releaseCount == 2 &&
		view && view->tick == 1,
		"loop-start point output or loop clock remapping is wrong")) return false;

	Vans::VansCompiledTimelineSection rangeSection;
	rangeSection.startTick = 0;
	rangeSection.durationTicks = 10;
	rangeSection.active = true;
	rangeSection.ranges.push_back({ "range", 0, 5, {} });
	Vans::VansTimelineTraversalSegment traversal;
	traversal.previousTick = 0;
	traversal.currentTick = 1;
	traversal.seekPolicy = Vans::VansTimelineSeekPolicy::AllEdges;
	traversal.includesPreviousEndpoint = true;
	const auto crossings = Vans::VansTimelineEvaluator::CrossRanges(rangeSection, traversal);
	if (!ExpectTimeline(crossings.size() == 2 &&
		crossings[0].edge == Vans::VansTimelineRangeEdge::Enter &&
		crossings[1].edge == Vans::VansTimelineRangeEdge::Update,
		"range starting at playback origin did not enter and update")) return false;
	traversal.previousTick = -1;
	traversal.currentTick = 6;
	traversal.includesPreviousEndpoint = false;
	const auto forwardJump = Vans::VansTimelineEvaluator::CrossRanges(rangeSection, traversal);
	if (!ExpectTimeline(forwardJump.size() == 2 &&
		forwardJump[0].edge == Vans::VansTimelineRangeEdge::Enter &&
		forwardJump[1].edge == Vans::VansTimelineRangeEdge::Exit,
		"forward traversal across an entire range did not emit enter/exit")) return false;
	traversal.previousTick = 6;
	traversal.currentTick = -1;
	traversal.playbackDirection = -1;
	const auto reverseJump = Vans::VansTimelineEvaluator::CrossRanges(rangeSection, traversal);
	if (!ExpectTimeline(reverseJump.size() == 2 &&
		reverseJump[0].edge == Vans::VansTimelineRangeEdge::Enter &&
		reverseJump[1].edge == Vans::VansTimelineRangeEdge::Exit,
		"reverse traversal across an entire range did not emit enter/exit")) return false;
	traversal.previousTick = 2;
	traversal.currentTick = 2;
	traversal.playbackDirection = 1;
	traversal.seekPolicy = Vans::VansTimelineSeekPolicy::RebuildActive;
	const auto rebuilt = Vans::VansTimelineEvaluator::CrossRanges(rangeSection, traversal);
	if (!ExpectTimeline(rebuilt.size() == 1 && rebuilt.front().edge == Vans::VansTimelineRangeEdge::Update,
		"range seek rebuild did not emit the active update state")) return false;
	return sessions.Release(created.handle);
}

bool TestTimelineExternalClockContract()
{
	Vans::VansTimelineCompileOptions options;
	options.extensions = &Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	const auto compiled = Vans::VansTimelineCompiler::Compile(MakeProbeAsset(), options);
	if (!compiled) return false;
	auto applier = std::make_shared<ProbeSampleApplier>();
	applier->m_Type = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		std::string(Vans::TimelineNames::Transform) + ".Output");
	Vans::VansTimelineApplierRegistry appliers;
	std::string error;
	if (!appliers.Register(applier, error) || !appliers.Seal(error)) return false;
	auto clock = std::make_shared<Vans::VansTimelineOwnedClockSource>();
	const Vans::VansTimelineClockHandle clockHandle = clock->Create();
	Vans::VansTimelineSessionService sessions(Vans::VansTimelineClockRegistry::BuiltIns(), appliers);
	Vans::VansTimelineSessionDesc desc;
	desc.timeline = compiled.timeline;
	desc.externalClock = clock;
	desc.externalClockHandle = clockHandle;
	desc.runtimeBindings = { { Vans::VansMakeStableId<Vans::VansTimelineBindingTag>("probe-binding"),
		Vans::VansMakeStableId<Vans::VansRuntimeObjectTypeTag>("Test.ExternalClockTarget"),
		{ 3, 5 }, 1 } };
	const auto created = sessions.Create(desc);
	if (!created || !sessions.Play(created.handle)) return false;
	clock->SetAbsolute(clockHandle, 250, false);
	sessions.Advance(created.handle, 1.0);
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	const auto absolute = sessions.Query(created.handle);
	if (!ExpectTimeline(absolute && absolute->tick == 250 && applier->applyCount == 1,
		"external absolute clock did not drive the session")) return false;
	clock->SetAbsolute(clockHandle, 700, true);
	sessions.Advance(created.handle, 1.0);
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	const auto corrected = sessions.Query(created.handle);
	if (!ExpectTimeline(corrected && corrected->tick == 700 && corrected->clockSerial > absolute->clockSerial &&
		applier->applyCount == 2,
		"external clock correction did not rebuild at its absolute tick")) return false;
	if (!sessions.Release(created.handle)) return false;
	return clock->Release(clockHandle);
}

bool TestTimelineSessionContract()
{
	Vans::VansTimelineCompileOptions options;
	options.extensions = &Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	const auto compiled = Vans::VansTimelineCompiler::Compile(MakeProbeAsset(), options);
	if (!compiled) return false;
	auto applier = std::make_shared<ProbeSampleApplier>();
	applier->m_Type = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		std::string(Vans::TimelineNames::Transform) + ".Output");
	Vans::VansTimelineApplierRegistry appliers;
	std::string error;
	if (!appliers.Register(applier, error) || !appliers.Seal(error)) return false;
	Vans::VansTimelineSessionService sessions(Vans::VansTimelineClockRegistry::BuiltIns(), appliers);
	Vans::VansTimelineSessionDesc desc;
	desc.timeline = compiled.timeline;
	desc.kind = Vans::VansTimelineSessionKind::External;
	desc.clockType = std::string(Vans::TimelineClockNames::Manual);
	desc.runtimeBindings = { { Vans::VansMakeStableId<Vans::VansTimelineBindingTag>("probe-binding"),
		{}, { 0, 1 }, 1 } };
	const auto created = sessions.Create(desc);
	if (!ExpectTimeline(static_cast<bool>(created), created.error.c_str())) return false;
	if (!ExpectTimeline(sessions.ConfigurePlayback(created.handle, 1.0, 1,
		Vans::VansTimelineLoopMode::None), "playback configuration failed")) return false;
	if (!ExpectTimeline(sessions.Play(created.handle), "session play failed")) return false;
	sessions.Advance(created.handle, 0.005);
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	if (!(applier->applyCount == 1 && sessions.WriterCount() == 1 && sessions.RestoreTokenCount() == 1))
	{
		std::cerr << "[TimelineRefactor] applyCount=" << applier->applyCount
			<< " writers=" << sessions.WriterCount() << " restore=" << sessions.RestoreTokenCount();
		for (const auto& diagnostic : sessions.Diagnostics())
			std::cerr << " diagnostic=" << diagnostic.code << ':' << diagnostic.message;
		std::cerr << '\n';
		return false;
	}
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::Camera);
	if (!ExpectTimeline(sessions.WriterCount() == 1 && applier->restoreCount == 0,
		"Camera phase released an active PostScript writer")) return false;
	const Vans::VansTimelineSessionHandle stale = created.handle;
	if (!ExpectTimeline(sessions.Release(created.handle), "session release failed")) return false;
	return ExpectTimeline(!sessions.Query(stale) && applier->restoreCount == 1 &&
		sessions.WriterCount() == 0 && sessions.RestoreTokenCount() == 0,
		"generation-safe session release or restore failed");
}

bool TestTimelineSessionFailureTransactionContract()
{
	Vans::VansTimelineAsset asset = MakeProbeAsset();
	Vans::VansTimelineTrack failingTrack = asset.tracks.front();
	failingTrack.id = "failing-probe-track";
	failingTrack.sections.front().id = "failing-probe-section";
	failingTrack.sections.front().channels.front().id = "failing-probe-channel";
	failingTrack.sections.front().channels.front().keys.front().id = "failing-probe-key";
	asset.tracks.push_back(std::move(failingTrack));
	Vans::VansTimelineCompileOptions options;
	options.extensions = &Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!ExpectTimeline(static_cast<bool>(compiled),
		"failure transaction Timeline failed compilation")) return false;
	auto applier = std::make_shared<ProbeSampleApplier>();
	applier->m_Type = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		std::string(Vans::TimelineNames::Transform) + ".Output");
	applier->failTrackId = "failing-probe-track";
	Vans::VansTimelineApplierRegistry appliers;
	std::string error;
	if (!appliers.Register(applier, error) || !appliers.Seal(error)) return false;
	Vans::VansTimelineSessionService sessions(Vans::VansTimelineClockRegistry::BuiltIns(), appliers);
	Vans::VansTimelineSessionDesc desc;
	desc.timeline = compiled.timeline;
	desc.clockType = std::string(Vans::TimelineClockNames::Manual);
	desc.runtimeBindings = { { Vans::VansMakeStableId<Vans::VansTimelineBindingTag>("probe-binding"),
		{}, { 0, 1 }, 1 } };
	const auto created = sessions.Create(desc);
	if (!created || !sessions.Play(created.handle)) return false;
	sessions.Advance(created.handle, 1.0 / 60000.0);
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	const auto failed = sessions.Query(created.handle);
	if (!ExpectTimeline(failed && failed->state == Vans::VansTimelinePlayerState::Error,
		"partial phase failure did not fail its Session")) return false;
	if (!ExpectTimeline(applier->applyCount == 1 && applier->restoreCount == 1 &&
		applier->value == 0.0 && sessions.WriterCount() == 0 && sessions.RestoreTokenCount() == 0,
		"partial phase failure leaked a writer or pre-animated state")) return false;
	return sessions.Release(created.handle);
}

bool TestTimelineStationaryContinuousContract()
{
	Vans::VansTimelineTrackExtensionRegistry extensions;
	std::string error;
	if (!extensions.Register(Vans::VansMakeTimelineSampleExtension(
		"Test.CameraContinuous", "Camera Continuous", "Test",
		Vans::VansTimelineEvaluationPhase::Camera,
		Vans::VansTimelineBindingRequirement::Required,
		Vans::VansTimelineContinuousTrackFlags(),
		{ {}, { Vans::VansMakeTimelineChannelSchema(
			"position", Vans::VansTimelineValueType::Vec3, true) }, false, false }), error) ||
		!extensions.Seal(error)) return false;
	Vans::VansTimelineAsset asset = MakeProbeAsset("Test.CameraContinuous");
	Vans::VansTimelineCompileOptions options;
	options.extensions = &extensions;
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!ExpectTimeline(static_cast<bool>(compiled),
		"stationary continuous Timeline failed compilation")) return false;
	auto applier = std::make_shared<ProbeSampleApplier>();
	applier->m_Type = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		"Test.CameraContinuous.Output");
	Vans::VansTimelineApplierRegistry appliers;
	if (!appliers.Register(applier, error) || !appliers.Seal(error)) return false;
	Vans::VansTimelineSessionService sessions(Vans::VansTimelineClockRegistry::BuiltIns(), appliers);
	Vans::VansTimelineSessionDesc desc;
	desc.timeline = compiled.timeline;
	desc.clockType = std::string(Vans::TimelineClockNames::Manual);
	desc.runtimeBindings = { { Vans::VansMakeStableId<Vans::VansTimelineBindingTag>("probe-binding"),
		{}, { 0, 1 }, 1 } };
	const auto created = sessions.Create(desc);
	if (!created || !sessions.Play(created.handle)) return false;
	const auto evaluateFrame = [&]
	{
		sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
		sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::Camera);
	};
	sessions.Advance(created.handle, 1.0 / 60000.0);
	evaluateFrame();
	sessions.Advance(created.handle, 0.0);
	evaluateFrame();
	if (!sessions.Pause(created.handle)) return false;
	sessions.Advance(created.handle, 1.0);
	evaluateFrame();
	if (!ExpectTimeline(applier->applyCount == 3 && sessions.WriterCount() == 1,
		"continuous Camera output was not resubmitted on zero-delta and paused frames")) return false;
	if (!sessions.Release(created.handle)) return false;
	return ExpectTimeline(applier->restoreCount == 1 && sessions.WriterCount() == 0 &&
		sessions.RestoreTokenCount() == 0,
		"stationary continuous output did not restore on Session release");
}

bool TestTimelineEventContract()
{
	Vans::VansPayloadSchemaRegistry payloads;
	Vans::VansPayloadSchema schema;
	schema.stableName = "Test.TimelinePayload";
	schema.typeId = Vans::VansMakeStableId<Vans::VansTimelinePayloadTypeTag>(schema.stableName);
	Vans::VansPayloadFieldSchema field;
	field.name = "value";
	field.id = Vans::VansMakeStableId<Vans::VansTimelineFieldTag>(field.name);
	field.type = Vans::VansTimelineValueType::Int64;
	field.defaultValue = std::int64_t{};
	field.required = true;
	schema.fields.push_back(field);
	std::string error;
	if (!payloads.Register(std::move(schema), error) || !payloads.Seal(error)) return false;

	Vans::VansTimelineAsset asset;
	asset.durationTicks = 100;
	asset.playbackRange = { 0, 100 };
	asset.workRange = asset.playbackRange;
	asset.markers.push_back({ "marker", 5, "Marker", {}, "Test", true, true, true,
		"EveryCrossing", Vans::VansMakeStableId<Vans::VansTimelinePayloadTypeTag>("Test.TimelinePayload"),
		Vans::VansSerializedValue::Object({ { "value", Vans::VansSerializedValue::Int(5) } }) });
	asset.markers.push_back({ "origin-marker", 0, "Origin", {}, "Test", true, true, true,
		"EveryCrossing", Vans::VansMakeStableId<Vans::VansTimelinePayloadTypeTag>("Test.TimelinePayload"),
		Vans::VansSerializedValue::Object({ { "value", Vans::VansSerializedValue::Int(0) } }) });
	Vans::VansTimelineTrack track;
	track.id = "signal-track";
	track.type = Vans::VansTimelineTrackTypeRef::FromName(std::string(Vans::TimelineNames::EventSignal));
	track.extensionData = Vans::VansSerializedValue::Object({
		{ "signalId", Vans::VansSerializedValue::String("signal") },
		{ "payloadType", Vans::VansSerializedValue::String("Test.TimelinePayload") },
		{ "payload", Vans::VansSerializedValue::Object({ { "value", Vans::VansSerializedValue::Int(7) } }) },
		{ "lane", Vans::VansSerializedValue::String("GameLogic") },
		{ "dispatchTiming", Vans::VansSerializedValue::String("NextFrame") },
		{ "firePolicy", Vans::VansSerializedValue::String("EveryCrossing") },
		{ "editorSafe", Vans::VansSerializedValue::Bool(true) } });
	Vans::VansTimelineSection section;
	section.id = "signal-section";
	section.startTick = 10;
	section.durationTicks = 1;
	section.sourceOutTick = 1;
	track.sections.push_back(std::move(section));
	Vans::VansTimelineSection originSection;
	originSection.id = "origin-signal-section";
	originSection.durationTicks = 1;
	originSection.sourceOutTick = 1;
	track.sections.push_back(std::move(originSection));
	asset.tracks.push_back(std::move(track));
	Vans::VansTimelineCompileOptions options;
	options.extensions = &Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	options.validation.hasPayloadSchema = [&](Vans::VansTimelinePayloadTypeId id) { return payloads.Resolve(id) != nullptr; };
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!ExpectTimeline(static_cast<bool>(compiled), "event Timeline failed compilation")) return false;
	Vans::VansTimelineApplierRegistry appliers;
	if (!appliers.Seal(error)) return false;
	Vans::VansTimelineSessionService sessions(Vans::VansTimelineClockRegistry::BuiltIns(), appliers, &payloads);
	Vans::VansTimelineSessionDesc desc;
	desc.timeline = compiled.timeline;
	desc.clockType = std::string(Vans::TimelineClockNames::Manual);
	const auto created = sessions.Create(desc);
	if (!created || !sessions.Play(created.handle)) return false;
	int markers = 0;
	int signals = 0;
	std::vector<Vans::VansTimelineEventContext> eventContexts;
	auto markerConnection = Vans::VansEventBus::Get().Subscribe<Vans::VansTimelineMarkerReachedEvent>(
		[&](const auto& event) { ++markers; eventContexts.push_back(event.context); },
		Vans::VansEventLane::GameLogic);
	auto signalConnection = Vans::VansEventBus::Get().Subscribe<Vans::VansTimelineSignalFiredEvent>(
		[&](const auto& event) { ++signals; eventContexts.push_back(event.context); },
		Vans::VansEventLane::GameLogic);
	sessions.Advance(created.handle, 12.0 / 60000.0);
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	Vans::VansEventBus::Get().Flush(Vans::VansEventLane::GameLogic);
	if (!ExpectTimeline(markers == 2 && signals == 0,
		"Timeline SameFrame marker or NextFrame signal timing is wrong")) return false;
	Vans::VansEventBus::Get().BeginFrame();
	Vans::VansEventBus::Get().Flush(Vans::VansEventLane::GameLogic);
	if (!ExpectTimeline(signals == 2, "Timeline NextFrame signals were not delivered")) return false;
	std::unordered_set<std::uint64_t> sequences;
	for (const auto& context : eventContexts)
		if (!ExpectTimeline(context.session == created.handle && context.root == created.handle &&
			context.correlation != 0 && sequences.insert(context.sequence).second,
			"typed Timeline event lost session/root/correlation or unique sequence")) return false;
	return sessions.Release(created.handle);
}

bool TestTimelineSubTimelineContract()
{
	Vans::VansTimelineAsset child = MakeProbeAsset();
	child.durationTicks = 50;
	child.playbackRange = { 0, 50 };
	child.workRange = child.playbackRange;
	child.tracks.front().sections.front().durationTicks = 50;
	child.tracks.front().sections.front().sourceOutTick = 50;
	Vans::VansTimelineParameterDescriptor inheritedParameter;
	inheritedParameter.id = Vans::VansMakeStableId<Vans::VansTimelineParameterTag>("Test.Inherited");
	inheritedParameter.name = "Inherited";
	inheritedParameter.type = Vans::VansTimelineValueType::Float;
	inheritedParameter.defaultValue = 1.0f;
	child.parameters.push_back(inheritedParameter);
	child.tracks.front().condition.parameterId = inheritedParameter.id;
	child.tracks.front().condition.expectedValue = 4.0f;
	Vans::VansTimelineAsset root;
	root.durationTicks = 100;
	root.playbackRange = { 0, 100 };
	root.workRange = root.playbackRange;
	root.parameters.push_back(inheritedParameter);
	Vans::VansTimelineTrack track;
	track.id = "child-track";
	track.type = Vans::VansTimelineTrackTypeRef::FromName(std::string(Vans::TimelineNames::SubTimeline));
	track.extensionData = Vans::VansSerializedValue::Object({
		{ "failurePolicy", Vans::VansSerializedValue::String("FailParent") } });
	Vans::VansTimelineSection section;
	section.id = "child-section";
	section.durationTicks = 50;
	section.sourceOutTick = 50;
	section.assetGuid = "child-guid";
	track.sections.push_back(std::move(section));
	root.tracks.push_back(std::move(track));
	Vans::VansTimelineCompileOptions options;
	options.extensions = &Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	options.dependencyLoader = [&](const Vans::VansTimelineDependency&, Vans::VansTimelineAsset& loaded,
		std::string& identity, std::string&) { loaded = child; identity = "child-guid"; return true; };
	const auto compiled = Vans::VansTimelineCompiler::Compile(root, options);
	if (!ExpectTimeline(static_cast<bool>(compiled) && compiled.timeline->ChildTimelines().size() == 1,
		"SubTimeline was not compiled into the root asset")) return false;
	auto applier = std::make_shared<ProbeSampleApplier>();
	applier->m_Type = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		std::string(Vans::TimelineNames::Transform) + ".Output");
	Vans::VansTimelineApplierRegistry appliers;
	std::string error;
	if (!appliers.Register(applier, error) || !appliers.Seal(error)) return false;
	Vans::VansTimelineSessionService sessions(Vans::VansTimelineClockRegistry::BuiltIns(), appliers);
	Vans::VansTimelineSessionDesc desc;
	desc.timeline = compiled.timeline;
	desc.clockType = std::string(Vans::TimelineClockNames::Manual);
	const Vans::VansGenerationHandle injectedTarget{ 17, 6 };
	desc.runtimeBindings.push_back({
		Vans::VansMakeStableId<Vans::VansTimelineBindingTag>("probe-binding"),
		Vans::VansMakeStableId<Vans::VansRuntimeObjectTypeTag>("Test.NestedTarget"),
		injectedTarget, 1 });
	desc.parameterOverrides.push_back({ inheritedParameter.id, 4.0f });
	const auto created = sessions.Create(desc);
	if (!created || !sessions.Play(created.handle)) return false;
	sessions.Advance(created.handle, 10.0 / 60000.0);
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	if (!ExpectTimeline(sessions.SessionCount() == 2 && applier->applyCount == 1 &&
		applier->lastTarget == injectedTarget,
		"SubTimeline did not inherit and apply its runtime binding")) return false;
	if (!sessions.Release(created.handle)) return false;
	if (!ExpectTimeline(sessions.SessionCount() == 0,
		"root release did not propagate to child Session")) return false;

	applier->failApply = true;
	const auto failingRoot = sessions.Create(desc);
	if (!failingRoot || !sessions.Play(failingRoot.handle)) return false;
	sessions.Advance(failingRoot.handle, 10.0 / 60000.0);
	sessions.Evaluate(failingRoot.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	const auto failed = sessions.Query(failingRoot.handle);
	if (!ExpectTimeline(failed && failed->state == Vans::VansTimelinePlayerState::Error,
		"SubTimeline apply failure did not propagate to the parent Session")) return false;
	return sessions.Release(failingRoot.handle);
}

bool TestTimelinePreAnimatedStackContract()
{
	// 完整的堆栈顺序由 Session/Writer 合约覆盖；这里验证空恢复和重复释放保持幂等。
	Vans::VansTimelinePreAnimatedState state;
	Vans::VansTimelineApplierRegistry registry;
	auto applier = std::make_shared<ProbeSampleApplier>();
	applier->m_Type = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>("Test.StackOutput");
	std::string error;
	if (!registry.Register(applier, error) || !registry.Seal(error)) return false;
	state.BindAppliers(&registry);
	const Vans::VansTimelineWriterHandle lower{ 0, 1 };
	const Vans::VansTimelineWriterHandle upper{ 1, 1 };
	const Vans::VansTimelineResourceId resource{ Vans::VansStableHash64("Test.Resource"), 1 };
	auto lowerToken = applier->Capture(lower, 1.0, resource);
	auto upperToken = applier->Capture(upper, 2.0, resource);
	lowerToken.applier = 0;
	upperToken.applier = 0;
	if (!state.Store(lowerToken) || !state.Store(upperToken)) return false;
	if (!ExpectTimeline(!state.ReleaseWriter(lower, true) && applier->value == 2.0 &&
		applier->restoreCount == 0 && applier->deactivateCount == 1 && state.TokenCount() == 2,
		"lower writer restored through an active upper writer")) return false;
	if (!ExpectTimeline(state.ReleaseWriter(upper, true) && applier->value == 0.0 &&
		applier->restoreCount == 2 && applier->deactivateCount == 2 && state.TokenCount() == 0,
		"deferred writer restores did not unwind in reverse order")) return false;
	state.RestoreAll();
	return ExpectTimeline(state.TokenCount() == 0, "pre-animated state is not idempotent");
}

bool TestTimelineTimeContract()
{
	const Vans::VansTimelineTimebase ntsc{ 60000, 30000, 1001 };
	const auto oneMinute = Vans::VansTimelineTime::FrameToTick(1798, ntsc);
	if (!ExpectTimeline(Vans::VansTimelineTime::FormatTimecode(oneMinute, ntsc, true) == "00:00:59;28",
		"drop-frame timecode changed")) return false;
	const auto pingPong = Vans::VansTimelineSectionTimeMapper::Map(
		150, 0, 400, 10, 110, 1.0, false, Vans::VansTimelineLoopMode::PingPong, 4);
	return ExpectTimeline(pingPong.active && pingPong.localTick == 59 && pingPong.reversed,
		"ping-pong section mapping changed");
}
