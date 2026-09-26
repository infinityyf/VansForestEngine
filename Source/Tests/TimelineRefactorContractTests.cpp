#include "TimelineRefactorContractTests.h"

#include "../EngineCore/TimelineCore/VansTimelineCompiler.h"
#include "../EngineCore/TimelineCore/VansTimelinePayloadSchemaRegistry.h"
#include "../EngineCore/TimelineCore/VansTimelineSerialization.h"
#include "../EngineCore/TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineApplierRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineBuiltInRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineClockRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineEvaluator.h"
#include "../EngineCore/TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../EngineCore/TimelineRuntime/VansTimelinePreAnimatedState.h"
#include "../EngineCore/TimelineRuntime/VansTimelinePropertyAccessRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineRuntimeSystem.h"
#include "../EngineCore/TimelineRuntime/VansTimelineSessionService.h"
#include "../EngineCore/TimelineRuntime/VansTimelineSampleExtension.h"
#include "../EngineCore/Timeline/VansEngineTimelineRegistry.h"
#include "../EngineCore/TimelineRuntime/Events/VansTimelineRuntimeEvents.h"
#include "../EngineCore/EventCore/VansEventBus.h"
#include "../EngineCore/SceneRuntime/Timeline/VansPropertyTimelineIntegration.h"
#include "../EngineCore/SceneRuntime/Timeline/VansTransformTimelineAccess.h"
#include "../EngineCore/SceneRuntime/VansRuntimeComponentTypes.h"
#include "../EngineCore/SceneRuntime/VansRuntimeWorld.h"
#include "../EngineCore/SceneRuntime/Transform/VansTransformStore.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineTrackDescriptorRegistry.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineEditService.h"
#include "../EngineCore/AssetCore/Storage/VansFileStorage.h"
#include "../EngineCore/AudioCore/Timeline/VansAudioTimelineIntegration.h"
#include "../EngineCore/AudioCore/VansAudioManager.h"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace
{
bool ExpectTimeline(bool value, const char* message)
{
	if (!value) std::cerr << "[TimelineRefactor] " << message << '\n';
	return value;
}

Vans::VansEngineTimelineCatalog TimelineCatalog()
{
	const Vans::VansEngineTimelineCatalog catalog = Vans::VansGetEngineTimelineCatalog();
	if (!catalog) std::cerr << "[TimelineRefactor] " << catalog.error << '\n';
	return catalog;
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
		return { Vans::VansTimelineApplyStatus::Applied, { handle, {}, {}, resource } };
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
	Vans::VansTimelineResourceId resource;
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

class RegistryProbeApplier final : public Vans::IVansTimelineOutputApplier
{
public:
	RegistryProbeApplier(Vans::VansTimelineOutputTypeId type, std::uint32_t size,
		std::uint32_t alignment, std::string name)
		: m_Type(type), m_Size(size), m_Alignment(alignment), m_Name(std::move(name)) {}
	Vans::VansTimelineOutputTypeId OutputType() const override { return m_Type; }
	std::string_view StableName() const override { return m_Name; }
	std::uint32_t PayloadSize() const override { return m_Size; }
	std::uint32_t PayloadAlignment() const override { return m_Alignment; }
	Vans::VansTimelineApplyResult Apply(const Vans::VansTimelineApplyContext&,
		const Vans::VansResolvedTimelineTarget&,
		Vans::VansTimelineOutputPayloadView) override
	{ return { Vans::VansTimelineApplyStatus::Applied }; }
	bool Restore(Vans::VansTimelineRestoreToken) override { return true; }
	void ReleaseWriter(Vans::VansTimelineWriterHandle) override {}
	void ReleaseAll() override {}

private:
	Vans::VansTimelineOutputTypeId m_Type;
	std::uint32_t m_Size = 0;
	std::uint32_t m_Alignment = 0;
	std::string m_Name;
};

class ProbeTransformTimelineAccess final : public Vans::IVansTimelineTransformAccess
{
public:
	std::uint32_t ParentTransform(std::uint32_t) const override { return UINT32_MAX; }
	bool CanWrite(const Vans::VansResolvedTimelineTarget&,
		std::string_view physicsPolicy, std::string& error) const override
	{
		++canWriteCount;
		lastPhysicsPolicy = physicsPolicy;
		if (allowWrite) return true;
		error = "Transform Timeline cannot drive a dynamic rigid body";
		return false;
	}
	void NotifyWritten(std::uint32_t transform) override
	{
		++notifyCount;
		lastTransform = transform;
	}

	bool allowWrite = false;
	mutable int canWriteCount = 0;
	mutable std::string lastPhysicsPolicy;
	int notifyCount = 0;
	std::uint32_t lastTransform = UINT32_MAX;
};

struct EventDispatchMutationProbe
{
	int depth = 0;
};

struct EventDisconnectProbe
{
};
}

bool TestEventDispatchMutationContract()
{
	Vans::VansEventBus& bus = Vans::VansEventBus::Get();
	std::vector<int> trace;
	Vans::VansEventConnection deferred;
	auto high = bus.Subscribe<EventDispatchMutationProbe>(
		[&](const EventDispatchMutationProbe& event)
		{
			trace.push_back(event.depth * 10 + 1);
			if (event.depth != 0)
				return;
			deferred = bus.Subscribe<EventDispatchMutationProbe>(
				[&](const EventDispatchMutationProbe& nested)
				{
					trace.push_back(nested.depth * 10 + 4);
				}, Vans::VansEventLane::GameLogic, 40);
			bus.PublishNow(EventDispatchMutationProbe{ 1 });
		}, Vans::VansEventLane::GameLogic, 30);
	auto middle = bus.Subscribe<EventDispatchMutationProbe>(
		[&](const EventDispatchMutationProbe& event)
		{
			trace.push_back(event.depth * 10 + 2);
		}, Vans::VansEventLane::GameLogic, 20);
	auto low = bus.Subscribe<EventDispatchMutationProbe>(
		[&](const EventDispatchMutationProbe& event)
		{
			trace.push_back(event.depth * 10 + 3);
		}, Vans::VansEventLane::GameLogic, 10);

	bus.PublishNow(EventDispatchMutationProbe{ 0 });
	bus.PublishNow(EventDispatchMutationProbe{ 2 });
	const std::vector<int> expected{
		1, 11, 12, 13, 2, 3,
		24, 21, 22, 23
	};
	if (!ExpectTimeline(trace == expected,
		"EventBus nested dispatch did not preserve the stable subscription set and priority order"))
		return false;

	int disconnectedCalls = 0;
	Vans::VansEventConnection victim = bus.Subscribe<EventDisconnectProbe>(
		[&](const EventDisconnectProbe&) { ++disconnectedCalls; },
		Vans::VansEventLane::GameLogic, 10);
	auto disconnecting = bus.Subscribe<EventDisconnectProbe>(
		[&](const EventDisconnectProbe&) { victim.Disconnect(); },
		Vans::VansEventLane::GameLogic, 20);
	bus.PublishNow(EventDisconnectProbe{});
	return ExpectTimeline(disconnectedCalls == 0,
		"EventBus no longer applies callback disconnection to the active dispatch");
}

bool TestTimelineRegistryContract()
{
	const Vans::VansEngineTimelineCatalog engineCatalog = TimelineCatalog();
	if (!ExpectTimeline(static_cast<bool>(engineCatalog),
		"engine Timeline capability catalog failed to build")) return false;
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
	Vans::VansTimelineAsset previewAsset;
	previewAsset.durationTicks = 1;
	previewAsset.playbackRange = { 0, 1 };
	previewAsset.workRange = previewAsset.playbackRange;
	Vans::VansTimelineTrack previewTrack;
	previewTrack.id = "preview-track";
	previewTrack.type = Vans::VansTimelineTrackTypeRef::FromName("Test.Continuous");
	previewAsset.tracks.push_back(std::move(previewTrack));
	Vans::VansTimelineCompileOptions previewOptions;
	previewOptions.extensions = &registry;
	previewOptions.validation.preview = true;
	previewOptions.validation.requireRuntimeCapabilities = true;
	previewOptions.validation.hasOutputApplier = [](Vans::VansTimelineOutputTypeId)
	{ return false; };
	const auto invalidPreview = Vans::VansTimelineCompiler::Compile(previewAsset, previewOptions);
	const bool previewRejectedMissingApplier = std::any_of(
		invalidPreview.diagnostics.begin(), invalidPreview.diagnostics.end(), [](const auto& diagnostic)
		{ return diagnostic.code == "Timeline.ApplierMissing"; });
	if (!ExpectTimeline(!invalidPreview && previewRejectedMissingApplier,
		"Timeline preview accepted a track without its required runtime applier")) return false;
	const auto& builtIns = *engineCatalog.trackExtensions;
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
	const auto* activation = builtIns.Resolve(Vans::TimelineNames::Activation);
	if (!ExpectTimeline(activation && activation->sourceSchema.fields.size() == 4 &&
		std::none_of(activation->sourceSchema.fields.begin(), activation->sourceSchema.fields.end(),
			[](const Vans::VansTimelineSourceField& field) { return field.name == "useCommandBuffer"; }),
		"Activation schema still exposes the non-functional command-buffer switch")) return false;
	if (!ExpectTimeline(!builtIns.Resolve("Timeline.Spawnable") &&
		!builtIns.Resolve("Timeline.SceneState"),
		"removed no-op Timeline capabilities are still registered")) return false;
	if (!ExpectTimeline(
		engineCatalog.clocks->Resolve(Vans::VansMakeStableId<Vans::VansTimelineClockTag>(
			Vans::TimelineClockNames::GameTime)) != nullptr &&
		engineCatalog.clocks->Resolve(Vans::VansMakeStableId<Vans::VansTimelineClockTag>(
			Vans::TimelineClockNames::Manual)) != nullptr &&
		engineCatalog.clocks->Resolve(Vans::VansMakeStableId<Vans::VansTimelineClockTag>(
			"Timeline.Clock.UnscaledTime")) == nullptr &&
		engineCatalog.clocks->Resolve(Vans::VansMakeStableId<Vans::VansTimelineClockTag>(
			"Timeline.Clock.FixedTick")) == nullptr,
		"engine Timeline catalog exposes unimplemented clock modes")) return false;
	const auto outputType = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>("Test.Output");
	Vans::VansTimelineApplierRegistry missingApplier;
	if (!ExpectTimeline(!Vans::VansValidateEngineTimelineRegistries(
		registry, missingApplier, error) && error.rfind("Timeline.ApplierMissing", 0) == 0,
		"Timeline registry pair accepted a required output without an applier")) return false;

	Vans::VansTimelineApplierRegistry matchingApplier;
	if (!matchingApplier.Register(std::make_shared<RegistryProbeApplier>(
		outputType, static_cast<std::uint32_t>(sizeof(std::uint32_t)),
		static_cast<std::uint32_t>(alignof(std::uint32_t)), "Test.OutputApplier"), error))
		return false;
	if (!ExpectTimeline(Vans::VansValidateEngineTimelineRegistries(
		registry, matchingApplier, error), error.c_str())) return false;

	Vans::VansTimelineApplierRegistry extraApplier;
	const auto extraType = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>("Test.ExtraOutput");
	if (!extraApplier.Register(std::make_shared<RegistryProbeApplier>(
		outputType, static_cast<std::uint32_t>(sizeof(std::uint32_t)),
		static_cast<std::uint32_t>(alignof(std::uint32_t)), "Test.OutputApplier"), error) ||
		!extraApplier.Register(std::make_shared<RegistryProbeApplier>(
		extraType, static_cast<std::uint32_t>(sizeof(std::uint32_t)),
		static_cast<std::uint32_t>(alignof(std::uint32_t)), "Test.ExtraApplier"), error))
		return false;
	if (!ExpectTimeline(!Vans::VansValidateEngineTimelineRegistries(
		registry, extraApplier, error) && error.rfind("Timeline.TrackOutputMissing", 0) == 0,
		"Timeline registry pair did not reject an unmatched registration set")) return false;

	Vans::VansTimelineApplierRegistry emptyAppliers;
	if (!ExpectTimeline(!emptyAppliers.Seal(false, error) &&
		error == "Timeline.ApplierRegistryEmpty",
		"engine applier registry accepted an implicit empty table")) return false;
	if (!ExpectTimeline(emptyAppliers.Seal(true, error) && emptyAppliers.ManifestHash() != 0,
		"explicit event-only applier registry could not be sealed")) return false;
	auto emptyRuntimeAppliers = std::make_shared<Vans::VansTimelineApplierRegistry>();
	if (!emptyRuntimeAppliers->Seal(true, error)) return false;
	Vans::VansTimelineRuntimeSystem runtime(*engineCatalog.clocks);
	if (!ExpectTimeline(!runtime.SetApplierRegistry(emptyRuntimeAppliers, 1, error) &&
		error == "Timeline.ApplierRegistryEmpty",
		"engine runtime accepted an explicitly empty applier registry")) return false;
	Vans::VansTimelinePropertyAccessRegistry emptyProperties;
	if (!ExpectTimeline(!emptyProperties.Seal(false, error) &&
		error == "Timeline.PropertyAccessRegistryEmpty",
		"property access registry accepted an implicit empty table")) return false;
	Vans::VansTimelinePayloadSchemaRegistry emptyPayloads;
	if (!ExpectTimeline(!emptyPayloads.Seal(false, error) && error == "Event.PayloadRegistryEmpty" &&
		emptyPayloads.Seal(true, error) && emptyPayloads.IsSealed(),
		"payload registry did not preserve its explicit empty-project policy")) return false;
	const Vans::VansTimelinePropertyAccessRegistry& propertyAccess =
		*engineCatalog.propertyAccess;
	if (!ExpectTimeline(propertyAccess.IsSealed() && !propertyAccess.Empty() &&
		propertyAccess.ManifestHash() != 0 && propertyAccess.Descriptors().size() == 15,
		"engine property access registry is unavailable")) return false;
	for (std::string_view name : { "Transform.Position", "Transform.Rotation", "Transform.Scale" })
	{
		const auto* property = propertyAccess.Resolve(name);
		if (!ExpectTimeline(property &&
			property->writeDomain == Vans::VansTimelinePropertyWriteDomain::Transform,
			"Transform property accessor is missing its Transform write domain")) return false;
	}
	Vans::VansTimelinePropertyAccessRegistry transformDomain;
	Vans::VansTimelinePropertyAccessRegistry propertyDomain;
	auto transformDescriptor = *propertyAccess.Resolve("Transform.Position");
	auto propertyDescriptor = transformDescriptor;
	propertyDescriptor.writeDomain = Vans::VansTimelinePropertyWriteDomain::Property;
	if (!transformDomain.Register(std::move(transformDescriptor), error) ||
		!propertyDomain.Register(std::move(propertyDescriptor), error) ||
		!transformDomain.Seal(false, error) || !propertyDomain.Seal(false, error) ||
		!ExpectTimeline(transformDomain.ManifestHash() != propertyDomain.ManifestHash(),
			"property write-domain changes did not invalidate the registry manifest")) return false;
	Vans::VansRuntimeWorld audioWorld;
	VansEngine::VansAudioManager audioManager;
	Vans::VansTimelineApplierRegistry audioAppliers;
	if (!Vans::VansRegisterAudioTimelineIntegration(
		audioWorld, audioManager, audioAppliers, error)) return false;
	const auto audioType = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		std::string(Vans::TimelineNames::Audio) + ".Output");
	Vans::IVansTimelineOutputApplier* audioApplier = audioAppliers.At(audioAppliers.SlotOf(audioType));
	if (!ExpectTimeline(audioApplier && audioApplier->Restore({}),
		"stateless Audio Timeline restore reports a false failure")) return false;
	return true;
}

bool TestTimelinePropertyTransformContract()
{
	Vans::VansTimelineAsset asset = MakeProbeAsset(Vans::TimelineNames::Property);
	auto& track = asset.tracks.front();
	track.extensionData = Vans::VansSerializedValue::Object({
		{ "descriptorId", Vans::VansSerializedValue::String("Transform.Position") },
		{ "componentType", Vans::VansSerializedValue::String("transform") },
		{ "valueType", Vans::VansSerializedValue::String("Vec3") } });
	track.sections.front().channels.front().name = "value";

	Vans::VansTimelineCompileOptions options;
	options.extensions = TimelineCatalog().trackExtensions;
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!ExpectTimeline(static_cast<bool>(compiled), compiled.diagnostics.empty()
		? "Transform property Timeline failed compilation" : compiled.diagnostics.front().message.c_str()))
		return false;

	Vans::VansRuntimeWorld world;
	const Vans::VansEntityHandle entity = world.CreateEntity({ "probe-entity", "Property Transform" });
	const std::uint32_t transformId = Vans::VansTransformStore::Allocate();
	struct TransformLease
	{
		std::uint32_t id;
		~TransformLease() { Vans::VansTransformStore::Release(id); }
	} transformLease{ transformId };
	world.AddComponent(entity, Vans::VansRuntimeComponentType_Transform,
		Vans::VansRuntimeTransformComponent{ transformId });
	Vans::VansTransform transform = Vans::VansTransformStore::Read(transformId);
	transform.m_Position = { 4.0f, 5.0f, 6.0f };
	Vans::VansTransformStore::Write(transformId, transform);

	std::string error;
	const Vans::VansTimelinePropertyAccessRegistry& properties =
		*TimelineCatalog().propertyAccess;
	auto access = std::make_shared<ProbeTransformTimelineAccess>();
	Vans::VansTimelineApplierRegistry appliers;
	if (!Vans::VansRegisterPropertyTimelineIntegration(
		world, properties, access, appliers, error) || !appliers.Seal(false, error)) return false;
	const auto outputType = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		std::string(Vans::TimelineNames::Property) + ".Output");
	Vans::IVansTimelineOutputApplier* applier = appliers.At(appliers.SlotOf(outputType));
	const auto& tracks = compiled.timeline->Tracks(Vans::VansTimelineEvaluationPhase::PostScript);
	if (!applier || tracks.empty() || tracks.front().sections.empty()) return false;
	const auto& compiledTrack = tracks.front();
	const auto& compiledSection = compiledTrack.sections.front();
	Vans::VansTimelineApplyContext context{
		*compiled.timeline, compiledTrack, &compiledSection, { 0, 1 }, { 0, 1 },
		Vans::VansTimelineSessionKind::External, { 0, 1 }, {},
		Vans::VansTimelineBlendMode::Override, Vans::VansTimelineCompletionMode::RestoreState };
	Vans::VansResolvedTimelineTarget target;
	target.entity = entity;
	target.rootOwner = entity;
	target.valid = true;
	Vans::VansTimelineSampleOutput sample;
	sample.localTick = 1;
	sample.weight = 1.0;
	sample.active = true;
	const Vans::VansTimelineOutputPayloadView payload{
		reinterpret_cast<const std::byte*>(&sample), sizeof(sample), alignof(decltype(sample)) };

	const auto rejected = applier->Apply(context, target, payload);
	if (!ExpectTimeline(rejected.status == Vans::VansTimelineApplyStatus::Failed &&
		access->canWriteCount == 1 && access->lastPhysicsPolicy == "RejectDynamicBody" &&
		access->notifyCount == 0 && Vans::VansTransformStore::Read(transformId).m_Position == glm::vec3(4.0f, 5.0f, 6.0f) &&
		!rejected.restore.handle.IsValid(),
		"Transform property bypassed its write gate or mutated state after rejection")) return false;

	access->allowWrite = true;
	const auto accepted = applier->Apply(context, target, payload);
	const Vans::VansTimelineResourceId expectedResource = Vans::VansMakeTimelineTransformResource(entity);
	if (!ExpectTimeline(accepted.status == Vans::VansTimelineApplyStatus::Applied &&
		accepted.restore.handle.IsValid() && accepted.restore.resource == expectedResource &&
		Vans::VansTransformStore::Read(transformId).m_Position == glm::vec3(1.0f, 2.0f, 3.0f) &&
		access->notifyCount == 1 && access->lastTransform == transformId &&
		expectedResource.type == Vans::VansStableHash64("Scene.Transform"),
		"Transform property did not use the shared write notification and resource contract")) return false;
	target.entity = {};
	if (!ExpectTimeline(applier->Restore(accepted.restore) &&
		Vans::VansTransformStore::Read(transformId).m_Position == glm::vec3(4.0f, 5.0f, 6.0f) &&
		access->notifyCount == 2,
		"Transform property restore did not restore state through the shared write contract")) return false;
	return true;
}

bool TestTimelineSerializationContract()
{
	using Json = nlohmann::ordered_json;
	const Json current = {
		{ "assetKind", "Timeline" }, { "durationTicks", 1000 },
		{ "playbackRange", { { "startTick", 0 }, { "endTick", 1000 } } },
		{ "workRange", { { "startTick", 0 }, { "endTick", 1000 } } },
		{ "parameters", Json::array() },
		{ "bindings", Json::array({ {
			{ "id", "component-binding" }, { "kind", "SceneComponent" },
			{ "componentGuid", "component-guid" }, { "componentType", "transform" }
		} }) }, { "groups", Json::array() }, { "markers", Json::array() },
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
		!encoded["tracks"][0].contains("config") &&
		encoded["bindings"][0]["componentType"] == "transform" &&
		!encoded["bindings"][0].contains("componentTypeId"),
		"canonical Timeline did not preserve extension data or stable component type names")) return false;
	Vans::VansTimelineAsset roundTrip;
	if (!ExpectTimeline(Vans::VansTimelineSerialization::Decode(encoded, roundTrip, error), error.c_str())) return false;
	if (!ExpectTimeline(Vans::VansTimelineSerialization::Encode(roundTrip) == encoded,
		"canonical Timeline roundtrip is unstable")) return false;
	Json obsoleteIntegerType = current;
	obsoleteIntegerType["bindings"][0].erase("componentType");
	obsoleteIntegerType["bindings"][0]["componentTypeId"] = Vans::VansRuntimeComponentType_Transform;
	if (!ExpectTimeline(!Vans::VansTimelineSerialization::Decode(obsoleteIntegerType, roundTrip, error),
		"Timeline accepted obsolete integer componentTypeId disk ABI")) return false;
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

bool TestTimelineCompileEvaluateContract()
{
	Vans::VansTimelineAsset asset = MakeProbeAsset();
	Vans::VansTimelineCompileOptions options;
	options.extensions = TimelineCatalog().trackExtensions;
	options.runtimeRegistryManifestHash = 0x123456789abcdef0ull;
	const Vans::VansTimelineCompileResult compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!ExpectTimeline(static_cast<bool>(compiled), compiled.diagnostics.empty()
		? "compile failed" : compiled.diagnostics.front().message.c_str())) return false;
	if (!ExpectTimeline(compiled.timeline->ContentHash() != 0 && compiled.timeline->RegistryManifestHash() != 0,
		"compiled Timeline does not carry stable manifests")) return false;
	Vans::VansTimelineCompileOptions changedRegistry = options;
	changedRegistry.runtimeRegistryManifestHash ^= 0x55aa55aa55aa55aaull;
	const auto recompiled = Vans::VansTimelineCompiler::Compile(asset, changedRegistry);
	if (!ExpectTimeline(recompiled &&
		recompiled.timeline->ContentHash() == compiled.timeline->ContentHash() &&
		recompiled.timeline->RegistryManifestHash() != compiled.timeline->RegistryManifestHash(),
		"runtime registry changes did not invalidate the compiled Timeline identity")) return false;
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
	if (!syntheticAppliers.Register(sink, error) || !syntheticAppliers.Seal(false, error)) return false;
	Vans::VansTimelineSessionService syntheticSessions(
		*TimelineCatalog().clocks, syntheticAppliers);
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
	options.extensions = TimelineCatalog().trackExtensions;
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
	if (!appliers.Register(applier, error) || !appliers.Seal(false, error)) return false;
	Vans::VansTimelineSessionService sessions(*TimelineCatalog().clocks, appliers);
	Vans::VansTimelineSessionDesc desc;
	desc.timeline = compiled.timeline;
	desc.clockType = std::string(Vans::TimelineClockNames::Manual);
	const auto created = sessions.Create(desc);
	if (!created || !sessions.ConfigurePlayback(created.handle, 1.0, 1,
		Vans::VansTimelineLoopMode::Loop, 3) || !sessions.Play(created.handle)) return false;
	sessions.Advance(created.handle, 1.0 / 60000.0);
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	if (!ExpectTimeline(applier->applyCount == 1 && applier->releaseCount == 1,
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
	options.extensions = TimelineCatalog().trackExtensions;
	const auto compiled = Vans::VansTimelineCompiler::Compile(MakeProbeAsset(), options);
	if (!compiled) return false;
	auto applier = std::make_shared<ProbeSampleApplier>();
	applier->m_Type = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		std::string(Vans::TimelineNames::Transform) + ".Output");
	Vans::VansTimelineApplierRegistry appliers;
	std::string error;
	if (!appliers.Register(applier, error) || !appliers.Seal(false, error)) return false;
	auto clock = std::make_shared<Vans::VansTimelineOwnedClockSource>();
	const Vans::VansTimelineClockHandle clockHandle = clock->Create();
	Vans::VansTimelineSessionService sessions(*TimelineCatalog().clocks, appliers);
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
	options.extensions = TimelineCatalog().trackExtensions;
	const auto compiled = Vans::VansTimelineCompiler::Compile(MakeProbeAsset(), options);
	if (!compiled) return false;
	auto applier = std::make_shared<ProbeSampleApplier>();
	applier->m_Type = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		std::string(Vans::TimelineNames::Transform) + ".Output");
	Vans::VansTimelineApplierRegistry appliers;
	std::string error;
	if (!appliers.Register(applier, error) || !appliers.Seal(false, error)) return false;
	Vans::VansTimelineSessionService sessions(*TimelineCatalog().clocks, appliers);
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
	if (!(applier->applyCount == 1 && applier->restoreCount == 0 && applier->value == 1.0))
	{
		std::cerr << "[TimelineRefactor] applyCount=" << applier->applyCount
			<< " restoreCount=" << applier->restoreCount << " value=" << applier->value;
		for (const auto& diagnostic : sessions.Diagnostics())
			std::cerr << " diagnostic=" << diagnostic.code << ':' << diagnostic.message;
		std::cerr << '\n';
		return false;
	}
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::Camera);
	if (!ExpectTimeline(applier->restoreCount == 0 && applier->value == 1.0,
		"Camera phase released an active PostScript writer")) return false;
	const Vans::VansTimelineSessionHandle stale = created.handle;
	if (!ExpectTimeline(sessions.Release(created.handle), "session release failed")) return false;
	return ExpectTimeline(!sessions.Query(stale) && applier->restoreCount == 1 && applier->value == 0.0,
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
	options.extensions = TimelineCatalog().trackExtensions;
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!ExpectTimeline(static_cast<bool>(compiled),
		"failure transaction Timeline failed compilation")) return false;
	auto applier = std::make_shared<ProbeSampleApplier>();
	applier->m_Type = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		std::string(Vans::TimelineNames::Transform) + ".Output");
	applier->failTrackId = "failing-probe-track";
	Vans::VansTimelineApplierRegistry appliers;
	std::string error;
	if (!appliers.Register(applier, error) || !appliers.Seal(false, error)) return false;
	Vans::VansTimelineSessionService sessions(*TimelineCatalog().clocks, appliers);
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
		applier->value == 0.0,
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
	if (!appliers.Register(applier, error) || !appliers.Seal(false, error)) return false;
	Vans::VansTimelineSessionService sessions(*TimelineCatalog().clocks, appliers);
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
	if (!ExpectTimeline(applier->applyCount == 3 && applier->restoreCount == 0,
		"continuous Camera output was not resubmitted on zero-delta and paused frames")) return false;
	if (!sessions.Release(created.handle)) return false;
	return ExpectTimeline(applier->restoreCount == 1 && applier->value == 0.0,
		"stationary continuous output did not restore on Session release");
}

bool TestTimelineEventContract()
{
	auto payloads = std::make_shared<Vans::VansTimelinePayloadSchemaRegistry>();
	Vans::VansTimelinePayloadSchema schema;
	schema.stableName = "Test.TimelinePayload";
	schema.typeId = Vans::VansMakeStableId<Vans::VansTimelinePayloadTypeTag>(schema.stableName);
	Vans::VansTimelinePayloadFieldSchema field;
	field.name = "value";
	field.id = Vans::VansMakeStableId<Vans::VansTimelineFieldTag>(field.name);
	field.type = Vans::VansTimelineValueType::Int64;
	field.required = true;
	schema.fields.push_back(field);
	std::string error;
	if (!payloads->Register(std::move(schema), error) || !payloads->Seal(false, error)) return false;
	Vans::VansTimelineRuntimeSystem payloadRuntime(*TimelineCatalog().clocks);
	if (!payloadRuntime.SetPayloadSchemaRegistry(payloads, error)) return false;

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
	options.extensions = TimelineCatalog().trackExtensions;
	options.validation.hasPayloadSchema = [&](Vans::VansTimelinePayloadTypeId id)
	{ return payloadRuntime.HasPayloadSchema(id); };
	options.validation.validatePayload = [&](Vans::VansTimelinePayloadTypeId id,
		const Vans::VansSerializedValue& payload, std::string& payloadError)
	{ return payloadRuntime.ValidatePayload(id, payload, payloadError); };
	Vans::VansEventLane resolvedLane = Vans::VansEventLane::Editor;
	const std::vector<std::string> expectedSignalLanes = {
		"GameLogic", "Script", "MainThread", "Diagnostics", "RenderPrep"
	};
	if (!ExpectTimeline(Vans::VansTimelineSignalLaneNames() == expectedSignalLanes &&
		Vans::VansResolveTimelineSignalLane("GameLogic", resolvedLane) &&
		resolvedLane == Vans::VansEventLane::GameLogic &&
		!Vans::VansResolveTimelineSignalLane("Editor", resolvedLane),
		"Timeline signal lane catalog exposes an unavailable runtime lane")) return false;
	Vans::VansTimelineAsset editorLaneAsset = asset;
	for (auto& [name, value] : editorLaneAsset.tracks.front().extensionData.objectFields)
		if (name == "lane") value = Vans::VansSerializedValue::String("Editor");
	const auto editorLaneCompile = Vans::VansTimelineCompiler::Compile(editorLaneAsset, options);
	const bool rejectedEditorLane = std::any_of(
		editorLaneCompile.diagnostics.begin(), editorLaneCompile.diagnostics.end(),
		[](const Vans::VansTimelineDiagnostic& diagnostic)
		{
			return diagnostic.code == "Timeline.SourceEnumValueInvalid" &&
				diagnostic.propertyPath == "lane";
		});
	if (!ExpectTimeline(!editorLaneCompile && rejectedEditorLane,
		"Timeline compiler accepted the editor-only Signal lane")) return false;
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!ExpectTimeline(static_cast<bool>(compiled), "event Timeline failed compilation")) return false;
	Vans::VansTimelineApplierRegistry appliers;
	if (!appliers.Seal(true, error)) return false;
	Vans::VansTimelineSessionService sessions(
		*TimelineCatalog().clocks, appliers, payloads.get());
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
	options.extensions = TimelineCatalog().trackExtensions;
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
	if (!appliers.Register(applier, error) || !appliers.Seal(false, error)) return false;
	Vans::VansTimelineSessionService sessions(*TimelineCatalog().clocks, appliers);
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
	if (!ExpectTimeline(applier->applyCount == 1 &&
		applier->lastTarget == injectedTarget,
		"SubTimeline did not inherit and apply its runtime binding")) return false;
	if (!sessions.Release(created.handle)) return false;
	if (!ExpectTimeline(!sessions.Query(created.handle) && applier->restoreCount == 1 &&
		applier->value == 0.0,
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
	if (!registry.Register(applier, error) || !registry.Seal(false, error)) return false;
	state.BindAppliers(&registry);
	const Vans::VansTimelineWriterHandle lower{ 0, 1 };
	const Vans::VansTimelineWriterHandle upper{ 1, 1 };
	const Vans::VansTimelineResourceId resource{ Vans::VansStableHash64("Test.Resource"), 1 };
	auto lowerToken = applier->Capture(lower, 1.0, resource);
	auto upperToken = applier->Capture(upper, 2.0, resource);
	lowerToken.applier = 0;
	upperToken.applier = 0;
	const Vans::VansTimelinePreAnimatedStoreResult lowerStore = state.Store(lowerToken);
	const Vans::VansTimelinePreAnimatedStoreResult upperStore = state.Store(upperToken);
	const Vans::VansTimelinePreAnimatedStoreResult repeatedUpperStore = state.Store(upperToken);
	if (!ExpectTimeline(lowerStore.accepted && !lowerStore.overlappingWriter.IsValid() &&
		upperStore.accepted && upperStore.overlappingWriter == lower &&
		repeatedUpperStore.accepted && !repeatedUpperStore.overlappingWriter.IsValid(),
		"same-resource writer overlap was not reported exactly once")) return false;
	if (!ExpectTimeline(!state.ReleaseWriter(lower, true) && applier->value == 2.0 &&
		applier->restoreCount == 0 && applier->deactivateCount == 1,
		"lower writer restored through an active upper writer")) return false;
	if (!ExpectTimeline(state.ReleaseWriter(upper, true) && applier->value == 0.0 &&
		applier->restoreCount == 2 && applier->deactivateCount == 2,
		"deferred writer restores did not unwind in reverse order")) return false;
	state.RestoreAll();
	if (!ExpectTimeline(applier->value == 0.0 && applier->restoreCount == 2 &&
		applier->deactivateCount == 2, "pre-animated state is not idempotent")) return false;

	Vans::VansTimelineAsset asset = MakeProbeAsset();
	Vans::VansTimelineTrack overlappingTrack = asset.tracks.front();
	overlappingTrack.id = "overlapping-probe-track";
	overlappingTrack.sections.front().id = "overlapping-probe-section";
	overlappingTrack.sections.front().channels.front().id = "overlapping-probe-channel";
	overlappingTrack.sections.front().channels.front().keys.front().id = "overlapping-probe-key";
	asset.tracks.push_back(std::move(overlappingTrack));
	Vans::VansTimelineCompileOptions options;
	options.extensions = TimelineCatalog().trackExtensions;
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!ExpectTimeline(static_cast<bool>(compiled),
		"same-resource overlap Timeline failed compilation")) return false;
	auto overlapApplier = std::make_shared<ProbeSampleApplier>();
	overlapApplier->m_Type = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
		std::string(Vans::TimelineNames::Transform) + ".Output");
	overlapApplier->resource = resource;
	Vans::VansTimelineApplierRegistry overlapAppliers;
	if (!overlapAppliers.Register(overlapApplier, error) || !overlapAppliers.Seal(false, error)) return false;
	Vans::VansTimelineSessionService sessions(
		*TimelineCatalog().clocks, overlapAppliers);
	Vans::VansTimelineSessionDesc desc;
	desc.timeline = compiled.timeline;
	desc.clockType = std::string(Vans::TimelineClockNames::Manual);
	desc.runtimeBindings = { { Vans::VansMakeStableId<Vans::VansTimelineBindingTag>("probe-binding"),
		{}, { 0, 1 }, 1 } };
	const auto created = sessions.Create(desc);
	if (!created || !sessions.Play(created.handle)) return false;
	sessions.Advance(created.handle, 1.0 / 60000.0);
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	sessions.Advance(created.handle, 1.0 / 60000.0);
	sessions.Evaluate(created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
	const std::size_t overlapDiagnostics = std::count_if(
		sessions.Diagnostics().begin(), sessions.Diagnostics().end(),
		[](const Vans::VansTimelineDiagnostic& diagnostic)
		{ return diagnostic.code == "Timeline.ResourceWriterOverlap"; });
	if (!ExpectTimeline(overlapDiagnostics == 1 && sessions.Query(created.handle) &&
		sessions.Query(created.handle)->state == Vans::VansTimelinePlayerState::Playing,
		"same-resource overlap diagnostic repeated or failed its Session")) return false;
	if (!sessions.Release(created.handle)) return false;
	return ExpectTimeline(overlapApplier->restoreCount == 2 && overlapApplier->value == 0.0,
		"same-resource overlap release leaked a writer or restore token");
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
