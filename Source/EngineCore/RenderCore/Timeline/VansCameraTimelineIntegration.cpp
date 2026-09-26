#include "VansCameraTimelineIntegration.h"
#include "VansVirtualCameraParameterStore.h"

#include "../VansCamera.h"
#include "../VansCameraControlArbiter.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include "../../TimelineRuntime/VansTimelineEvaluator.h"
#include "../../TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../../TimelineRuntime/VansTimelineSampleExtension.h"
#include "../../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>

namespace VansGraphics
{
namespace
{
std::uint32_t TransformId(Vans::VansRuntimeWorld& world, Vans::VansEntityHandle entity)
{
	auto* storage = world.FindStorage<Vans::VansRuntimeTransformComponent>(
		Vans::VansRuntimeComponentType_Transform);
	if (!storage || !world.IsAlive(entity)) return UINT32_MAX;
	for (Vans::VansComponentHandle component : world.CollectComponentsOwnedBy(entity))
		if (component.typeId == Vans::VansRuntimeComponentType_Transform)
			if (const auto* transform = storage->Get(component)) return transform->transformStoreId;
	return UINT32_MAX;
}

VansCamera* CameraForEntity(Vans::VansRuntimeWorld& world, Vans::VansEntityHandle entity)
{
	auto* storage = world.FindStorage<Vans::VansRuntimeCameraComponent>(
		Vans::VansRuntimeComponentType_Camera);
	if (!storage || !world.IsAlive(entity)) return nullptr;
	for (Vans::VansComponentHandle component : world.CollectComponentsOwnedBy(entity))
		if (component.typeId == Vans::VansRuntimeComponentType_Camera)
			if (const auto* camera = storage->Get(component)) return camera->camera;
	return nullptr;
}

Vans::VansCameraBlendMode ControlMode(Vans::VansTimelineBlendMode mode, float weight)
{
	return mode == Vans::VansTimelineBlendMode::Additive
		? Vans::VansCameraBlendMode::Additive
		: (mode == Vans::VansTimelineBlendMode::Override
			? (weight >= 1.0f
				? Vans::VansCameraBlendMode::Exclusive
				: Vans::VansCameraBlendMode::Weighted)
			: Vans::VansCameraBlendMode::Weighted);
}

float Number(const Vans::VansTimelineValue& value, float fallback)
{
	if (const auto* number = std::get_if<float>(&value)) return *number;
	if (const auto* number = std::get_if<double>(&value)) return static_cast<float>(*number);
	if (const auto* number = std::get_if<std::int32_t>(&value)) return static_cast<float>(*number);
	if (const auto* number = std::get_if<std::int64_t>(&value)) return static_cast<float>(*number);
	return fallback;
}

float BlendCurveWeight(float alpha, const Vans::VansTimelineStructValue* curve)
{
	alpha = std::clamp(alpha, 0.0f, 1.0f);
	if (!curve) return alpha;
	const std::string shape = Vans::ReadSerializedStringField(curve->value, "shape", "Linear");
	const Vans::VansSerializedValue* exponentValue = Vans::FindObjectField(curve->value, "exponent");
	const float exponent = static_cast<float>(std::max(0.0001,
		exponentValue ? Vans::ReadSerializedNumber(*exponentValue, 1.0) : 1.0));
	if (shape == "SmoothStep") alpha = alpha * alpha * (3.0f - 2.0f * alpha);
	else if (shape == "EaseIn") alpha = std::pow(alpha, exponent);
	else if (shape == "EaseOut") alpha = 1.0f - std::pow(1.0f - alpha, exponent);
	return alpha;
}

Vans::VansCameraContributionOwner TimelineOwner(Vans::VansTimelineWriterHandle writer)
{
	return { VansCameraControlArbiter::TimelineDomain(), writer };
}

Vans::VansCameraContributionRequest TimelineContribution(
	Vans::VansTimelineWriterHandle writer,
	Vans::VansCameraViewSnapshot view,
	Vans::VansCameraContributionKind kind,
	Vans::VansCameraBlendMode blendMode,
	Vans::VansCameraSpace space,
	std::int32_t priority,
	std::uint64_t sequence,
	float weight,
	std::uint32_t channels,
	bool suppressUserLook = false)
{
	Vans::VansCameraContributionRequest contribution;
	contribution.view = Vans::VansCameraRuntime::MainView();
	contribution.owner = TimelineOwner(writer);
	contribution.kind = kind;
	contribution.value = view;
	contribution.blendMode = blendMode;
	contribution.space = space;
	contribution.order.priority = priority;
	contribution.order.stableSequence = sequence;
	contribution.weight = weight;
	contribution.channels = channels;
	contribution.suppressUserLook = suppressUserLook;
	return contribution;
}

struct CameraPropertyRestoreState
{
	Vans::VansTimelineWriterHandle writer;
	Vans::VansEntityHandle entity;
	VansVirtualCameraParameters previous;
	bool hadPrevious = false;
};

class CameraPropertyTimelineApplier final : public Vans::IVansTimelineOutputApplier
{
public:
	CameraPropertyTimelineApplier(Vans::VansRuntimeWorld& world, VansCamera& mainCamera,
		VansCameraControlArbiter& arbiter, VansVirtualCameraParameterStore& virtualCameraParameters)
		: m_World(world), m_MainCamera(mainCamera), m_Arbiter(arbiter),
		  m_VirtualCameraParameters(virtualCameraParameters) {}
	Vans::VansTimelineOutputTypeId OutputType() const override
	{
		return Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
			std::string(Vans::TimelineNames::CameraProperty) + ".Output");
	}
	std::string_view StableName() const override { return "Render.CameraPropertyTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(Vans::VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(Vans::VansTimelineSampleOutput); }
	Vans::VansTimelineApplyResult Apply(const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target, Vans::VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<Vans::VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.section) return { Vans::VansTimelineApplyStatus::Ignored };
		if (!target.entity.IsValid()) return { Vans::VansTimelineApplyStatus::Failed, {},
			"CameraProperty binding has no scene entity" };
		const bool targetsOutputCamera = CameraForEntity(m_World, target.entity) == &m_MainCamera;
		const Vans::VansCameraViewSnapshot outputView = m_MainCamera.CaptureView();
		VansVirtualCameraParameters parameters{
			outputView.lens.fieldOfView, outputView.lens.nearClip, outputView.lens.farClip };
		if (!targetsOutputCamera)
			if (const VansVirtualCameraParameters* existing = m_VirtualCameraParameters.Find(target.entity))
				parameters = *existing;
		std::uint32_t channels = 0;
		for (const Vans::VansTimelineChannel& channel : context.section->channels)
		{
			const auto value = Vans::VansTimelineEvaluator::SampleChannel(channel, sample->localTick);
			if (!value) continue;
			if (channel.name == "fieldOfView")
			{ parameters.fieldOfView = Number(*value, parameters.fieldOfView); channels |= 0x04u; }
			else if (channel.name == "nearClip")
			{ parameters.nearClip = Number(*value, parameters.nearClip); channels |= 0x08u; }
			else if (channel.name == "farClip")
			{ parameters.farClip = Number(*value, parameters.farClip); channels |= 0x10u; }
		}
		if (!channels) return { Vans::VansTimelineApplyStatus::Ignored };
		if (!targetsOutputCamera)
		{
			const VansVirtualCameraParameters* previous = m_VirtualCameraParameters.Find(target.entity);
			const auto [restore, state] = m_State.Acquire(context.writer, [&]
			{ return CameraPropertyRestoreState{ context.writer, target.entity,
				previous ? *previous : VansVirtualCameraParameters{}, previous != nullptr }; });
			(void)state;
			m_VirtualCameraParameters.Set(target.entity, parameters);
			const std::uint64_t instance = (static_cast<std::uint64_t>(target.entity.generation) << 32) |
				target.entity.index;
			return { Vans::VansTimelineApplyStatus::Applied,
				{ restore, {}, {}, { Vans::VansStableHash64("Camera.VirtualParameters"), instance + 1 } } };
		}
		Vans::VansCameraViewSnapshot output = outputView;
		output.lens.fieldOfView = parameters.fieldOfView;
		output.lens.nearClip = parameters.nearClip;
		output.lens.farClip = parameters.farClip;
		const float weight = static_cast<float>(std::clamp(sample->weight, 0.0, 1.0));
		std::string submitError;
		if (!m_Arbiter.Submit(TimelineContribution(
			context.writer,
			output,
			Vans::VansCameraContributionKind::Lens,
			ControlMode(context.blendMode, weight),
			Vans::VansCameraSpace::World,
			VansCameraControlArbiter::TimelinePriority + context.order.priority,
			context.order.sequence,
			weight,
			channels), submitError))
		{
			return { Vans::VansTimelineApplyStatus::Failed, {}, submitError };
		}
		return { Vans::VansTimelineApplyStatus::Applied };
	}
	bool Restore(Vans::VansTimelineRestoreToken token) override
	{
		CameraPropertyRestoreState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		if (state->hadPrevious) m_VirtualCameraParameters.Set(state->entity, state->previous);
		else m_VirtualCameraParameters.Remove(state->entity);
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(Vans::VansTimelineWriterHandle writer) override
	{
		m_Arbiter.Release(TimelineOwner(writer));
		m_State.ReleaseWriter(writer);
	}
	void ReleaseAll() override
	{
		m_Arbiter.ReleaseDomain(VansCameraControlArbiter::TimelineDomain());
		m_State.Clear();
		m_VirtualCameraParameters.Clear();
	}
private:
	Vans::VansRuntimeWorld& m_World;
	VansCamera& m_MainCamera;
	VansCameraControlArbiter& m_Arbiter;
	VansVirtualCameraParameterStore& m_VirtualCameraParameters;
	Vans::VansTimelineModuleApplierState<CameraPropertyRestoreState> m_State;
};

class CameraCutTimelineApplier final : public Vans::IVansTimelineOutputApplier
{
public:
	CameraCutTimelineApplier(Vans::VansRuntimeWorld& world, VansCamera& mainCamera,
		VansCameraControlArbiter& arbiter, VansVirtualCameraParameterStore& virtualCameraParameters)
		: m_World(world), m_MainCamera(mainCamera), m_Arbiter(arbiter),
		  m_VirtualCameraParameters(virtualCameraParameters) {}
	Vans::VansTimelineOutputTypeId OutputType() const override
	{
		return Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
			std::string(Vans::TimelineNames::CameraCut) + ".Output");
	}
	std::string_view StableName() const override { return "Render.CameraCutTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(Vans::VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(Vans::VansTimelineSampleOutput); }
	Vans::VansTimelineApplyResult Apply(const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget&, Vans::VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<Vans::VansTimelineSampleOutput>();
		if (!sample || !sample->active) return { Vans::VansTimelineApplyStatus::Ignored };
		const Vans::VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const auto* sourceValue = reader.ValueAt(context.section->extensionData, 0);
		const auto* sourceName = sourceValue ? std::get_if<std::string>(sourceValue) : nullptr;
		if (!sourceName || sourceName->empty())
			return { Vans::VansTimelineApplyStatus::Failed, {}, "CameraCut requires a source camera binding" };
		if (!context.bindings || !context.diagnostics)
			return { Vans::VansTimelineApplyStatus::Failed, {}, "CameraCut binding resolver is unavailable" };
		const Vans::VansResolvedTimelineTarget* source = context.bindings->Find(
			Vans::VansMakeStableId<Vans::VansTimelineBindingTag>(*sourceName), context.timeline, *context.diagnostics);
		const Vans::VansEntityHandle sourceEntity = source ? source->entity : Vans::VansEntityHandle{};
		const auto* targetValue = reader.ValueAt(context.section->extensionData, 1);
		const auto* targetName = targetValue ? std::get_if<std::string>(targetValue) : nullptr;
		if (targetName && !targetName->empty())
		{
			const Vans::VansResolvedTimelineTarget* target = context.bindings->Find(
				Vans::VansMakeStableId<Vans::VansTimelineBindingTag>(*targetName), context.timeline, *context.diagnostics);
			if (!target || CameraForEntity(m_World, target->entity) != &m_MainCamera)
				return { Vans::VansTimelineApplyStatus::Failed, {}, "CameraCut target is not the scene output camera" };
		}
		const std::uint32_t sourceTransformId = TransformId(m_World, sourceEntity);
		if (!Vans::VansTransformStore::IsAllocated(sourceTransformId))
			return { Vans::VansTimelineApplyStatus::Failed, {}, "CameraCut source Transform is unavailable" };
		Vans::VansCameraViewSnapshot viewSnapshot = m_MainCamera.CaptureView();
		const Vans::VansTransform& sourceTransform = Vans::VansTransformStore::Read(sourceTransformId);
		viewSnapshot.pose.position = sourceTransform.m_Position;
		viewSnapshot.pose.rotationDegrees = sourceTransform.m_Rotation;
		std::uint32_t channels = 0x03u;
		if (const VansVirtualCameraParameters* lens = m_VirtualCameraParameters.Find(sourceEntity))
		{
			viewSnapshot.lens.fieldOfView = lens->fieldOfView;
			viewSnapshot.lens.nearClip = lens->nearClip;
			viewSnapshot.lens.farClip = lens->farClip;
			channels |= 0x1Cu;
		}
		float weight = static_cast<float>(sample->weight);
		const auto* modeValue = reader.ValueAt(context.section->extensionData, 2);
		const auto* mode = modeValue ? std::get_if<std::string>(modeValue) : nullptr;
		const auto* durationValue = reader.ValueAt(context.section->extensionData, 3);
		const auto* curveValue = reader.ValueAt(context.section->extensionData, 4);
		const auto* curve = curveValue ? std::get_if<Vans::VansTimelineStructValue>(curveValue) : nullptr;
		const auto* blendOutDurationValue = reader.ValueAt(context.section->extensionData, 5);
		const auto* blendOutCurveValue = reader.ValueAt(context.section->extensionData, 6);
		const auto* blendOutCurve = blendOutCurveValue
			? std::get_if<Vans::VansTimelineStructValue>(blendOutCurveValue) : nullptr;
		const auto* suppressUserLookValue = reader.ValueAt(context.section->extensionData, 7);
		const Vans::VansTimelineTick blendTicks = durationValue
			? static_cast<Vans::VansTimelineTick>(Number(*durationValue, 0.0f)) : 0;
		const Vans::VansTimelineTick blendOutTicks = blendOutDurationValue
			? static_cast<Vans::VansTimelineTick>(Number(*blendOutDurationValue, 0.0f)) : 0;
		if (mode && *mode == "Blend" && blendTicks > 0)
		{
			const auto elapsedTicks = std::max<Vans::VansTimelineTick>(
				0, sample->timelineTick - context.section->startTick);
			weight *= BlendCurveWeight(static_cast<float>(elapsedTicks) / blendTicks, curve);
		}
		if (mode && *mode == "Blend" && blendOutTicks > 0)
		{
			const auto sectionEndTick = context.section->startTick + context.section->durationTicks;
			const auto remainingTicks = std::max<Vans::VansTimelineTick>(
				0, sectionEndTick - sample->timelineTick);
			weight *= BlendCurveWeight(static_cast<float>(remainingTicks) / blendOutTicks, blendOutCurve);
		}
		const auto* suppressUserLook = suppressUserLookValue
			? std::get_if<bool>(suppressUserLookValue) : nullptr;
		std::string submitError;
		if (!m_Arbiter.Submit(TimelineContribution(
			context.writer,
			viewSnapshot,
			Vans::VansCameraContributionKind::Shot,
			(mode && *mode == "Blend")
				? Vans::VansCameraBlendMode::Weighted
				: Vans::VansCameraBlendMode::Exclusive,
			Vans::VansCameraSpace::World,
			VansCameraControlArbiter::TimelinePriority + context.order.priority,
			context.order.sequence,
			weight,
			channels,
			suppressUserLook && *suppressUserLook), submitError))
		{
			return { Vans::VansTimelineApplyStatus::Failed, {}, submitError };
		}
		return { Vans::VansTimelineApplyStatus::Applied };
	}
	bool Restore(Vans::VansTimelineRestoreToken) override { return true; }
	void ReleaseWriter(Vans::VansTimelineWriterHandle writer) override
	{ m_Arbiter.Release(TimelineOwner(writer)); }
	void ReleaseAll() override
	{ m_Arbiter.ReleaseDomain(VansCameraControlArbiter::TimelineDomain()); }
private:
	Vans::VansRuntimeWorld& m_World;
	VansCamera& m_MainCamera;
	VansCameraControlArbiter& m_Arbiter;
	VansVirtualCameraParameterStore& m_VirtualCameraParameters;
};

class CameraShakeTimelineApplier final : public Vans::IVansTimelineOutputApplier
{
public:
	explicit CameraShakeTimelineApplier(VansCameraControlArbiter& arbiter) : m_Arbiter(arbiter) {}
	Vans::VansTimelineOutputTypeId OutputType() const override
	{
		return Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
			std::string(Vans::TimelineNames::CameraShake) + ".Output");
	}
	std::string_view StableName() const override { return "Render.CameraShakeTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(Vans::VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(Vans::VansTimelineSampleOutput); }
	Vans::VansTimelineApplyResult Apply(const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget&, Vans::VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<Vans::VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.section) return { Vans::VansTimelineApplyStatus::Ignored };
		const Vans::VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		Vans::VansCameraViewSnapshot offset{};
		std::uint32_t channels = 0;
		for (const Vans::VansTimelineChannel& channel : context.section->channels)
			if (const auto value = Vans::VansTimelineEvaluator::SampleChannel(channel, sample->localTick))
			{
				if (channel.name == "positionOffset")
					if (const auto* typed = std::get_if<Vans::VansTimelineVec3>(&*value))
					{ offset.pose.position = { typed->value[0], typed->value[1], typed->value[2] }; channels |= 0x01u; }
				if (channel.name == "rotationOffset")
					if (const auto* typed = std::get_if<Vans::VansTimelineVec3>(&*value))
					{ offset.pose.rotationDegrees = { typed->value[0], typed->value[1], typed->value[2] }; channels |= 0x02u; }
			}
		if (!channels) return { Vans::VansTimelineApplyStatus::Ignored };
		const auto* amplitudeValue = reader.ValueAt(context.section->extensionData, 0);
		const float amplitude = amplitudeValue ? Number(*amplitudeValue, 1.0f) : 1.0f;
		std::string submitError;
		if (!m_Arbiter.Submit(TimelineContribution(
			context.writer,
			offset,
			Vans::VansCameraContributionKind::PoseOffset,
			Vans::VansCameraBlendMode::Additive,
			Vans::VansCameraSpace::CameraLocal,
			VansCameraControlArbiter::TimelinePriority + context.order.priority,
			context.order.sequence,
			static_cast<float>(std::clamp(sample->weight * amplitude, 0.0, 1.0)),
			channels), submitError))
		{
			return { Vans::VansTimelineApplyStatus::Failed, {}, submitError };
		}
		return { Vans::VansTimelineApplyStatus::Applied };
	}
	bool Restore(Vans::VansTimelineRestoreToken) override { return true; }
	void ReleaseWriter(Vans::VansTimelineWriterHandle writer) override
	{ m_Arbiter.Release(TimelineOwner(writer)); }
	void ReleaseAll() override
	{ m_Arbiter.ReleaseDomain(VansCameraControlArbiter::TimelineDomain()); }
private:
	VansCameraControlArbiter& m_Arbiter;
};
}

bool VansRegisterCameraTimelineIntegration(
	Vans::VansRuntimeWorld& world,
	VansCamera& mainCamera,
	VansCameraControlArbiter& arbiter,
	VansVirtualCameraParameterStore& virtualCameraParameters,
	Vans::VansTimelineApplierRegistry& registry,
	std::string& error)
{
	if (!registry.Register(std::make_shared<CameraPropertyTimelineApplier>(
		world, mainCamera, arbiter, virtualCameraParameters), error)) return false;
	if (!registry.Register(std::make_shared<CameraCutTimelineApplier>(
		world, mainCamera, arbiter, virtualCameraParameters), error)) return false;
	return registry.Register(std::make_shared<CameraShakeTimelineApplier>(arbiter), error);
}

bool VansRegisterCameraTimelineExtensions(
	Vans::VansTimelineTrackExtensionRegistry& registry,
	std::string& error)
{
	using F = Vans::VansTimelineValueType;
	using B = Vans::VansTimelineBindingRequirement;
	using P = Vans::VansTimelineEvaluationPhase;
	if (!registry.Register(Vans::VansMakeTimelineSampleExtension(
		Vans::TimelineNames::CameraCut, "Camera Cut", "Cinematic", P::Camera, B::None,
		Vans::VansTimelineContinuousTrackFlags(false),
		{ { Vans::VansMakeTimelineSourceField("cameraBindingId", F::String, std::string(), true),
			Vans::VansMakeTimelineSourceField("targetCameraBindingId", F::String, std::string()),
			Vans::VansMakeTimelineSourceField("cutMode", F::Enum, std::string("Cut"), false,
				{ "Cut", "Blend" }),
			Vans::VansMakeTimelineSourceField("blendDurationTicks", F::Int64, std::int64_t{}),
			Vans::VansMakeTimelineSourceField("blendCurve", F::Struct, Vans::VansTimelineStructValue{}),
			Vans::VansMakeTimelineSourceField("blendOutDurationTicks", F::Int64, std::int64_t{}),
			Vans::VansMakeTimelineSourceField("blendOutCurve", F::Struct, Vans::VansTimelineStructValue{}),
			Vans::VansMakeTimelineSourceField("suppressUserLook", F::Bool, false) },
			{}, false, false }, nullptr), error)) return false;
	if (!registry.Register(Vans::VansMakeTimelineSampleExtension(
		Vans::TimelineNames::CameraProperty, "Camera Property", "Cinematic", P::Camera, B::Required,
		Vans::VansTimelineContinuousTrackFlags(),
		{ {}, { Vans::VansMakeTimelineChannelSchema("fieldOfView", F::Float),
			Vans::VansMakeTimelineChannelSchema("nearClip", F::Float),
			Vans::VansMakeTimelineChannelSchema("farClip", F::Float) }, false, false }, nullptr), error)) return false;
	return registry.Register(Vans::VansMakeTimelineSampleExtension(
		Vans::TimelineNames::CameraShake, "Camera Shake", "Cinematic", P::Camera, B::None,
		Vans::VansTimelineContinuousTrackFlags(),
		{ { Vans::VansMakeTimelineSourceField("amplitudeScale", F::Float, 1.0f) },
			{ Vans::VansMakeTimelineChannelSchema("positionOffset", F::Vec3),
				Vans::VansMakeTimelineChannelSchema("rotationOffset", F::Vec3) }, false, false }, nullptr), error);
}
}
