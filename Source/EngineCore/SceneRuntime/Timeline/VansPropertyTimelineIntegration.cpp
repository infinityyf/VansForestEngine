#include "VansPropertyTimelineIntegration.h"

#include "../../TimelineCore/VansTimelineSerialization.h"
#include "../../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../../TimelineCore/VansTimelineValidator.h"
#include "../../TimelineRuntime/VansTimelineEvaluator.h"
#include "../../TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../../TimelineRuntime/VansTimelinePropertyAccessRegistry.h"
#include "../../TimelineRuntime/VansTimelineSampleExtension.h"
#include "../VansRuntimeComponentTypes.h"
#include "../VansRuntimeWorld.h"
#include "../../ScriptCore/VansTransform.h"

#include <cmath>
#include <limits>
#include <type_traits>

namespace Vans
{
namespace
{
struct CompiledPropertyAccess
{
	std::uint64_t descriptorId = 0;
	std::uint16_t componentTypeId = 0;
	std::uint8_t valueType = 0;
};

std::uint32_t ResolveTransformId(const VansTimelinePropertyAccessContext& context)
{
	if (!context.world || !context.world->IsAlive(context.target.entity)) return UINT32_MAX;
	auto* storage = static_cast<VansComponentStorage<VansRuntimeTransformComponent>*>(
		context.world->FindStorage(VansRuntimeComponentType_Transform));
	if (!storage) return UINT32_MAX;
	for (VansComponentHandle component : context.world->CollectComponentsOwnedBy(context.target.entity))
		if (component.typeId == VansRuntimeComponentType_Transform)
			if (const auto* runtime = storage->Get(component)) return runtime->transformStoreId;
	return UINT32_MAX;
}

bool ReadTransformPosition(const VansTimelinePropertyAccessContext& context,
	VansTimelineValue& value, std::string& error)
{
	const std::uint32_t id = ResolveTransformId(context);
	if (id >= VansGraphics::VansTransformStore::GlobalTransforms.size())
	{ error = "Transform.Position requires a live Transform binding"; return false; }
	const auto& position = VansGraphics::VansTransformStore::GetTransform(id).m_Position;
	value = VansTimelineVec3{ { position.x, position.y, position.z } }; return true;
}

bool WriteTransformPosition(const VansTimelinePropertyAccessContext& context,
	const VansTimelineValue& value, std::string& error)
{
	const std::uint32_t id = ResolveTransformId(context); const auto* typed = std::get_if<VansTimelineVec3>(&value);
	if (id >= VansGraphics::VansTransformStore::GlobalTransforms.size() || !typed)
	{ error = "Transform.Position target or value is invalid"; return false; }
	VansGraphics::VansTransformStore::GetTransform(id).m_Position = {
		static_cast<float>(typed->value[0]), static_cast<float>(typed->value[1]), static_cast<float>(typed->value[2]) };
	VansGraphics::VansTransformStore::TransformIDToTransformDirty[id] = true; return true;
}

bool ReadTransformScale(const VansTimelinePropertyAccessContext& context,
	VansTimelineValue& value, std::string& error)
{
	const std::uint32_t id = ResolveTransformId(context);
	if (id >= VansGraphics::VansTransformStore::GlobalTransforms.size())
	{ error = "Transform.Scale requires a live Transform binding"; return false; }
	const auto& scale = VansGraphics::VansTransformStore::GetTransform(id).m_Scale;
	value = VansTimelineVec3{ { scale.x, scale.y, scale.z } }; return true;
}

bool WriteTransformScale(const VansTimelinePropertyAccessContext& context,
	const VansTimelineValue& value, std::string& error)
{
	const std::uint32_t id = ResolveTransformId(context); const auto* typed = std::get_if<VansTimelineVec3>(&value);
	if (id >= VansGraphics::VansTransformStore::GlobalTransforms.size() || !typed)
	{ error = "Transform.Scale target or value is invalid"; return false; }
	VansGraphics::VansTransformStore::GetTransform(id).m_Scale = {
		static_cast<float>(typed->value[0]), static_cast<float>(typed->value[1]), static_cast<float>(typed->value[2]) };
	VansGraphics::VansTransformStore::TransformIDToTransformDirty[id] = true; return true;
}

bool ReadTransformRotation(const VansTimelinePropertyAccessContext& context,
	VansTimelineValue& value, std::string& error)
{
	const std::uint32_t id = ResolveTransformId(context);
	if (id >= VansGraphics::VansTransformStore::GlobalTransforms.size())
	{ error = "Transform.Rotation requires a live Transform binding"; return false; }
	const glm::quat rotation = glm::quat(glm::radians(VansGraphics::VansTransformStore::GetTransform(id).m_Rotation));
	value = VansTimelineQuaternion{ { rotation.x, rotation.y, rotation.z, rotation.w } }; return true;
}

bool WriteTransformRotation(const VansTimelinePropertyAccessContext& context,
	const VansTimelineValue& value, std::string& error)
{
	const std::uint32_t id = ResolveTransformId(context); const auto* typed = std::get_if<VansTimelineQuaternion>(&value);
	if (id >= VansGraphics::VansTransformStore::GlobalTransforms.size() || !typed)
	{ error = "Transform.Rotation target or value is invalid"; return false; }
	glm::quat rotation(static_cast<float>(typed->value[3]), static_cast<float>(typed->value[0]),
		static_cast<float>(typed->value[1]), static_cast<float>(typed->value[2]));
	if (glm::length(rotation) <= 0.00001f) rotation = glm::quat(1, 0, 0, 0);
	VansGraphics::VansTransformStore::GetTransform(id).m_Rotation = glm::degrees(glm::eulerAngles(glm::normalize(rotation)));
	VansGraphics::VansTransformStore::TransformIDToTransformDirty[id] = true; return true;
}

void AddError(VansTimelineDiagnostics& diagnostics, const VansTimelineId& id,
	std::string property, std::string message)
{
	diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
		"Timeline.PropertyDescriptorInvalid", {}, id, std::move(property), std::move(message) });
}

void ValidateProperty(const VansTimelineTrack& track, const VansTimelineSourceSchema& schema,
	const VansTimelineValidationContext&, VansTimelineDiagnostics& diagnostics)
{
	VansValidateTimelineExtensionSchema(track, schema, diagnostics);
	const VansSerializedValue* descriptorValue = VansTimelineFindSourceField(track.extensionData, "descriptorId");
	const VansSerializedValue* componentValue = VansTimelineFindSourceField(track.extensionData, "componentTypeId");
	const VansSerializedValue* typeValue = VansTimelineFindSourceField(track.extensionData, "valueType");
	if (!descriptorValue || descriptorValue->kind != VansSerializedValue::Kind::String ||
		!componentValue || componentValue->kind != VansSerializedValue::Kind::Int ||
		!typeValue || typeValue->kind != VansSerializedValue::Kind::String) return;
	const auto* descriptor = VansTimelinePropertyAccessRegistry::BuiltIns().Resolve(descriptorValue->stringValue);
	VansTimelineValueType type{};
	if (!descriptor || !VansTimelineSerialization::TryParseValueType(typeValue->stringValue, type) ||
		descriptor->componentTypeId != componentValue->intValue || descriptor->valueType != type)
		AddError(diagnostics, track.id, "descriptorId",
			"Property descriptor, component type and value type must match one registered accessor");
}

bool CompileProperty(const VansTimelineExtensionCompileContext& context,
	const VansTimelineTrack& track, const VansTimelineSourceSchema& schema,
	VansTimelineCompiledDataWriter& writer, VansTimelineCompiledDataView& trackData,
	std::vector<VansTimelineCompiledDataView>& sectionData, VansTimelineDiagnostics& diagnostics)
{
	(void)context;
	VansTimelineCompiledDataView validated;
	if (!writer.WriteSchema(schema, track.extensionData, validated, diagnostics, track.id)) return false;
	const VansSerializedValue* descriptorValue = VansTimelineFindSourceField(track.extensionData, "descriptorId");
	const VansSerializedValue* componentValue = VansTimelineFindSourceField(track.extensionData, "componentTypeId");
	const VansSerializedValue* typeValue = VansTimelineFindSourceField(track.extensionData, "valueType");
	VansTimelineValueType valueType = VansTimelineValueType::Null;
	if (!descriptorValue || descriptorValue->kind != VansSerializedValue::Kind::String ||
		!componentValue || componentValue->kind != VansSerializedValue::Kind::Int ||
		!typeValue || typeValue->kind != VansSerializedValue::Kind::String ||
		!VansTimelineSerialization::TryParseValueType(typeValue->stringValue, valueType)) return false;
	const CompiledPropertyAccess compiled{
		VansMakeStableId<VansTimelinePropertyAccessTag>(descriptorValue->stringValue).value,
		static_cast<std::uint16_t>(componentValue->intValue), static_cast<std::uint8_t>(valueType) };
	trackData = writer.Write(compiled);
	sectionData.assign(track.sections.size(), trackData);
	return true;
}

template <typename Vector>
Vector BlendVector(const Vector& current, const Vector& sampled, VansTimelineBlendMode mode)
{
	Vector result = sampled;
	for (std::size_t index = 0; index < result.value.size(); ++index)
		result.value[index] = mode == VansTimelineBlendMode::Multiply
			? current.value[index] * sampled.value[index]
			: current.value[index] + sampled.value[index];
	return result;
}

VansTimelineQuaternion MultiplyQuaternion(const VansTimelineQuaternion& left,
	const VansTimelineQuaternion& right)
{
	const auto& a = left.value; const auto& b = right.value;
	VansTimelineQuaternion value{ {
		a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
		a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
		a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
		a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2] } };
	double length = 0.0; for (double component : value.value) length += component * component;
	length = std::sqrt(length);
	if (length > 0.0000001) for (double& component : value.value) component /= length;
	return value;
}

bool BlendValue(const VansTimelineValue& current, const VansTimelineValue& sampled,
	VansTimelineBlendMode mode, VansTimelineValue& output, std::string& error)
{
	if (mode == VansTimelineBlendMode::Override) { output = sampled; return true; }
	if (current.index() != sampled.index())
	{ error = "Property blend requires matching value types"; return false; }
	return std::visit([&](const auto& base) -> bool
	{
		using T = std::decay_t<decltype(base)>; const T* value = std::get_if<T>(&sampled);
		if constexpr (std::is_same_v<T, std::int32_t>)
		{
			const std::int64_t result = mode == VansTimelineBlendMode::Multiply
				? static_cast<std::int64_t>(base) * *value : static_cast<std::int64_t>(base) + *value;
			output = static_cast<std::int32_t>(std::clamp<std::int64_t>(result,
				std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max())); return true;
		}
		else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, float> || std::is_same_v<T, double>)
		{ output = mode == VansTimelineBlendMode::Multiply ? base * *value : base + *value; return true; }
		else if constexpr (std::is_same_v<T, VansTimelineVec2> || std::is_same_v<T, VansTimelineVec3> ||
			std::is_same_v<T, VansTimelineVec4> || std::is_same_v<T, VansTimelineColorLinear> ||
			std::is_same_v<T, VansTimelineColorSrgb>)
		{ output = BlendVector(base, *value, mode); return true; }
		else if constexpr (std::is_same_v<T, VansTimelineQuaternion>)
		{ output = MultiplyQuaternion(base, *value); return true; }
		else { error = "Property value only supports Override blending"; return false; }
	}, current);
}

struct PropertyRestoreState
{
	VansTimelineWriterHandle writer;
	const VansTimelinePropertyAccessDescriptor* descriptor = nullptr;
	VansTimelinePropertyAccessContext access;
	VansTimelineValue previous;
};

class PropertyTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	PropertyTimelineApplier(VansRuntimeWorld& world, const VansTimelinePropertyAccessRegistry& accessors)
		: m_World(world), m_Accessors(accessors) {}
	VansTimelineOutputTypeId OutputType() const override
	{ return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::Property) + ".Output"); }
	std::string_view StableName() const override { return "Scene.PropertyTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.section || context.section->channels.empty())
			return { VansTimelineApplyStatus::Ignored };
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const CompiledPropertyAccess* compiled = reader.Read<CompiledPropertyAccess>(context.section->extensionData);
		const auto* descriptor = compiled ? m_Accessors.Resolve(
			VansStableId<VansTimelinePropertyAccessTag>{ compiled->descriptorId }) : nullptr;
		if (!descriptor || descriptor->componentTypeId != compiled->componentTypeId ||
			descriptor->valueType != static_cast<VansTimelineValueType>(compiled->valueType))
			return { VansTimelineApplyStatus::Failed, {}, "Property accessor contract does not match compiled track data" };
		const auto sampled = VansTimelineEvaluator::SampleChannel(context.section->channels.front(), sample->localTick);
		if (!sampled) return { VansTimelineApplyStatus::Ignored };
		VansTimelinePropertyAccessContext access{ target, &m_World, {} };
		access.resource = { VansStableHash64("Scene.Property") ^ descriptor->id.value,
			target.component.IsValid()
				? ((static_cast<std::uint64_t>(target.component.generation) << 32) |
					(static_cast<std::uint64_t>(target.component.typeId) << 16) | target.component.index + 1ull)
				: ((static_cast<std::uint64_t>(target.entity.generation) << 32) | target.entity.index + 1ull) };
		std::string readError;
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			PropertyRestoreState created{ context.writer, descriptor, access, {} };
			descriptor->read(access, created.previous, readError); return created;
		});
		if (!readError.empty()) { m_State.Release(restore); return { VansTimelineApplyStatus::Failed, {}, readError }; }
		VansTimelineValue current; std::string error;
		if (!descriptor->read(access, current, error)) return { VansTimelineApplyStatus::Failed, {}, error };
		VansTimelineValue blended;
		if (!BlendValue(current, *sampled, context.blendMode, blended, error) ||
			!descriptor->write(access, blended, error))
			return { VansTimelineApplyStatus::Failed, {}, error };
		return { VansTimelineApplyStatus::Applied, { restore, {}, {}, access.resource } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		PropertyRestoreState* state = m_State.Resolve(token.handle);
		if (!state) return false; std::string error;
		state->descriptor->write(state->access, state->previous, error);
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansRuntimeWorld& m_World;
	const VansTimelinePropertyAccessRegistry& m_Accessors;
	VansTimelineModuleApplierState<PropertyRestoreState> m_State;
};
}

bool VansRegisterSceneTimelinePropertyAccessors(VansTimelinePropertyAccessRegistry& registry,
	std::string& error)
{
	auto add = [&](std::string name, VansTimelineValueType type,
		VansTimelinePropertyReadFn read, VansTimelinePropertyWriteFn write)
	{
		VansTimelinePropertyAccessDescriptor descriptor;
		descriptor.id = VansMakeStableId<VansTimelinePropertyAccessTag>(name);
		descriptor.stableName = std::move(name); descriptor.componentTypeId = VansRuntimeComponentType_Transform;
		descriptor.valueType = type; descriptor.read = read; descriptor.write = write;
		return registry.Register(std::move(descriptor), error);
	};
	return add("Transform.Position", VansTimelineValueType::Vec3, ReadTransformPosition, WriteTransformPosition) &&
		add("Transform.Rotation", VansTimelineValueType::Quaternion, ReadTransformRotation, WriteTransformRotation) &&
		add("Transform.Scale", VansTimelineValueType::Vec3, ReadTransformScale, WriteTransformScale);
}

bool VansRegisterPropertyTimelineExtension(VansTimelineTrackExtensionRegistry& registry,
	std::string& error)
{
	using F = VansTimelineValueType;
	auto descriptor = VansMakeTimelineSampleExtension(
		TimelineNames::Property, "Property", "Object", VansTimelineEvaluationPhase::PostScript,
		VansTimelineBindingRequirement::Required, VansTimelineContinuousTrackFlags(),
		{ { VansMakeTimelineSourceField("descriptorId", F::String, std::string(), true),
			VansMakeTimelineSourceField("componentTypeId", F::Int32, std::int32_t(0), true),
			VansMakeTimelineSourceField("valueType", F::Enum, std::string("Float"), false,
				{ "Bool", "Int32", "Int64", "Float", "Double", "String", "Vec2", "Vec3", "Vec4",
					"Quaternion", "ColorLinear", "ColorSrgb" }) },
			{ VansMakeTimelineChannelSchema("value", F::Float, true, "valueType") }, false, false });
	descriptor.validate = ValidateProperty;
	descriptor.compile = CompileProperty;
	return registry.Register(std::move(descriptor), error);
}

bool VansRegisterPropertyTimelineIntegration(VansRuntimeWorld& world,
	const VansTimelinePropertyAccessRegistry& accessors,
	VansTimelineApplierRegistry& registry, std::string& error)
{
	return registry.Register(std::make_shared<PropertyTimelineApplier>(world, accessors), error);
}
}
