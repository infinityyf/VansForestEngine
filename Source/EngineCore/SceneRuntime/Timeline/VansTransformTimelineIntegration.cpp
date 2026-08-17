#include "VansTransformTimelineIntegration.h"
#include "VansActivationTimelineIntegration.h"
#include "VansPropertyTimelineIntegration.h"
#include "VansTransformTimelineAccess.h"

#include "../VansRuntimeComponentTypes.h"
#include "../VansRuntimeWorld.h"
#include "../../ScriptCore/VansTransform.h"
#include "../../TimelineRuntime/VansTimelineEvaluator.h"
#include "../../TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../../TimelineRuntime/VansTimelineSampleExtension.h"
#include "../../TimelineCore/VansTimelineTrackExtensionRegistry.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
std::uint32_t ResolveTransformId(VansRuntimeWorld& world, VansEntityHandle entity)
{
	if (!world.IsAlive(entity)) return UINT32_MAX;
	auto* storage = static_cast<VansComponentStorage<VansRuntimeTransformComponent>*>(
		world.FindStorage(VansRuntimeComponentType_Transform));
	if (!storage) return UINT32_MAX;
	for (VansComponentHandle component : world.CollectComponentsOwnedBy(entity))
		if (component.typeId == VansRuntimeComponentType_Transform)
			if (const auto* transform = storage->Get(component)) return transform->transformStoreId;
	return UINT32_MAX;
}

glm::vec3 ToVec3(const VansTimelineVec3& value)
{
	return { static_cast<float>(value.value[0]), static_cast<float>(value.value[1]),
		static_cast<float>(value.value[2]) };
}

glm::quat ToQuaternion(const VansTimelineQuaternion& value)
{
	glm::quat result(static_cast<float>(value.value[3]), static_cast<float>(value.value[0]),
		static_cast<float>(value.value[1]), static_cast<float>(value.value[2]));
	return glm::length(result) > 0.00001f ? glm::normalize(result) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

void DecomposeTransform(const glm::mat4& matrix,
	glm::vec3& position, glm::quat& rotation, glm::vec3& scale)
{
	glm::vec3 skew; glm::vec4 perspective;
	if (!glm::decompose(matrix, scale, rotation, position, skew, perspective))
	{ position = glm::vec3(matrix[3]); rotation = glm::quat(1, 0, 0, 0); scale = glm::vec3(1); }
	rotation = glm::normalize(rotation);
}

std::string String(const VansTimelineCompiledDataReader& reader,
	const VansTimelineCompiledDataView& data, std::size_t slot)
{
	const VansTimelineValue* value = reader.ValueAt(data, slot);
	const auto* typed = value ? std::get_if<std::string>(value) : nullptr;
	return typed ? *typed : std::string{};
}

double Number(const VansTimelineValue* value, double fallback)
{
	if (const auto* typed = value ? std::get_if<float>(value) : nullptr) return *typed;
	if (const auto* typed = value ? std::get_if<double>(value) : nullptr) return *typed;
	if (const auto* typed = value ? std::get_if<std::int32_t>(value) : nullptr) return *typed;
	if (const auto* typed = value ? std::get_if<std::int64_t>(value) : nullptr) return static_cast<double>(*typed);
	return fallback;
}

float ShortestAngleDelta(float fromDegrees, float toDegrees)
{
	float delta = std::fmod(toDegrees - fromDegrees, 360.0f);
	if (delta > 180.0f) delta -= 360.0f;
	if (delta < -180.0f) delta += 360.0f;
	return delta;
}

glm::vec3 BlendEulerDegrees(const glm::vec3& from, const glm::vec3& to, float weight)
{
	return { from.x + ShortestAngleDelta(from.x, to.x) * weight,
		from.y + ShortestAngleDelta(from.y, to.y) * weight,
		from.z + ShortestAngleDelta(from.z, to.z) * weight };
}

struct TransformRestoreState
{
	VansTimelineWriterHandle writer;
	VansEntityHandle entity;
	VansGraphics::VansTransform previous;
};

class TransformTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	TransformTimelineApplier(VansRuntimeWorld& world, std::shared_ptr<IVansTimelineTransformAccess> access)
		: m_World(world), m_Access(std::move(access)) {}
	VansTimelineOutputTypeId OutputType() const override
	{
		return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::Transform) + ".Output");
	}
	std::string_view StableName() const override { return "Scene.TransformTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }

	VansTimelineApplyResult Apply(
		const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target,
		VansTimelineOutputPayloadView view) override
	{
		const VansTimelineSampleOutput* sample = view.As<VansTimelineSampleOutput>();
		if (!sample || !context.section) return { VansTimelineApplyStatus::Failed, {}, "Transform output is invalid" };
		if (!sample->active) return { VansTimelineApplyStatus::Ignored };
		const std::uint32_t transformId = ResolveTransformId(m_World, target.entity);
		if (transformId >= VansGraphics::VansTransformStore::GlobalTransforms.size())
			return { VansTimelineApplyStatus::Failed, {}, "Transform binding has no live Transform component" };
		VansGraphics::VansTransform& transform = VansGraphics::VansTransformStore::GetTransform(transformId);
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const std::string space = String(reader, context.section->extensionData, 0);
		const auto* channelBits = reader.ValueAt(context.section->extensionData, 1);
		const std::uint32_t mask = static_cast<std::uint32_t>(Number(channelBits, 0x3ffu));
		const std::string physicsPolicy = String(reader, context.section->extensionData, 3);
		const auto* scaleValue = reader.ValueAt(context.section->extensionData, 4);
		const bool writeScale = !scaleValue || !std::get_if<bool>(scaleValue) || *std::get_if<bool>(scaleValue);
		std::string accessError;
		if (!m_Access || !m_Access->CanWrite(target, physicsPolicy, accessError))
			return { VansTimelineApplyStatus::Failed, {}, accessError.empty()
				? "Transform Timeline access service is unavailable" : accessError };
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			return TransformRestoreState{ context.writer, target.entity, transform };
		});
		(void)state;
		glm::vec3 sampledPosition = transform.m_Position;
		glm::quat sampledRotation = glm::quat(glm::radians(transform.m_Rotation));
		glm::vec3 sampledScale = transform.m_Scale;
		bool hasPosition = false, hasRotation = false, hasScale = false;
		for (const VansTimelineChannel& channel : context.section->channels)
		{
			const auto value = VansTimelineEvaluator::SampleChannel(channel, sample->localTick);
			if (!value) continue;
			if (channel.name == "position")
				if (const auto* vector = std::get_if<VansTimelineVec3>(&*value))
				{ sampledPosition = ToVec3(*vector); hasPosition = true; }
			if (channel.name == "scale")
				if (const auto* vector = std::get_if<VansTimelineVec3>(&*value))
				{ sampledScale = ToVec3(*vector); hasScale = true; }
			if (channel.name == "rotation")
				if (const auto* rotation = std::get_if<VansTimelineQuaternion>(&*value))
				{ sampledRotation = ToQuaternion(*rotation); hasRotation = true; }
		}
		std::uint32_t origin = UINT32_MAX;
		if (space == "Local") origin = m_Access->ParentTransform(transformId);
		else if (space == "OwnerRelative") origin = ResolveTransformId(m_World, target.rootOwner);
		if (origin < VansGraphics::VansTransformStore::GlobalTransforms.size() && origin != transformId)
		{
			const glm::mat4 local = glm::translate(glm::mat4(1), sampledPosition) *
				glm::mat4_cast(sampledRotation) * glm::scale(glm::mat4(1), sampledScale);
			DecomposeTransform(VansGraphics::VansTransformStore::GetTransform(origin).GetModelMatrix() * local,
				sampledPosition, sampledRotation, sampledScale);
		}
		const VansGraphics::VansTransform base = transform;
		const glm::quat baseRotation = glm::quat(glm::radians(base.m_Rotation));
		if (context.blendMode == VansTimelineBlendMode::Additive || context.blendMode == VansTimelineBlendMode::Relative)
		{ sampledPosition += base.m_Position; sampledRotation = glm::normalize(baseRotation * sampledRotation); sampledScale *= base.m_Scale; }
		else if (context.blendMode == VansTimelineBlendMode::Multiply)
		{ sampledPosition *= base.m_Position; sampledRotation = glm::normalize(baseRotation * sampledRotation); sampledScale *= base.m_Scale; }
		const float weight = static_cast<float>(std::clamp(sample->weight, 0.0, 1.0));
		if (hasPosition)
		{
			const glm::vec3 result = glm::mix(base.m_Position, sampledPosition, weight);
			if (mask & 0x1u) transform.m_Position.x = result.x;
			if (mask & 0x2u) transform.m_Position.y = result.y;
			if (mask & 0x4u) transform.m_Position.z = result.z;
		}
		if (hasRotation && (mask & 0x78u))
			transform.m_Rotation = glm::degrees(glm::eulerAngles(glm::slerp(baseRotation, sampledRotation, weight)));
		if (hasScale && writeScale)
		{
			const glm::vec3 result = glm::mix(base.m_Scale, sampledScale, weight);
			if (mask & 0x80u) transform.m_Scale.x = result.x;
			if (mask & 0x100u) transform.m_Scale.y = result.y;
			if (mask & 0x200u) transform.m_Scale.z = result.z;
		}
		VansGraphics::VansTransformStore::TransformIDToTransformDirty[transformId] = true;
		m_Access->NotifyWritten(transformId);
		const VansTimelineResourceId resource{
			VansStableHash64("Scene.Transform"),
			(static_cast<std::uint64_t>(target.entity.generation) << 32) | (target.entity.index + 1ull) };
		return { VansTimelineApplyStatus::Applied, { restore, {}, {}, resource } };
	}

	bool Restore(VansTimelineRestoreToken token) override
	{
		TransformRestoreState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		const std::uint32_t transformId = ResolveTransformId(m_World, state->entity);
		if (transformId < VansGraphics::VansTransformStore::GlobalTransforms.size())
		{
			VansGraphics::VansTransformStore::GetTransform(transformId) = state->previous;
			VansGraphics::VansTransformStore::TransformIDToTransformDirty[transformId] = true;
			if (m_Access) m_Access->NotifyWritten(transformId);
		}
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }

private:
	VansRuntimeWorld& m_World;
	std::shared_ptr<IVansTimelineTransformAccess> m_Access;
	VansTimelineModuleApplierState<TransformRestoreState> m_State;
};

struct ConstraintRestoreState
{
	VansTimelineWriterHandle writer;
	VansEntityHandle entity;
	VansGraphics::VansTransform previous;
	VansGraphics::VansTransform offset;
};

class ConstraintTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	ConstraintTimelineApplier(VansRuntimeWorld& world, std::shared_ptr<IVansTimelineTransformAccess> access)
		: m_World(world), m_Access(std::move(access)) {}
	VansTimelineOutputTypeId OutputType() const override
	{
		return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::Constraint) + ".Output");
	}
	std::string_view StableName() const override { return "Scene.ConstraintTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.bindings || !context.diagnostics || !context.section)
			return { VansTimelineApplyStatus::Ignored };
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const std::string kind = String(reader, context.section->extensionData, 0);
		const std::string sourceName = String(reader, context.section->extensionData, 1);
		const std::string targetName = String(reader, context.section->extensionData, 2);
		if (sourceName.empty())
			return { VansTimelineApplyStatus::Failed, {}, "LookAt source binding is missing" };
		const VansResolvedTimelineTarget* source = context.bindings->Find(
			VansMakeStableId<VansTimelineBindingTag>(sourceName), context.timeline, *context.diagnostics);
		const VansResolvedTimelineTarget* constrained = targetName.empty() ? &target : context.bindings->Find(
			VansMakeStableId<VansTimelineBindingTag>(targetName), context.timeline, *context.diagnostics);
		if (!source || !constrained)
			return { VansTimelineApplyStatus::Failed, {}, "Constraint source or target binding is unresolved" };
		const std::uint32_t targetId = ResolveTransformId(m_World, constrained->entity);
		const std::uint32_t sourceId = source ? ResolveTransformId(m_World, source->entity) : UINT32_MAX;
		if (targetId >= VansGraphics::VansTransformStore::GlobalTransforms.size() ||
			sourceId >= VansGraphics::VansTransformStore::GlobalTransforms.size())
			return { VansTimelineApplyStatus::Failed, {}, "LookAt bindings have no live Transform" };
		std::string accessError;
		if (!m_Access || !m_Access->CanWrite(*constrained, "RejectDynamicBody", accessError))
			return { VansTimelineApplyStatus::Failed, {}, accessError.empty()
				? "Constraint Timeline access service is unavailable" : accessError };
		VansGraphics::VansTransform& transform = VansGraphics::VansTransformStore::GetTransform(targetId);
		const VansGraphics::VansTransform& sourceTransform = VansGraphics::VansTransformStore::GetTransform(sourceId);
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			ConstraintRestoreState result; result.writer = context.writer;
			result.entity = constrained->entity; result.previous = transform;
			const auto* maintainValue = reader.ValueAt(context.section->extensionData, 3);
			const bool maintain = maintainValue && std::get_if<bool>(maintainValue) && *std::get_if<bool>(maintainValue);
			if (maintain)
			{
				glm::vec3 position, scale; glm::quat rotation;
				VansGraphics::VansTransform sourceCopy = sourceTransform;
				DecomposeTransform(glm::inverse(sourceCopy.GetModelMatrix()) * transform.GetModelMatrix(),
					position, rotation, scale);
				result.offset.m_Position = position;
				result.offset.m_Rotation = glm::degrees(glm::eulerAngles(rotation));
				result.offset.m_Scale = scale;
			}
			else
			{
				if (const auto* value = reader.ValueAt(context.section->extensionData, 4))
					if (const auto* typed = std::get_if<VansTimelineVec3>(value)) result.offset.m_Position = ToVec3(*typed);
				if (const auto* value = reader.ValueAt(context.section->extensionData, 5))
					if (const auto* typed = std::get_if<VansTimelineQuaternion>(value))
						result.offset.m_Rotation = glm::degrees(glm::eulerAngles(ToQuaternion(*typed)));
				if (const auto* value = reader.ValueAt(context.section->extensionData, 6))
					if (const auto* typed = std::get_if<VansTimelineVec3>(value)) result.offset.m_Scale = ToVec3(*typed);
			}
			return result;
		});
		float weight = static_cast<float>(std::clamp(sample->weight, 0.0, 1.0));
		weight *= static_cast<float>(Number(reader.ValueAt(context.section->extensionData, 10), 1.0));
		for (const VansTimelineChannel& channel : context.section->channels)
			if (channel.name == "weight")
				if (const auto sampled = VansTimelineEvaluator::SampleChannel(channel, sample->localTick))
				{
					if (const auto* number = std::get_if<float>(&*sampled)) weight *= *number;
					else if (const auto* number = std::get_if<double>(&*sampled)) weight *= static_cast<float>(*number);
				}
		weight = std::clamp(weight, 0.0f, 1.0f);
		const glm::quat sourceRotation = glm::quat(glm::radians(sourceTransform.m_Rotation));
		const std::string offsetBasis = String(reader, context.section->extensionData, 11);
		const glm::quat basisRotation = offsetBasis == "YawOnly"
			? glm::angleAxis(glm::radians(sourceTransform.m_Rotation.y), glm::vec3(0, 1, 0))
			: (offsetBasis == "World" ? glm::quat(1, 0, 0, 0) : sourceRotation);
		const glm::quat offsetRotation = glm::quat(glm::radians(state->offset.m_Rotation));
		glm::vec3 desiredPosition = sourceTransform.m_Position + basisRotation * state->offset.m_Position;
		glm::quat desiredRotation = glm::normalize(sourceRotation * offsetRotation);
		glm::vec3 desiredScale = sourceTransform.m_Scale * state->offset.m_Scale;
		glm::vec3 desiredCameraEuler(0.0f);
		bool useCameraEuler = false;
		if (kind == "Aim" || kind == "LookAt")
		{
			glm::vec3 lookAtOffset(0.0f);
			if (const auto* value = reader.ValueAt(context.section->extensionData, 12))
				if (const auto* typed = std::get_if<VansTimelineVec3>(value))
					lookAtOffset = ToVec3(*typed);
			const glm::vec3 lookAtPosition = sourceTransform.m_Position + basisRotation * lookAtOffset;
			const glm::vec3 direction = lookAtPosition - transform.m_Position;
			if (glm::dot(direction, direction) > 0.000001f)
			{
				const glm::vec3 normalized = glm::normalize(direction);
				useCameraEuler = String(reader, context.section->extensionData, 13) == "CameraEuler";
				if (useCameraEuler)
				{
					// VansCamera 直接把 Transform 的 X/Y 欧拉角解释为 pitch/yaw，
					// 不能使用面向对象局部轴的 quatLookAt 结果。
					desiredCameraEuler = {
						glm::degrees(std::asin(std::clamp(normalized.y, -1.0f, 1.0f))),
						glm::degrees(std::atan2(normalized.z, normalized.x)), 0.0f };
				}
				else
				{
					const std::string upAxis = String(reader, context.section->extensionData, 8);
					const std::string aimAxis = String(reader, context.section->extensionData, 9);
					const glm::vec3 up = upAxis == "Z" ? glm::vec3(0, 0, 1) :
						(upAxis == "X" ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
					desiredRotation = glm::quatLookAt(normalized, up);
					if (aimAxis == "X") desiredRotation *= glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0));
					else if (aimAxis == "Y") desiredRotation *= glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1, 0, 0));
				}
			}
		}
		const std::uint32_t axisMask = static_cast<std::uint32_t>(Number(
			reader.ValueAt(context.section->extensionData, 7), 0x7));
		auto blendAxes = [&](glm::vec3& current, const glm::vec3& desired)
		{
			for (int axis = 0; axis < 3; ++axis)
				if (axisMask & (1u << axis)) current[axis] = glm::mix(current[axis], desired[axis], weight);
		};
		if (kind == "Parent" || kind == "Position" || kind == "Point") blendAxes(transform.m_Position, desiredPosition);
		if (kind == "Parent" || kind == "Rotation" || kind == "Orient" || kind == "Aim" || kind == "LookAt")
		{
			if (useCameraEuler)
				transform.m_Rotation = BlendEulerDegrees(transform.m_Rotation, desiredCameraEuler, weight);
			else
			{
				const glm::quat current = glm::quat(glm::radians(transform.m_Rotation));
				transform.m_Rotation = glm::degrees(glm::eulerAngles(glm::slerp(current, desiredRotation, weight)));
			}
		}
		if (kind == "Parent" || kind == "Scale") blendAxes(transform.m_Scale, desiredScale);
		VansGraphics::VansTransformStore::TransformIDToTransformDirty[targetId] = true;
		m_Access->NotifyWritten(targetId);
		const VansTimelineResourceId resource{
			VansStableHash64("Scene.Transform"),
			(static_cast<std::uint64_t>(constrained->entity.generation) << 32) | (constrained->entity.index + 1ull) };
		return { VansTimelineApplyStatus::Applied, { restore, {}, {}, resource } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		ConstraintRestoreState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		const std::uint32_t id = ResolveTransformId(m_World, state->entity);
		if (id < VansGraphics::VansTransformStore::GlobalTransforms.size())
		{
			VansGraphics::VansTransformStore::GetTransform(id) = state->previous;
			VansGraphics::VansTransformStore::TransformIDToTransformDirty[id] = true;
			if (m_Access) m_Access->NotifyWritten(id);
		}
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansRuntimeWorld& m_World;
	std::shared_ptr<IVansTimelineTransformAccess> m_Access;
	VansTimelineModuleApplierState<ConstraintRestoreState> m_State;
};
}

bool VansRegisterTransformTimelineIntegration(
	VansRuntimeWorld& world,
	std::shared_ptr<IVansTimelineTransformAccess> access,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	if (!access) { error = "Timeline Transform access service is unavailable"; return false; }
	if (!registry.Register(std::make_shared<TransformTimelineApplier>(world, access), error)) return false;
	return registry.Register(std::make_shared<ConstraintTimelineApplier>(world, std::move(access)), error);
}

bool VansRegisterSceneTimelineExtensions(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error)
{
	using F = VansTimelineValueType;
	const auto post = VansTimelineEvaluationPhase::PostScript;
	const auto required = VansTimelineBindingRequirement::Required;
	if (!registry.Register(VansMakeTimelineSampleExtension(
		TimelineNames::Transform, "Transform", "Object", post, required,
		VansTimelineContinuousTrackFlags(),
		{ { VansMakeTimelineSourceField("space", F::Enum, std::string("Local"), false,
				{ "Local", "World", "OwnerRelative" }),
			VansMakeTimelineSourceField("channels", F::Int32, std::int32_t(0x3ff)),
			VansMakeTimelineSourceField("rotationMode", F::Enum, std::string("QuaternionSlerp"), false,
				{ "QuaternionSlerp" }),
			VansMakeTimelineSourceField("physicsPolicy", F::Enum, std::string("RejectDynamicBody"), false,
				{ "RejectDynamicBody" }),
			VansMakeTimelineSourceField("writeScale", F::Bool, true) },
			{ VansMakeTimelineChannelSchema("position", F::Vec3),
			VansMakeTimelineChannelSchema("rotation", F::Quaternion),
			VansMakeTimelineChannelSchema("scale", F::Vec3) }, false, false }), error)) return false;
	if (!registry.Register(VansMakeTimelineSampleExtension(
		TimelineNames::Constraint, "Constraint", "Object", post, required,
		VansTimelineContinuousTrackFlags(),
		{ { VansMakeTimelineSourceField("constraintType", F::Enum, std::string("Parent"), false,
				{ "Parent", "Position", "Point", "Rotation", "Orient", "Aim", "LookAt", "Scale" }),
			VansMakeTimelineSourceField("sourceBindingId", F::String, std::string(), true),
			VansMakeTimelineSourceField("targetBindingId", F::String, std::string()),
			VansMakeTimelineSourceField("maintainOffset", F::Bool, true),
			VansMakeTimelineSourceField("offsetPosition", F::Vec3, VansTimelineVec3{}),
			VansMakeTimelineSourceField("offsetRotation", F::Quaternion, VansTimelineQuaternion{}),
			VansMakeTimelineSourceField("offsetScale", F::Vec3, VansTimelineVec3{ { 1, 1, 1 } }),
			VansMakeTimelineSourceField("axisMask", F::Int32, std::int32_t(0x7)),
			VansMakeTimelineSourceField("upAxis", F::Enum, std::string("Y"), false, { "X", "Y", "Z" }),
			VansMakeTimelineSourceField("aimAxis", F::Enum, std::string("Z"), false, { "X", "Y", "Z" }),
			VansMakeTimelineSourceField("weight", F::Float, 1.0f),
			VansMakeTimelineSourceField("offsetBasis", F::Enum, std::string("Full"), false,
				{ "Full", "YawOnly", "World" }),
			VansMakeTimelineSourceField("lookAtOffset", F::Vec3, VansTimelineVec3{}),
			VansMakeTimelineSourceField("rotationConvention", F::Enum, std::string("ObjectAxes"), false,
				{ "ObjectAxes", "CameraEuler" }) },
			{ VansMakeTimelineChannelSchema("weight", F::Float) }, false, false }, nullptr), error)) return false;
	if (!VansRegisterActivationTimelineExtension(registry, error)) return false;
	return VansRegisterPropertyTimelineExtension(registry, error);
}
}
