#include "VansCameraTimelineIntegration.h"
#include "VansVirtualCameraParameterStore.h"

#include "../VansCamera.h"
#include "../VansCameraControlArbiter.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../ScriptCore/VansTransform.h"
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
	auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeTransformComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Transform));
	if (!storage || !world.IsAlive(entity)) return UINT32_MAX;
	for (Vans::VansComponentHandle component : world.CollectComponentsOwnedBy(entity))
		if (component.typeId == Vans::VansRuntimeComponentType_Transform)
			if (const auto* transform = storage->Get(component)) return transform->transformStoreId;
	return UINT32_MAX;
}

VansCamera* CameraForEntity(Vans::VansRuntimeWorld& world, Vans::VansEntityHandle entity)
{
	auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeCameraComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Camera));
	if (!storage || !world.IsAlive(entity)) return nullptr;
	for (Vans::VansComponentHandle component : world.CollectComponentsOwnedBy(entity))
		if (component.typeId == Vans::VansRuntimeComponentType_Camera)
			if (const auto* camera = storage->Get(component)) return camera->camera;
	return nullptr;
}

VansCameraControlMode ControlMode(Vans::VansTimelineBlendMode mode, float weight)
{
	return mode == Vans::VansTimelineBlendMode::Additive
		? VansCameraControlMode::Additive
		: (mode == Vans::VansTimelineBlendMode::Override
			? (weight >= 1.0f ? VansCameraControlMode::Exclusive : VansCameraControlMode::Weighted)
			: VansCameraControlMode::Weighted);
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

VansCameraControlOwner TimelineOwner(Vans::VansTimelineWriterHandle writer)
{
	return { VansCameraControlArbiter::TimelineDomain(), writer };
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
		const VansCameraControlPose outputPose = m_MainCamera.CaptureControlPose();
		VansVirtualCameraParameters parameters{
			outputPose.fieldOfView, outputPose.nearClip, outputPose.farClip };
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
		VansCameraControlPose pose = outputPose;
		pose.fieldOfView = parameters.fieldOfView;
		pose.nearClip = parameters.nearClip;
		pose.farClip = parameters.farClip;
		const float weight = static_cast<float>(std::clamp(sample->weight, 0.0, 1.0));
		m_Arbiter.Submit({ TimelineOwner(context.writer), pose, ControlMode(context.blendMode, weight),
			VansCameraControlSpace::World,
			VansCameraControlArbiter::TimelinePriority + context.order.priority,
			context.order.sequence, weight, channels });
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
		if (sourceTransformId >= VansTransformStore::GlobalTransforms.size())
			return { Vans::VansTimelineApplyStatus::Failed, {}, "CameraCut source Transform is unavailable" };
		VansCameraControlPose pose = m_MainCamera.CaptureControlPose();
		const VansTransform& sourceTransform = VansTransformStore::GetTransform(sourceTransformId);
		pose.position = sourceTransform.m_Position;
		pose.rotationDegrees = sourceTransform.m_Rotation;
		std::uint32_t channels = 0x03u;
		if (const VansVirtualCameraParameters* lens = m_VirtualCameraParameters.Find(sourceEntity))
		{
			pose.fieldOfView = lens->fieldOfView;
			pose.nearClip = lens->nearClip;
			pose.farClip = lens->farClip;
			channels |= 0x1Cu;
		}
		float weight = static_cast<float>(sample->weight);
		const auto* modeValue = reader.ValueAt(context.section->extensionData, 2);
		const auto* mode = modeValue ? std::get_if<std::string>(modeValue) : nullptr;
		const auto* durationValue = reader.ValueAt(context.section->extensionData, 3);
		const auto* curveValue = reader.ValueAt(context.section->extensionData, 4);
		const auto* curve = curveValue ? std::get_if<Vans::VansTimelineStructValue>(curveValue) : nullptr;
		const auto* priorityValue = reader.ValueAt(context.section->extensionData, 5);
		const auto* blendOutDurationValue = reader.ValueAt(context.section->extensionData, 6);
		const auto* blendOutCurveValue = reader.ValueAt(context.section->extensionData, 7);
		const auto* blendOutCurve = blendOutCurveValue
			? std::get_if<Vans::VansTimelineStructValue>(blendOutCurveValue) : nullptr;
		const auto* suppressUserLookValue = reader.ValueAt(context.section->extensionData, 8);
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
		const std::int32_t priority = priorityValue
			? static_cast<std::int32_t>(Number(*priorityValue, static_cast<float>(context.order.priority)))
			: context.order.priority;
		const auto* suppressUserLook = suppressUserLookValue
			? std::get_if<bool>(suppressUserLookValue) : nullptr;
		m_Arbiter.Submit({ TimelineOwner(context.writer), pose,
			(mode && *mode == "Blend") ? VansCameraControlMode::Weighted : VansCameraControlMode::Exclusive,
			VansCameraControlSpace::World,
			VansCameraControlArbiter::TimelinePriority + priority,
			context.order.sequence, weight, channels, suppressUserLook && *suppressUserLook });
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
		VansCameraControlPose pose{};
		std::uint32_t channels = 0;
		for (const Vans::VansTimelineChannel& channel : context.section->channels)
			if (const auto value = Vans::VansTimelineEvaluator::SampleChannel(channel, sample->localTick))
			{
				if (channel.name == "positionOffset")
					if (const auto* typed = std::get_if<Vans::VansTimelineVec3>(&*value))
					{ pose.position = { typed->value[0], typed->value[1], typed->value[2] }; channels |= 0x01u; }
				if (channel.name == "rotationOffset")
					if (const auto* typed = std::get_if<Vans::VansTimelineVec3>(&*value))
					{ pose.rotationDegrees = { typed->value[0], typed->value[1], typed->value[2] }; channels |= 0x02u; }
			}
		if (!channels) return { Vans::VansTimelineApplyStatus::Ignored };
		const auto* amplitudeValue = reader.ValueAt(context.section->extensionData, 0);
		const auto* priorityValue = reader.ValueAt(context.section->extensionData, 1);
		const float amplitude = amplitudeValue ? Number(*amplitudeValue, 1.0f) : 1.0f;
		const std::int32_t priority = priorityValue ? static_cast<std::int32_t>(Number(*priorityValue, 0)) : 0;
		m_Arbiter.Submit({ TimelineOwner(context.writer), pose, VansCameraControlMode::Additive,
			VansCameraControlSpace::CameraLocal,
			VansCameraControlArbiter::TimelinePriority + priority, context.order.sequence,
			static_cast<float>(std::clamp(sample->weight * amplitude, 0.0, 1.0)), channels });
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

bool VansRegisterRenderTimelineExtensions(
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
			Vans::VansMakeTimelineSourceField("priority", F::Int32, std::int32_t{ 100 }),
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
	if (!registry.Register(Vans::VansMakeTimelineSampleExtension(
		Vans::TimelineNames::CameraShake, "Camera Shake", "Cinematic", P::Camera, B::None,
		Vans::VansTimelineContinuousTrackFlags(),
		{ { Vans::VansMakeTimelineSourceField("amplitudeScale", F::Float, 1.0f),
			Vans::VansMakeTimelineSourceField("priority", F::Int32, std::int32_t{}) },
			{ Vans::VansMakeTimelineChannelSchema("positionOffset", F::Vec3),
				Vans::VansMakeTimelineChannelSchema("rotationOffset", F::Vec3) }, false, false }, nullptr), error)) return false;
	auto fadePostProcess = Vans::VansMakeTimelineSampleExtension(
		Vans::TimelineNames::FadePostProcess, "Fade / Post Process", "Cinematic", P::PostScript, B::None,
		Vans::VansTimelineContinuousTrackFlags(),
		{ { Vans::VansMakeTimelineSourceField("mode", F::Enum, std::string("Fade"), false,
				{ "Fade", "PostProcess" }),
			Vans::VansMakeTimelineSourceField("color", F::ColorLinear, Vans::VansTimelineColorLinear{}) },
			{ Vans::VansMakeTimelineChannelSchema("weight", F::Float) }, false, false });
	fadePostProcess.sectionAssetKind = "PostProcessProfile";
	return registry.Register(std::move(fadePostProcess), error);
}
}
