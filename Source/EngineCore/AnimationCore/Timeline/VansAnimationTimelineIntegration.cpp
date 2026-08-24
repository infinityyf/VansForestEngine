#include "VansAnimationTimelineIntegration.h"

#include "../Storage/VansBoneMaskStorage.h"
#include "../VansAnimationClip.h"
#include "../VansAnimationNode.h"
#include "../../AssetCore/VansAssetResolver.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../ScriptCore/VansTransform.h"
#include "../../TimelineCore/VansTimelineDependencyBuilder.h"
#include "../../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../../TimelineRuntime/VansTimelineEvaluator.h"
#include "../../TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../../TimelineRuntime/VansTimelineSampleExtension.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Vans
{
namespace
{
using namespace VansGraphics;

VansRuntimeAnimationComponent* ResolveAnimation(
	VansRuntimeWorld& world, const VansResolvedTimelineTarget& target, VansComponentHandle* handle = nullptr)
{
	auto* storage = static_cast<VansComponentStorage<VansRuntimeAnimationComponent>*>(
		world.FindStorage(VansRuntimeComponentType_Animation));
	if (!storage || !world.IsAlive(target.entity)) return nullptr;
	for (VansComponentHandle component : world.CollectComponentsOwnedBy(target.entity))
		if (component.typeId == VansRuntimeComponentType_Animation)
			if (auto* runtime = storage->Get(component))
			{ if (handle) *handle = component; return runtime; }
	return nullptr;
}

VansRuntimeAnimationComponent* ResolveAnimation(
	VansRuntimeWorld& world, VansComponentHandle component)
{
	auto* storage = static_cast<VansComponentStorage<VansRuntimeAnimationComponent>*>(
		world.FindStorage(VansRuntimeComponentType_Animation));
	return storage ? storage->Get(component) : nullptr;
}

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

double Number(const VansTimelineValue* value, double fallback)
{
	if (const auto* typed = value ? std::get_if<float>(value) : nullptr) return *typed;
	if (const auto* typed = value ? std::get_if<double>(value) : nullptr) return *typed;
	if (const auto* typed = value ? std::get_if<std::int32_t>(value) : nullptr) return *typed;
	if (const auto* typed = value ? std::get_if<std::int64_t>(value) : nullptr) return static_cast<double>(*typed);
	return fallback;
}

bool Boolean(const VansTimelineValue& value, bool fallback = false)
{
	if (const auto* typed = std::get_if<bool>(&value)) return *typed;
	return Number(&value, fallback ? 1.0 : 0.0) != 0.0;
}

std::string String(const VansTimelineCompiledDataReader& reader,
	const VansTimelineCompiledDataView& data, std::size_t slot)
{
	const VansTimelineValue* value = reader.ValueAt(data, slot);
	const auto* typed = value ? std::get_if<std::string>(value) : nullptr;
	return typed ? *typed : std::string{};
}

glm::vec3 Vec3(const VansTimelineVec3& value)
{
	return { static_cast<float>(value.value[0]), static_cast<float>(value.value[1]),
		static_cast<float>(value.value[2]) };
}

glm::quat Quaternion(const VansTimelineQuaternion& value)
{
	glm::quat result(static_cast<float>(value.value[3]), static_cast<float>(value.value[0]),
		static_cast<float>(value.value[1]), static_cast<float>(value.value[2]));
	return glm::length(result) > 0.00001f ? glm::normalize(result) : glm::quat(1, 0, 0, 0);
}

void Decompose(const glm::mat4& matrix, glm::vec3& position, glm::quat& rotation, glm::vec3& scale)
{
	glm::vec3 skew; glm::vec4 perspective;
	if (!glm::decompose(matrix, scale, rotation, position, skew, perspective))
	{ position = {}; rotation = glm::quat(1, 0, 0, 0); scale = glm::vec3(1); }
	rotation = glm::normalize(rotation);
}

void CollectAnimationDependencies(
	const VansTimelineTrack& track,
	std::vector<VansTimelineDependency>& dependencies)
{
	auto collectMask = [&](const VansSerializedValue& data, const VansTimelineId& source)
	{
		const VansSerializedValue* mask = VansTimelineFindSourceField(data, "avatarMaskGuid");
		if (mask && mask->kind == VansSerializedValue::Kind::String && !mask->stringValue.empty())
			dependencies.push_back({ VansTimelineDependencyKind::Asset, "BoneMask",
				mask->stringValue, {}, source });
	};
	collectMask(track.extensionData, track.id);
	for (const VansTimelineSection& section : track.sections)
		if (section.extensionData) collectMask(*section.extensionData, section.id);
}

bool IsDiscontinuousEvaluation(const VansTimelineTraversalSegment& traversal)
{
	return traversal.reason == VansTimelineEvaluationReason::Jump ||
		traversal.reason == VansTimelineEvaluationReason::Scrub ||
		traversal.reason == VansTimelineEvaluationReason::Step || traversal.discontinuity;
}

void EvaluateAnimatorParameter(VansTimelineExtensionEvaluationContext& context)
{
	if (!context.section) return;
	const std::string type = String(context.compiledData, context.section->extensionData, 1);
	if (type != "Trigger")
	{
		VansEvaluateTimelineSampleExtension(context);
		return;
	}
	const std::string firePolicy = String(context.compiledData, context.section->extensionData, 2);
	if ((firePolicy == "Forward" && context.traversal.playbackDirection < 0) ||
		(firePolicy == "Backward" && context.traversal.playbackDirection > 0)) return;
	if (IsDiscontinuousEvaluation(context.traversal))
	{
		const std::string seekPolicy = String(context.compiledData, context.section->extensionData, 3);
		if (seekPolicy == "Never") return;
		if (seekPolicy == "Exact" && context.traversal.seekPolicy != VansTimelineSeekPolicy::ExactTick) return;
		if (seekPolicy == "Crossed" && context.traversal.seekPolicy != VansTimelineSeekPolicy::AllEdges &&
			context.traversal.seekPolicy != VansTimelineSeekPolicy::SafeEdges) return;
	}
	for (const VansTimelineChannel& channel : context.section->channels)
		for (const VansTimelineKey& key : channel.keys)
		{
			const bool enabled = !std::holds_alternative<bool>(key.value) || std::get<bool>(key.value);
			if (!enabled || !VansTimelineEvaluator::Crossed(
				context.traversal, context.section->startTick + key.tick)) continue;
			VansTimelineSampleOutput payload;
			payload.timelineTick = context.traversal.currentTick;
			payload.localTick = key.tick;
			payload.loopIteration = context.traversal.loopIteration;
			payload.direction = static_cast<std::int8_t>(context.traversal.playbackDirection);
			payload.active = true;
			payload.entered = true;
			context.Emit(context.track.outputTypeId, VansInvalidTimelineRegistrySlot,
				payload, key.id, context.section->completionMode);
			context.outputs.back().retainsPreAnimatedState = false;
		}
}

struct AnimationRestoreState
{
	VansTimelineWriterHandle writer;
	VansComponentHandle component;
	VansSlotPlaybackHandle playback;
};

class AnimationTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	AnimationTimelineApplier(VansRuntimeWorld& world, std::shared_ptr<VansAssetResolver> resolver)
		: m_World(world), m_Resolver(std::move(resolver)) {}
	VansTimelineOutputTypeId OutputType() const override
	{ return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::AnimationClip) + ".Output"); }
	std::string_view StableName() const override { return "Animation.AnimationClipTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		if (!sample || !context.section) return { VansTimelineApplyStatus::Failed, {}, "Animation output is invalid" };
		VansComponentHandle component;
		auto* runtime = ResolveAnimation(m_World, target, &component);
		auto* controller = runtime && runtime->animationNode ? runtime->animationNode->GetController() : nullptr;
		if (!controller) return { VansTimelineApplyStatus::Failed, {}, "Animation binding has no controller" };
		if (!sample->active)
		{
			if (AnimationRestoreState* state = m_State.ResolveWriter(context.writer))
				controller->StopSlot(state->playback, 0.0f, true);
			return { VansTimelineApplyStatus::Ignored };
		}
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const std::string slot = String(reader, context.section->extensionData, 0);
		const std::string layer = String(reader, context.section->extensionData, 1);
		if (slot.empty()) return { VansTimelineApplyStatus::Failed, {}, "Animation slot is missing" };
		const VansAnimationSlotDefinition* slotDefinition = controller->FindSlotDefinition(slot);
		if (!slotDefinition || (!layer.empty() && layer != slotDefinition->layerId))
			return { VansTimelineApplyStatus::Failed, {}, "Animation slot or layer is unavailable" };
		const std::string controllerClip = String(reader, context.section->extensionData, 7);
		if (!controllerClip.empty() && !controller->GetClip(controllerClip))
			return { VansTimelineApplyStatus::Failed, {},
				"Animation controller Clip is unavailable: " + controllerClip };
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			std::string clipName = controllerClip;
			if (clipName.empty() && !context.section->assetGuid.empty())
			{
				const VansResolvedAsset clipAsset = m_Resolver->Resolve(
					context.section->assetGuid, VansAssetType::AnimationClip);
				if (clipAsset.valid)
				{
					VansAnimationClip clip; Skeleton skeleton;
					if (VansAnimationClipIO::Load(clipAsset.readPath.string(), clip, skeleton))
					{
						clipName = clip.clipName;
						if (!controller->GetClip(clipName)) controller->AddClip(clipName, std::move(clip));
					}
				}
			}
			VansSlotPlayRequest request;
			request.clipName = clipName;
			request.startTime = static_cast<float>(std::max(0.0,
				VansTimelineTime::TickToSeconds(sample->localTick, context.timeline.Timebase())));
			request.loopCount = 1;
			request.priority = context.order.priority;
			request.blendIn = static_cast<float>(std::max(0.0,
				VansTimelineTime::TickToSeconds(context.section->easeInTicks, context.timeline.Timebase())));
			request.blendOut = static_cast<float>(std::max(0.0,
				VansTimelineTime::TickToSeconds(context.section->easeOutTicks, context.timeline.Timebase())));
			request.weight = static_cast<float>(std::max(0.0, Number(
				reader.ValueAt(context.section->extensionData, 2), 1.0) * sample->weight));
			const auto* additive = reader.ValueAt(context.section->extensionData, 3);
			request.additive = additive && std::get_if<bool>(additive) && *std::get_if<bool>(additive);
			request.externallyDriven = true;
			request.suppressRootMotion = true;
			request.syncGroup = String(reader, context.section->extensionData, 5);
			const auto* markerSync = reader.ValueAt(context.section->extensionData, 6);
			request.markerSync = markerSync && std::get_if<bool>(markerSync) && *std::get_if<bool>(markerSync);
			const std::string maskGuid = String(reader, context.section->extensionData, 4);
			if (!maskGuid.empty())
			{
				const VansResolvedAsset maskAsset = m_Resolver->Resolve(maskGuid, VansAssetType::BoneMask);
				if (!maskAsset.valid) return AnimationRestoreState{ context.writer, component, {} };
				VansBoneMaskAsset mask; std::string maskError;
				if (!VansBoneMaskStorage::Load(maskAsset.readPath, mask, maskError))
					return AnimationRestoreState{ context.writer, component, {} };
				VansCompiledBoneMask compiled = VansBoneMaskCompiler::Compile(mask, runtime->animationNode->GetSkeleton());
				if (!compiled.valid) return AnimationRestoreState{ context.writer, component, {} };
				request.boneMaskWeights = std::move(compiled.weights);
			}
			request.tag = context.track.id + ":" + context.section->id;
			return AnimationRestoreState{ context.writer, component,
				controller->PlaySlot(slot, request) };
		});
		if (!state || !state->playback)
			return { VansTimelineApplyStatus::Failed, {}, "Animation controller rejected the Timeline slot" };
		const float seconds = static_cast<float>(std::max(0.0,
			VansTimelineTime::TickToSeconds(sample->localTick, context.timeline.Timebase())));
		double channelWeight = 1.0;
		for (const VansTimelineChannel& channel : context.section->channels)
			if (channel.name == "weight")
				if (const auto value = VansTimelineEvaluator::SampleChannel(channel, sample->localTick))
					channelWeight = Number(&*value, 1.0);
		const float weight = static_cast<float>(std::max(0.0,
			Number(reader.ValueAt(context.section->extensionData, 2), 1.0) * sample->weight * channelWeight));
		if (!controller->DriveSlot(state->playback, seconds, weight))
			return { VansTimelineApplyStatus::Failed, {}, "Animation Timeline slot drive failed" };
		const VansTimelineResourceId resource{ VansStableHash64("Animation.Slot"),
			VansStableHash64(std::to_string(target.entity.index) + "#" + slot) };
		return { VansTimelineApplyStatus::Applied, { restore, {}, {}, resource } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		AnimationRestoreState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		if (auto* runtime = ResolveAnimation(m_World, state->component))
			if (auto* controller = runtime->animationNode ? runtime->animationNode->GetController() : nullptr)
				controller->StopSlot(state->playback, 0.0f, true);
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansRuntimeWorld& m_World;
	std::shared_ptr<VansAssetResolver> m_Resolver;
	VansTimelineModuleApplierState<AnimationRestoreState> m_State;
};

struct ParameterRestoreState
{
	VansTimelineWriterHandle writer;
	VansComponentHandle component;
	std::string name;
	AnimatorParameter previous;
};

class AnimatorParameterTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	explicit AnimatorParameterTimelineApplier(VansRuntimeWorld& world) : m_World(world) {}
	VansTimelineOutputTypeId OutputType() const override
	{ return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::AnimatorParameter) + ".Output"); }
	std::string_view StableName() const override { return "Animation.AnimatorParameterTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.section || context.section->channels.empty())
			return { VansTimelineApplyStatus::Ignored };
		VansComponentHandle component; auto* runtime = ResolveAnimation(m_World, target, &component);
		auto* controller = runtime && runtime->animationNode ? runtime->animationNode->GetController() : nullptr;
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const std::string name = String(reader, context.section->extensionData, 0);
		const std::string type = String(reader, context.section->extensionData, 1);
		const std::string missing = String(reader, context.section->extensionData, 4);
		if (!controller || !controller->HasParameter(name))
			return missing == "WarningAndSkip" ? VansTimelineApplyResult{ VansTimelineApplyStatus::Ignored }
				: VansTimelineApplyResult{ VansTimelineApplyStatus::Failed, {}, "Animator parameter is unavailable" };
		const auto found = controller->GetParameters().find(name);
		if (type == "Trigger")
		{
			controller->SetTrigger(name);
			return { VansTimelineApplyStatus::Applied };
		}
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{ return ParameterRestoreState{ context.writer, component, name, found->second }; });
		(void)state;
		const auto value = VansTimelineEvaluator::SampleChannel(context.section->channels.front(), sample->localTick);
		if (!value) return { VansTimelineApplyStatus::Ignored };
		if (type == "Bool") controller->SetBool(name, Boolean(*value));
		else if (type == "Int") controller->SetInt(name, static_cast<int>(Number(&*value, 0)));
		else if (type == "Vector3") { if (const auto* typed = std::get_if<VansTimelineVec3>(&*value)) controller->SetVector3(name, Vec3(*typed)); }
		else if (type == "Quaternion") { if (const auto* typed = std::get_if<VansTimelineQuaternion>(&*value)) controller->SetQuaternion(name, Quaternion(*typed)); }
		else controller->SetFloat(name, static_cast<float>(Number(&*value, 0)));
		return { VansTimelineApplyStatus::Applied,
			{ restore, {}, {}, { VansStableHash64("Animation.Parameter"),
				VansStableHash64(std::to_string(target.entity.index) + "#" + name) } } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		ParameterRestoreState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		if (auto* runtime = ResolveAnimation(m_World, state->component))
			if (auto* controller = runtime->animationNode ? runtime->animationNode->GetController() : nullptr)
				switch (state->previous.type)
				{
				case AnimatorParamType::Bool: controller->SetBool(state->name, state->previous.boolVal); break;
				case AnimatorParamType::Int: controller->SetInt(state->name, state->previous.intVal); break;
				case AnimatorParamType::Trigger: state->previous.boolVal ? controller->SetTrigger(state->name) : controller->ResetTrigger(state->name); break;
				case AnimatorParamType::Vector3: controller->SetVector3(state->name, state->previous.vec3Val); break;
				case AnimatorParamType::Quaternion: controller->SetQuaternion(state->name, state->previous.quatVal); break;
				default: controller->SetFloat(state->name, state->previous.floatVal); break;
				}
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansRuntimeWorld& m_World;
	VansTimelineModuleApplierState<ParameterRestoreState> m_State;
};

struct BoneRestoreState
{
	VansTimelineWriterHandle writer;
	VansComponentHandle component;
	std::string bone;
	glm::mat4 previous{ 1.0f };
	bool hadPrevious = false;
};

class BoneOverrideTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	explicit BoneOverrideTimelineApplier(VansRuntimeWorld& world) : m_World(world) {}
	VansTimelineOutputTypeId OutputType() const override
	{ return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::BoneOverride) + ".Output"); }
	std::string_view StableName() const override { return "Animation.BoneOverrideTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.section) return { VansTimelineApplyStatus::Ignored };
		VansComponentHandle component; auto* runtime = ResolveAnimation(m_World, target, &component);
		VansAnimationNode* node = runtime ? runtime->animationNode : nullptr;
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const std::string bone = String(reader, context.section->extensionData, 0);
		if (!node || bone.empty()) return { VansTimelineApplyStatus::Failed, {}, "Bone Override binding is invalid" };
		glm::mat4 current(1); if (!node->TryGetCurrentBoneLocalTransform(bone, current))
			return { VansTimelineApplyStatus::Failed, {}, "Bone Override could not resolve the bone" };
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			BoneRestoreState result; result.writer = context.writer; result.component = component; result.bone = bone;
			result.hadPrevious = node->TryGetBoneLocalTransform(bone, result.previous); return result;
		});
		(void)state;
		glm::vec3 position, scale; glm::quat rotation; Decompose(current, position, rotation, scale);
		glm::vec3 desiredPosition = position; glm::quat desiredRotation = rotation; glm::vec3 desiredScale = scale;
		for (const VansTimelineChannel& channel : context.section->channels)
			if (const auto value = VansTimelineEvaluator::SampleChannel(channel, sample->localTick))
			{
				if (channel.name == "position") if (const auto* typed = std::get_if<VansTimelineVec3>(&*value)) desiredPosition = Vec3(*typed);
				if (channel.name == "rotation") if (const auto* typed = std::get_if<VansTimelineQuaternion>(&*value)) desiredRotation = Quaternion(*typed);
				if (channel.name == "scale") if (const auto* typed = std::get_if<VansTimelineVec3>(&*value)) desiredScale = Vec3(*typed);
			}
		const float weight = static_cast<float>(std::clamp(Number(
			reader.ValueAt(context.section->extensionData, 1), 1.0) * sample->weight, 0.0, 1.0));
		const float positionWeight = weight * static_cast<float>(std::clamp(Number(
			reader.ValueAt(context.section->extensionData, 4), 1.0), 0.0, 1.0));
		const float rotationWeight = weight * static_cast<float>(std::clamp(Number(
			reader.ValueAt(context.section->extensionData, 5), 1.0), 0.0, 1.0));
		const auto* additiveValue = reader.ValueAt(context.section->extensionData, 2);
		const bool additive = additiveValue && std::get_if<bool>(additiveValue) && *std::get_if<bool>(additiveValue);
		bool ikPosition = false;
		bool ikRotation = false;
		const std::string ikBinding = String(reader, context.section->extensionData, 3);
		if (!ikBinding.empty())
		{
			if (!context.bindings || !context.diagnostics)
				return { VansTimelineApplyStatus::Failed, {}, "Bone Override IK requires a binding resolver" };
			const VansResolvedTimelineTarget* ikTarget = context.bindings->Find(
				VansMakeStableId<VansTimelineBindingTag>(ikBinding), context.timeline, *context.diagnostics);
			const std::uint32_t targetTransform = ikTarget ? ResolveTransformId(m_World, ikTarget->entity) : UINT32_MAX;
			const std::uint32_t ownerTransform = ResolveTransformId(m_World, target.entity);
			const auto* controller = node->GetController();
			const Skeleton& skeleton = node->GetSkeleton();
			const auto boneFound = skeleton.boneNameToIndex.find(bone);
			if (!ikTarget || targetTransform >= VansTransformStore::GlobalTransforms.size() ||
				ownerTransform >= VansTransformStore::GlobalTransforms.size() || !controller ||
				boneFound == skeleton.boneNameToIndex.end())
				return { VansTimelineApplyStatus::Failed, {}, "Bone Override IK target is not live for the bound skeleton" };
			VansTransform owner = VansTransformStore::GetTransform(ownerTransform);
			const glm::mat4 ownerInverse = glm::inverse(owner.GetModelMatrix());
			const glm::vec3 targetModel = glm::vec3(ownerInverse * glm::vec4(
				VansTransformStore::GetTransform(targetTransform).m_Position, 1.0f));
			const auto& globals = controller->GetCachedGlobalTransforms();
			const int boneIndex = boneFound->second;
			const int parentIndex = skeleton.bones[boneIndex].parentIndex;
			const glm::mat4 parentModel = parentIndex >= 0 && parentIndex < static_cast<int>(globals.size())
				? globals[parentIndex] : glm::mat4(1.0f);
			desiredPosition = glm::vec3(glm::inverse(parentModel) * glm::vec4(targetModel, 1.0f));
			ikPosition = true;
			if (boneIndex < static_cast<int>(globals.size()))
			{
				const glm::vec3 direction = targetModel - glm::vec3(globals[boneIndex][3]);
				if (glm::length2(direction) > 0.000001f)
				{
					glm::vec3 ignoredPosition, ignoredScale; glm::quat parentRotation;
					Decompose(parentModel, ignoredPosition, parentRotation, ignoredScale);
					desiredRotation = glm::normalize(glm::inverse(parentRotation) *
						glm::quatLookAt(glm::normalize(direction), glm::vec3(0, 1, 0)));
					ikRotation = true;
				}
			}
		}
		glm::vec3 finalPosition = position; glm::quat finalRotation = rotation; glm::vec3 finalScale = scale;
		const bool hasPosition = ikPosition || std::any_of(context.section->channels.begin(), context.section->channels.end(),
			[](const VansTimelineChannel& channel) { return channel.name == "position"; });
		const bool hasRotation = ikRotation || std::any_of(context.section->channels.begin(), context.section->channels.end(),
			[](const VansTimelineChannel& channel) { return channel.name == "rotation"; });
		const bool hasScale = std::any_of(context.section->channels.begin(), context.section->channels.end(),
			[](const VansTimelineChannel& channel) { return channel.name == "scale"; });
		if (hasPosition) finalPosition = additive ? position + desiredPosition * positionWeight
			: glm::mix(position, desiredPosition, positionWeight);
		if (hasRotation) finalRotation = additive
			? glm::normalize(rotation * glm::slerp(glm::quat(1, 0, 0, 0), desiredRotation, rotationWeight))
			: glm::normalize(glm::slerp(rotation, desiredRotation, rotationWeight));
		if (hasScale) finalScale = additive ? scale * glm::mix(glm::vec3(1), desiredScale, weight)
			: glm::mix(scale, desiredScale, weight);
		node->SetBoneLocalTransform(bone, glm::translate(glm::mat4(1), finalPosition) *
			glm::mat4_cast(finalRotation) * glm::scale(glm::mat4(1), finalScale));
		return { VansTimelineApplyStatus::Applied,
			{ restore, {}, {}, { VansStableHash64("Animation.BoneOverride"),
				VansStableHash64(std::to_string(target.entity.index) + "#" + bone) } } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		BoneRestoreState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		if (auto* runtime = ResolveAnimation(m_World, state->component))
			if (runtime->animationNode)
				state->hadPrevious ? runtime->animationNode->SetBoneLocalTransform(state->bone, state->previous)
					: runtime->animationNode->ClearBoneOverride(state->bone);
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansRuntimeWorld& m_World;
	VansTimelineModuleApplierState<BoneRestoreState> m_State;
};
}

bool VansRegisterAnimationTimelineExtensions(VansTimelineTrackExtensionRegistry& registry, std::string& error)
{
	using F = VansTimelineValueType;
	const auto post = VansTimelineEvaluationPhase::PostScript;
	const auto required = VansTimelineBindingRequirement::Required;
	auto animationClip = VansMakeTimelineSampleExtension(
		TimelineNames::AnimationClip, "Animation Clip", "Animation", post, required,
		VansTimelineContinuousTrackFlags(),
		{ { VansMakeTimelineSourceField("slot", F::String, std::string(), true),
			VansMakeTimelineSourceField("layer", F::String, std::string()),
			VansMakeTimelineSourceField("weight", F::Double, 1.0),
			VansMakeTimelineSourceField("additive", F::Bool, false),
			VansMakeTimelineSourceField("avatarMaskGuid", F::String, std::string()),
			VansMakeTimelineSourceField("syncGroup", F::String, std::string()),
			VansMakeTimelineSourceField("markerSync", F::Bool, false),
			// Animator 预编译的 Clip 名称；填写后 Timeline 只驱动 Slot，运行时不再动态 AddClip。
			VansMakeTimelineSourceField("controllerClip", F::String, std::string()) },
			{ VansMakeTimelineChannelSchema("weight", F::Float) }, false, false },
		CollectAnimationDependencies);
	animationClip.sectionAssetKind = "AnimationClip";
	if (!registry.Register(std::move(animationClip), error)) return false;
	auto parameter = VansMakeTimelineSampleExtension(
		TimelineNames::AnimatorParameter, "Animator Parameter", "Animation", post, required,
		VansTimelineContinuousTrackFlags(),
		{ { VansMakeTimelineSourceField("parameterName", F::String, std::string(), true),
			VansMakeTimelineSourceField("parameterType", F::Enum, std::string("Float"), false,
				{ "Float", "Bool", "Int", "Trigger", "Vector3", "Quaternion" }),
			VansMakeTimelineSourceField("firePolicy", F::Enum, std::string("Forward"), false,
				{ "Forward", "Backward", "Both" }),
			VansMakeTimelineSourceField("seekPolicy", F::Enum, std::string("Never"), false,
				{ "Never", "Crossed", "Exact" }),
			VansMakeTimelineSourceField("missingParameterPolicy", F::Enum, std::string("Error"), false,
				{ "Error", "WarningAndSkip" }) },
			{ VansMakeTimelineChannelSchema("value", F::Float, true, "parameterType",
				{ { "Float", F::Float }, { "Bool", F::Bool }, { "Int", F::Int32 },
					{ "Trigger", F::Bool }, { "Vector3", F::Vec3 }, { "Quaternion", F::Quaternion } }) },
			false, false });
	parameter.flags = parameter.flags | VansTimelineTrackFlags::PointEdge;
	parameter.evaluate = EvaluateAnimatorParameter;
	if (!registry.Register(std::move(parameter), error)) return false;
	return registry.Register(VansMakeTimelineSampleExtension(
		TimelineNames::BoneOverride, "Bone Override", "Animation", post, required,
		VansTimelineContinuousTrackFlags(),
		{ { VansMakeTimelineSourceField("boneId", F::String, std::string(), true),
			VansMakeTimelineSourceField("weight", F::Double, 1.0),
			VansMakeTimelineSourceField("additive", F::Bool, false),
			VansMakeTimelineSourceField("ikTargetBindingId", F::String, std::string()),
			VansMakeTimelineSourceField("positionWeight", F::Double, 1.0),
			VansMakeTimelineSourceField("rotationWeight", F::Double, 1.0) },
			{ VansMakeTimelineChannelSchema("position", F::Vec3),
				VansMakeTimelineChannelSchema("rotation", F::Quaternion),
				VansMakeTimelineChannelSchema("scale", F::Vec3) }, false, false }), error);
}

bool VansRegisterAnimationTimelineIntegration(VansRuntimeWorld& world, std::shared_ptr<VansAssetResolver> resolver,
	VansTimelineApplierRegistry& registry, std::string& error)
{
	if (!resolver) { error = "Animation Timeline integration requires an asset resolver"; return false; }
	if (!registry.Register(std::make_shared<AnimationTimelineApplier>(world, std::move(resolver)), error)) return false;
	if (!registry.Register(std::make_shared<AnimatorParameterTimelineApplier>(world), error)) return false;
	return registry.Register(std::make_shared<BoneOverrideTimelineApplier>(world), error);
}
}
