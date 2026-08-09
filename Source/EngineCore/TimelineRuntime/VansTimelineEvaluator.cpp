#include "VansTimelineEvaluator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <type_traits>

namespace Vans
{
namespace
{
std::string Lower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
	{
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

double ClampUnit(double value)
{
	return std::clamp(value, 0.0, 1.0);
}

double EvaluateBlendCurve(const VansTimelineBlendCurve& curve, double alpha)
{
	alpha = ClampUnit(alpha);
	const std::string shape = Lower(curve.shape);
	if (shape == "easein") return std::pow(alpha, std::max(0.0001, curve.exponent));
	if (shape == "easeout") return 1.0 - std::pow(1.0 - alpha, std::max(0.0001, curve.exponent));
	if (shape == "easeinout" || shape == "smoothstep") return alpha * alpha * (3.0 - 2.0 * alpha);
	return alpha;
}

double SectionEnvelope(const VansTimelineSection& section, VansTimelineTick tick)
{
	if (!section.active || tick < section.startTick ||
		tick >= section.startTick + section.durationTicks) return 0.0;
	const VansTimelineTick relative = tick - section.startTick;
	const VansTimelineTick remaining = section.durationTicks - relative;
	double weight = 1.0;
	if (section.easeInTicks > 0 && relative < section.easeInTicks)
		weight = std::min(weight, EvaluateBlendCurve(section.blendIn,
			static_cast<double>(relative) / static_cast<double>(section.easeInTicks)));
	if (section.easeOutTicks > 0 && remaining <= section.easeOutTicks)
		weight = std::min(weight, EvaluateBlendCurve(section.blendOut,
			static_cast<double>(remaining) / static_cast<double>(section.easeOutTicks)));
	return ClampUnit(weight);
}

template <std::size_t Count>
std::array<double, Count> LerpArray(
	const std::array<double, Count>& left,
	const std::array<double, Count>& right,
	double alpha)
{
	std::array<double, Count> result{};
	for (std::size_t index = 0; index < Count; ++index)
		result[index] = left[index] + (right[index] - left[index]) * alpha;
	return result;
}

VansTimelineQuaternion SlerpQuaternion(
	const VansTimelineQuaternion& left,
	const VansTimelineQuaternion& right,
	double alpha)
{
	auto a = left.value;
	auto b = right.value;
	double dot = 0.0;
	for (std::size_t index = 0; index < 4; ++index) dot += a[index] * b[index];
	if (dot < 0.0)
	{
		for (double& value : b) value = -value;
		dot = -dot;
	}
	dot = std::clamp(dot, -1.0, 1.0);
	std::array<double, 4> value{};
	if (dot > 0.9995)
		value = LerpArray(a, b, alpha);
	else
	{
		const double angle = std::acos(dot);
		const double denominator = std::sin(angle);
		const double leftWeight = std::sin((1.0 - alpha) * angle) / denominator;
		const double rightWeight = std::sin(alpha * angle) / denominator;
		for (std::size_t index = 0; index < 4; ++index)
			value[index] = a[index] * leftWeight + b[index] * rightWeight;
	}
	double length = 0.0;
	for (double component : value) length += component * component;
	length = std::sqrt(length);
	if (length > 0.0) for (double& component : value) component /= length;
	return { value };
}

double CurveAlpha(const VansTimelineKey& left, const VansTimelineKey& right, double alpha)
{
	switch (left.interpolation)
	{
	case VansTimelineInterpolation::Constant: return 0.0;
	case VansTimelineInterpolation::Auto:
	case VansTimelineInterpolation::ClampedAuto:
		return alpha * alpha * (3.0 - 2.0 * alpha);
	case VansTimelineInterpolation::Cubic:
	case VansTimelineInterpolation::Bezier:
	{
		const double duration = static_cast<double>(std::max<VansTimelineTick>(1, right.tick - left.tick));
		const double a2 = alpha * alpha;
		const double a3 = a2 * alpha;
		const double h00 = 2.0 * a3 - 3.0 * a2 + 1.0;
		const double h10 = a3 - 2.0 * a2 + alpha;
		const double h01 = -2.0 * a3 + 3.0 * a2;
		const double h11 = a3 - a2;
		const double tangentContribution = h10 * left.leaveTangent * duration + h11 * right.arriveTangent * duration;
		return ClampUnit(h01 + tangentContribution * 0.000001 + (1.0 - h00 - h01));
	}
	default: return alpha;
	}
}

VansTimelineKeyValue InterpolateValue(
	const VansTimelineKey& left,
	const VansTimelineKey& right,
	double alpha)
{
	if (left.interpolation == VansTimelineInterpolation::Constant || left.value.index() != right.value.index())
		return left.value;
	alpha = CurveAlpha(left, right, ClampUnit(alpha));
	if (const auto* value = std::get_if<float>(&left.value))
		return static_cast<float>(*value + (std::get<float>(right.value) - *value) * alpha);
	if (const auto* value = std::get_if<double>(&left.value))
		return *value + (std::get<double>(right.value) - *value) * alpha;
	if (const auto* value = std::get_if<VansTimelineVec2>(&left.value))
		return VansTimelineVec2{ LerpArray(value->value, std::get<VansTimelineVec2>(right.value).value, alpha) };
	if (const auto* value = std::get_if<VansTimelineVec3>(&left.value))
		return VansTimelineVec3{ LerpArray(value->value, std::get<VansTimelineVec3>(right.value).value, alpha) };
	if (const auto* value = std::get_if<VansTimelineVec4>(&left.value))
		return VansTimelineVec4{ LerpArray(value->value, std::get<VansTimelineVec4>(right.value).value, alpha) };
	if (const auto* value = std::get_if<VansTimelineQuaternion>(&left.value))
		return SlerpQuaternion(*value, std::get<VansTimelineQuaternion>(right.value), alpha);
	if (const auto* value = std::get_if<VansTimelineColorLinear>(&left.value))
		return VansTimelineColorLinear{ LerpArray(value->value, std::get<VansTimelineColorLinear>(right.value).value, alpha) };
	if (const auto* value = std::get_if<VansTimelineColorSrgb>(&left.value))
		return VansTimelineColorSrgb{ LerpArray(value->value, std::get<VansTimelineColorSrgb>(right.value).value, alpha) };
	return left.value;
}

std::optional<VansTimelineKeyValue> SampleChannel(const VansTimelineChannel& channel, VansTimelineTick tick)
{
	if (channel.keys.empty()) return std::nullopt;
	const auto right = std::lower_bound(channel.keys.begin(), channel.keys.end(), tick,
		[](const VansTimelineKey& key, VansTimelineTick value) { return key.tick < value; });
	if (right == channel.keys.begin())
		return right->value;
	if (right == channel.keys.end())
		return channel.keys.back().value;
	if (right->tick == tick)
		return right->value;
	const VansTimelineKey& left = *(right - 1);
	const double alpha = static_cast<double>(tick - left.tick) /
		static_cast<double>(std::max<VansTimelineTick>(1, right->tick - left.tick));
	return InterpolateValue(left, *right, alpha);
}

double NumericValue(const VansTimelineKeyValue& value, double fallback)
{
	if (const auto* typed = std::get_if<float>(&value)) return *typed;
	if (const auto* typed = std::get_if<double>(&value)) return *typed;
	if (const auto* typed = std::get_if<std::int32_t>(&value)) return *typed;
	if (const auto* typed = std::get_if<std::int64_t>(&value)) return static_cast<double>(*typed);
	return fallback;
}

bool ValuesEqual(const VansTimelineKeyValue& left, const VansTimelineKeyValue& right)
{
	if (left.index() != right.index()) return false;
	if (const auto* value = std::get_if<std::monostate>(&left)) return value != nullptr;
	if (const auto* value = std::get_if<bool>(&left)) return *value == std::get<bool>(right);
	if (const auto* value = std::get_if<std::int32_t>(&left)) return *value == std::get<std::int32_t>(right);
	if (const auto* value = std::get_if<std::int64_t>(&left)) return *value == std::get<std::int64_t>(right);
	if (const auto* value = std::get_if<float>(&left)) return *value == std::get<float>(right);
	if (const auto* value = std::get_if<double>(&left)) return *value == std::get<double>(right);
	if (const auto* value = std::get_if<std::string>(&left)) return *value == std::get<std::string>(right);
	return false;
}

bool ConditionPasses(
	const VansTimelineCondition& condition,
	const std::unordered_map<std::string, VansTimelineKeyValue>& parameters)
{
	if (condition.parameter.empty()) return true;
	const auto found = parameters.find(condition.parameter);
	const bool equal = found != parameters.end() && ValuesEqual(found->second, condition.expectedValue);
	return condition.negate ? !equal : equal;
}

const VansTimelineTrackConfig& EffectiveConfig(const VansTimelineTrack& track, const VansTimelineSection& section)
{
	return std::holds_alternative<std::monostate>(section.config) ? track.config : section.config;
}

bool IsInside(const VansTimelineSection& section, VansTimelineTick tick)
{
	return section.active && tick >= section.startTick && tick < section.startTick + section.durationTicks;
}

bool Crossed(const VansTimelineEvaluationSegment& segment, VansTimelineTick tick)
{
	if (segment.reason == VansTimelineEvaluationReason::LoopWrap)
		return segment.currentTick == tick;
	if (segment.reason == VansTimelineEvaluationReason::Jump || segment.reason == VansTimelineEvaluationReason::Scrub)
	{
		if (segment.seekPolicy == VansTimelineSeekPolicy::ExactTick)
			return segment.currentTick == tick;
		if (segment.seekPolicy != VansTimelineSeekPolicy::AllEdges &&
			segment.seekPolicy != VansTimelineSeekPolicy::SafeEdges)
			return false;
	}
	return VansTimelineEdgeCrossing::Crossed(segment.previousTick, segment.currentTick, tick,
		segment.seekPolicy == VansTimelineSeekPolicy::ExactTick);
}

bool EventDirectionAllowed(const VansTimelineEventTrackConfig& config, const VansTimelineEvaluationSegment& segment)
{
	const std::string policy = Lower(config.firePolicy);
	if (policy == "both") return true;
	if (segment.playbackDirection < 0) return policy == "reverse";
	return policy == "forward";
}

bool EventSeekAllowed(const VansTimelineEventTrackConfig& config, const VansTimelineEvaluationSegment& segment)
{
	if (segment.reason != VansTimelineEvaluationReason::Jump &&
		segment.reason != VansTimelineEvaluationReason::Scrub &&
		segment.reason != VansTimelineEvaluationReason::Step)
		return true;
	const std::string policy = Lower(config.seekPolicy);
	if (policy == "exacttick") return segment.seekPolicy == VansTimelineSeekPolicy::ExactTick;
	if (policy == "crossed") return segment.seekPolicy == VansTimelineSeekPolicy::AllEdges ||
		segment.seekPolicy == VansTimelineSeekPolicy::SafeEdges;
	return false;
}

bool EdgesAllowed(const VansTimelineEvaluationSegment& segment)
{
	return segment.seekPolicy == VansTimelineSeekPolicy::SafeEdges ||
		segment.seekPolicy == VansTimelineSeekPolicy::ExactTick ||
		segment.seekPolicy == VansTimelineSeekPolicy::AllEdges ||
		segment.reason == VansTimelineEvaluationReason::Playback ||
		segment.reason == VansTimelineEvaluationReason::LoopWrap;
}

VansTimelineTransformOutput SampleTransform(
	const VansTimelineSection& section,
	VansTimelineTick tick,
	const VansTimelineTransformTrackConfig& config)
{
	VansTimelineTransformOutput output;
	output.space = config.space;
	for (const auto& channel : section.channels)
	{
		const auto value = SampleChannel(channel, tick);
		if (!value) continue;
		const std::string name = Lower(channel.name);
		if ((name == "position" || name == "translation") && std::holds_alternative<VansTimelineVec3>(*value))
		{
			output.position = std::get<VansTimelineVec3>(*value); output.channels |= 0x7u;
		}
		else if (name == "position.x" || name == "translation.x") { output.position.value[0] = NumericValue(*value, 0.0); output.channels |= 0x1u; }
		else if (name == "position.y" || name == "translation.y") { output.position.value[1] = NumericValue(*value, 0.0); output.channels |= 0x2u; }
		else if (name == "position.z" || name == "translation.z") { output.position.value[2] = NumericValue(*value, 0.0); output.channels |= 0x4u; }
		else if (name == "rotation" && std::holds_alternative<VansTimelineQuaternion>(*value))
		{
			output.rotation = std::get<VansTimelineQuaternion>(*value); output.channels |= 0x78u;
		}
		else if (name == "scale" && std::holds_alternative<VansTimelineVec3>(*value))
		{
			output.scale = std::get<VansTimelineVec3>(*value); output.channels |= 0x380u;
		}
		else if (name == "scale.x") { output.scale.value[0] = NumericValue(*value, 1.0); output.channels |= 0x80u; }
		else if (name == "scale.y") { output.scale.value[1] = NumericValue(*value, 1.0); output.channels |= 0x100u; }
		else if (name == "scale.z") { output.scale.value[2] = NumericValue(*value, 1.0); output.channels |= 0x200u; }
	}
	output.channels &= config.channels;
	return output;
}

void PushOutput(
	const VansCompiledTimelineTrack& compiledTrack,
	const VansResolvedTimelineTarget& target,
	VansTimelineOutputValue value,
	std::string propertyKey,
	const std::string& writerId,
	std::vector<VansTimelineEvaluationOutput>& outputs,
	std::int32_t hierarchicalBias = 0)
{
	VansTimelineEvaluationOutput output;
	output.target = target;
	output.trackType = compiledTrack.source.type;
	output.value = std::move(value);
	output.blendMode = compiledTrack.source.blendMode;
	output.hierarchicalBias = hierarchicalBias;
	output.priority = compiledTrack.source.priority;
	output.trackOrder = compiledTrack.source.order;
	output.sourceTrackId = compiledTrack.source.id;
	output.propertyKey = std::move(propertyKey);
	output.writerId = writerId;
	outputs.push_back(std::move(output));
}

const VansTimelineBinding* FindBinding(const VansTimelineAsset& asset, const VansTimelineId& id)
{
	const auto found = std::find_if(asset.bindings.begin(), asset.bindings.end(), [&](const auto& binding)
	{
		return binding.id == id;
	});
	return found == asset.bindings.end() ? nullptr : &*found;
}

void EvaluateSection(
	const VansCompiledTimeline& timeline,
	const VansCompiledTimelineTrack& compiledTrack,
	const VansTimelineSection& section,
	const VansTimelineEvaluationSegment& segment,
	const VansResolvedTimelineTarget& target,
	VansTimelineBindingResolver& bindings,
	const std::string& writerId,
	std::vector<VansTimelineEvaluationOutput>& outputs)
{
	const VansTimelineTrack& track = compiledTrack.source;
	const std::size_t firstOutput = outputs.size();
	const bool inside = IsInside(section, segment.currentTick);
	const bool entered = EdgesAllowed(segment) && Crossed(segment, section.startTick);
	const bool exited = EdgesAllowed(segment) && Crossed(segment, section.startTick + section.durationTicks);
	if (!inside && !entered && !exited) return;
	const auto mapped = VansTimelineSectionTimeMapper::Map(
		inside ? segment.currentTick : (entered ? section.startTick : section.startTick + section.durationTicks - 1),
		section.startTick, section.durationTicks, section.sourceInTick, section.sourceOutTick,
		section.playRate, section.reverse, section.loopMode, section.loopCount);
	const VansTimelineTrackConfig& config = EffectiveConfig(track, section);

	switch (track.type)
	{
	case VansTimelineTrackType::Transform:
	{
		if (!inside) break;
		const auto& typed = std::get<VansTimelineTransformTrackConfig>(config);
		auto value = SampleTransform(section, mapped.active ? mapped.localTick : segment.currentTick, typed);
		if (value.channels != 0) PushOutput(compiledTrack, target, std::move(value), "transform", writerId, outputs);
		break;
	}
	case VansTimelineTrackType::Property:
	{
		if (!inside || section.channels.empty()) break;
		const auto sampled = SampleChannel(section.channels.front(), mapped.active ? mapped.localTick : segment.currentTick);
		if (!sampled) break;
		const auto& typed = std::get<VansTimelinePropertyTrackConfig>(config);
		PushOutput(compiledTrack, target,
			VansTimelinePropertyOutput{ typed.componentTypeId, typed.descriptorId, typed.propertyPath, typed.valueType, *sampled },
			"property:" + typed.descriptorId, writerId, outputs);
		break;
	}
	case VansTimelineTrackType::Activation:
	{
		const auto& typed = std::get<VansTimelineActivationTrackConfig>(config);
		bool emit = inside;
		bool active = typed.stateWhenInside;
		const std::string after = Lower(typed.stateAfter);
		const std::string before = Lower(typed.stateBefore);
		if (!inside && exited && after != "restore" && after != "donothing")
		{
			emit = true;
			active = after == "active";
		}
		else if (!inside && segment.currentTick < section.startTick &&
			before != "restore" && before != "donothing")
		{
			emit = true;
			active = before == "active";
		}
		else if (!inside) emit = false;
		if (emit)
		{
			PushOutput(compiledTrack, target, VansTimelineActivationOutput{ typed.scope, active },
				"activation:" + typed.scope, writerId, outputs);
		}
		break;
	}
	case VansTimelineTrackType::Constraint:
	{
		if (!inside) break;
		const auto& typed = std::get<VansTimelineConstraintTrackConfig>(config);
		double weight = typed.weight;
		if (!section.channels.empty()) if (const auto sampled = SampleChannel(section.channels.front(), mapped.localTick)) weight = NumericValue(*sampled, weight);
		VansResolvedTimelineTarget sourceTarget;
		VansResolvedTimelineTarget constrainedTarget = target;
		if (const auto* sourceBinding = FindBinding(timeline.Source(), typed.sourceBindingId))
			sourceTarget = bindings.ResolveOne(*sourceBinding);
		if (const auto* targetBinding = FindBinding(timeline.Source(), typed.targetBindingId))
			constrainedTarget = bindings.ResolveOne(*targetBinding);
		PushOutput(compiledTrack, constrainedTarget,
			VansTimelineConstraintOutput{ typed, sourceTarget, constrainedTarget, weight },
			"constraint", writerId, outputs);
		break;
	}
	case VansTimelineTrackType::AnimationClip:
	{
		const auto& typed = std::get<VansTimelineAnimationTrackConfig>(config);
		double weight = typed.weight * SectionEnvelope(section, segment.currentTick);
		for (const auto& channel : section.channels)
			if (Lower(channel.name) == "weight")
				if (const auto sampled = SampleChannel(channel, mapped.localTick))
					weight *= NumericValue(*sampled, 1.0);
		VansTimelineAnimationOutput value{ section.assetGuid, section.assetPath, typed.slot, typed.layer,
			mapped.localTick, VansTimelineTime::TickToSeconds(mapped.localTick, timeline.Source().timebase),
			weight,
			VansTimelineTime::TickToSeconds(section.easeInTicks, timeline.Source().timebase),
			VansTimelineTime::TickToSeconds(section.easeOutTicks, timeline.Source().timebase),
			typed.additive, typed.avatarMaskGuid, typed.avatarMaskPath, typed.syncGroup, typed.markerSync,
			inside, entered, exited, typed.rootMotionPolicy };
		PushOutput(compiledTrack, target, std::move(value), "animation:" + typed.slot, writerId, outputs);
		break;
	}
	case VansTimelineTrackType::AnimatorParameter:
	{
		const auto& typed = std::get<VansTimelineAnimatorParameterTrackConfig>(config);
		if (typed.parameterType == "Trigger")
		{
			if (!EdgesAllowed(segment)) break;
			const std::string direction = Lower(typed.firePolicy);
			if (direction != "both" && ((segment.playbackDirection < 0) != (direction == "reverse"))) break;
			const std::string seek = Lower(typed.seekPolicy);
			if ((segment.reason == VansTimelineEvaluationReason::Jump ||
				segment.reason == VansTimelineEvaluationReason::Scrub ||
				segment.reason == VansTimelineEvaluationReason::Step) &&
				!((seek == "exacttick" && segment.seekPolicy == VansTimelineSeekPolicy::ExactTick) ||
					(seek == "crossed" && (segment.seekPolicy == VansTimelineSeekPolicy::AllEdges ||
						segment.seekPolicy == VansTimelineSeekPolicy::SafeEdges)))) break;
			for (const auto& channel : section.channels)
				for (const auto& key : channel.keys)
					if (Crossed(segment, section.startTick + key.tick) &&
						(!std::holds_alternative<bool>(key.value) || std::get<bool>(key.value)))
						PushOutput(compiledTrack, target,
							VansTimelineAnimatorParameterOutput{ typed.parameterName, typed.parameterType,
								true, true, typed.missingParameterPolicy },
							"animatorParameter:" + typed.parameterName + ":" + key.id, writerId, outputs);
		}
		else if (inside && !section.channels.empty())
		{
			if (const auto sampled = SampleChannel(section.channels.front(), mapped.localTick))
				PushOutput(compiledTrack, target,
					VansTimelineAnimatorParameterOutput{ typed.parameterName, typed.parameterType,
						*sampled, false, typed.missingParameterPolicy },
					"animatorParameter:" + typed.parameterName, writerId, outputs);
		}
		break;
	}
	case VansTimelineTrackType::BoneOverride:
	{
		if (!inside) break;
		const auto& typed = std::get<VansTimelineBoneOverrideTrackConfig>(config);
		VansTimelineTransformTrackConfig transformConfig;
		transformConfig.space = typed.space;
		VansResolvedTimelineTarget ikTarget;
		VansResolvedTimelineTarget poleTarget;
		if (const auto* binding = FindBinding(timeline.Source(), typed.ikTargetBindingId))
			ikTarget = bindings.ResolveOne(*binding);
		if (const auto* binding = FindBinding(timeline.Source(), typed.poleBindingId))
			poleTarget = bindings.ResolveOne(*binding);
		PushOutput(compiledTrack, target,
			VansTimelineBoneOverrideOutput{ typed, SampleTransform(section, mapped.localTick, transformConfig),
				ikTarget, poleTarget },
			"boneOverride:" + typed.boneId, writerId, outputs);
		break;
	}
	case VansTimelineTrackType::Audio:
	{
		const auto& typed = std::get<VansTimelineAudioTrackConfig>(config);
		double envelope = SectionEnvelope(section, segment.currentTick);
		const double sectionSeconds = VansTimelineTime::TickToSeconds(
			std::max<VansTimelineTick>(0, segment.currentTick - section.startTick), timeline.Source().timebase);
		const double remainingSeconds = VansTimelineTime::TickToSeconds(
			std::max<VansTimelineTick>(0, section.startTick + section.durationTicks - segment.currentTick),
			timeline.Source().timebase);
		if (typed.fadeInSeconds > 0.0) envelope = std::min(envelope, sectionSeconds / typed.fadeInSeconds);
		if (typed.fadeOutSeconds > 0.0) envelope = std::min(envelope, remainingSeconds / typed.fadeOutSeconds);
		PushOutput(compiledTrack, target,
			VansTimelineAudioOutput{ section.assetGuid, section.assetPath, typed,
				VansTimelineTime::TickToSeconds(mapped.localTick, timeline.Source().timebase),
				ClampUnit(envelope), section.loopMode != VansTimelineLoopMode::None, inside, entered, exited },
			"audio:" + section.id, writerId, outputs);
		break;
	}
	case VansTimelineTrackType::Media:
	{
		const auto& typed = std::get<VansTimelineMediaTrackConfig>(config);
		PushOutput(compiledTrack, target,
			VansTimelineMediaOutput{ section.assetGuid, section.assetPath, typed,
				VansTimelineTime::TickToSeconds(mapped.localTick, timeline.Source().timebase),
				std::abs(section.playRate), section.reverse, inside, entered, exited },
			"media:" + section.id, writerId, outputs);
		break;
	}
	case VansTimelineTrackType::Particle:
	{
		const auto& typed = std::get<VansTimelineParticleTrackConfig>(config);
		PushOutput(compiledTrack, target,
			VansTimelineParticleOutput{ typed,
				VansTimelineTime::TickToSeconds(mapped.localTick, timeline.Source().timebase),
				VansTimelineTime::TickToSeconds(typed.prewarmTicks, timeline.Source().timebase),
				inside, entered, exited },
			"particle", writerId, outputs);
		break;
	}
	case VansTimelineTrackType::CameraCut:
	{
		const auto& typed = std::get<VansTimelineCameraCutTrackConfig>(config);
		VansResolvedTimelineTarget cameraTarget;
		VansResolvedTimelineTarget targetCameraTarget;
		if (const auto* cameraBinding = FindBinding(timeline.Source(), typed.cameraBindingId))
			cameraTarget = bindings.ResolveOne(*cameraBinding);
		if (!typed.targetCameraBindingId.empty())
			if (const auto* targetCameraBinding = FindBinding(timeline.Source(), typed.targetCameraBindingId))
				targetCameraTarget = bindings.ResolveOne(*targetCameraBinding);
		double blendAlpha = 1.0;
		if (typed.cutMode == "Blend" && typed.blendDurationTicks > 0)
			blendAlpha = EvaluateBlendCurve(typed.blendCurve,
				static_cast<double>(std::max<VansTimelineTick>(0, segment.currentTick - section.startTick)) /
				static_cast<double>(typed.blendDurationTicks));
		PushOutput(compiledTrack, {},
			VansTimelineCameraCutOutput{ typed.cameraBindingId, typed.targetCameraBindingId,
				cameraTarget, targetCameraTarget, typed, inside, blendAlpha },
			"cameraCut", writerId, outputs);
		outputs.back().priority += typed.priority;
		break;
	}
	case VansTimelineTrackType::CameraProperty:
	{
		if (!inside) break;
		for (const auto& channel : section.channels)
			if (const auto sampled = SampleChannel(channel, mapped.localTick))
				PushOutput(compiledTrack, target, VansTimelineCameraPropertyOutput{ channel.name, *sampled },
					"camera:" + channel.name, writerId, outputs);
		break;
	}
	case VansTimelineTrackType::CameraShake:
	{
		if (!inside) break;
		const auto& typed = std::get<VansTimelineCameraShakeTrackConfig>(config);
		VansTimelineCameraShakeOutput value;
		value.config = typed;
		value.active = inside;
		value.weight = SectionEnvelope(section, segment.currentTick) * std::max(0.0, typed.amplitudeScale);
		bool hasOffset = false;
		for (const auto& channel : section.channels)
		{
			const auto sampled = SampleChannel(channel, mapped.localTick);
			if (!sampled || !std::holds_alternative<VansTimelineVec3>(*sampled)) continue;
			const std::string name = Lower(channel.name);
			if (typed.position && (name == "positionoffset" || name == "position" || name == "offset"))
			{
				value.positionOffset = std::get<VansTimelineVec3>(*sampled);
				hasOffset = true;
			}
			else if (typed.rotation && (name == "rotationoffset" || name == "rotation" || name == "euler"))
			{
				value.rotationOffset = std::get<VansTimelineVec3>(*sampled);
				hasOffset = true;
			}
		}
		if (hasOffset)
		{
			PushOutput(compiledTrack, target, std::move(value), "cameraShake", writerId, outputs);
			outputs.back().priority += typed.priority;
		}
		break;
	}
	case VansTimelineTrackType::FadePostProcess:
	{
		if (!inside) break;
		const auto& typed = std::get<VansTimelineFadePostProcessTrackConfig>(config);
		double value = typed.blendWeight;
		if (!section.channels.empty()) if (const auto sampled = SampleChannel(section.channels.front(), mapped.localTick)) value = NumericValue(*sampled, value);
		value *= SectionEnvelope(section, segment.currentTick);
		PushOutput(compiledTrack, target, VansTimelineFadePostProcessOutput{ typed, value },
			"postProcess:" + typed.mode, writerId, outputs);
		outputs.back().priority += typed.priority;
		break;
	}
	case VansTimelineTrackType::Light:
	{
		if (!inside) break;
		for (const auto& channel : section.channels)
			if (const auto sampled = SampleChannel(channel, mapped.localTick))
				PushOutput(compiledTrack, target, VansTimelineLightOutput{ channel.name, *sampled },
					"light:" + channel.name, writerId, outputs);
		break;
	}
	case VansTimelineTrackType::MaterialParameter:
	{
		if (!inside || section.channels.empty()) break;
		const auto& typed = std::get<VansTimelineMaterialParameterTrackConfig>(config);
		if (const auto sampled = SampleChannel(section.channels.front(), mapped.localTick))
			PushOutput(compiledTrack, target,
				VansTimelineMaterialParameterOutput{ typed.materialSlotId, typed.parameterName, *sampled, typed.instancePolicy },
				"material:" + typed.materialSlotId + ":" + typed.parameterName, writerId, outputs);
		break;
	}
	case VansTimelineTrackType::MaterialSwitch:
	{
		if (!inside) break;
		const auto& typed = std::get<VansTimelineMaterialSwitchTrackConfig>(config);
		PushOutput(compiledTrack, target,
			VansTimelineMaterialSwitchOutput{ typed.materialSlotId, section.assetGuid, section.assetPath },
			"materialSwitch:" + typed.materialSlotId, writerId, outputs);
		break;
	}
	case VansTimelineTrackType::UIState:
	{
		const auto& typed = std::get<VansTimelineUIStateTrackConfig>(config);
		VansTimelineKeyValue value;
		if (!section.channels.empty()) if (const auto sampled = SampleChannel(section.channels.front(), mapped.localTick)) value = *sampled;
		PushOutput(compiledTrack, target, VansTimelineUIOutput{ typed, std::move(value), entered, exited },
			"ui:" + typed.screen + ":" + typed.element + ":" + typed.action, writerId, outputs);
		break;
	}
	case VansTimelineTrackType::EventSignal:
	{
		if (!EdgesAllowed(segment)) break;
		const auto& typed = std::get<VansTimelineEventTrackConfig>(config);
		if (!EventDirectionAllowed(typed, segment) || !EventSeekAllowed(typed, segment)) break;
		if (Lower(typed.loopPolicy) == "firstlooponly" && segment.loopIteration != 0) break;
		const bool safeOnly = segment.seekPolicy == VansTimelineSeekPolicy::SafeEdges;
		if (safeOnly && !typed.editorSafe) break;
		for (const auto& channel : section.channels)
			for (const auto& key : channel.keys)
				if (Crossed(segment, section.startTick + key.tick))
					PushOutput(compiledTrack, target, VansTimelineEventOutput{ typed, key.value, key.tick, segment.loopIteration },
						"event:" + typed.signalId + ":" + key.id, writerId, outputs);
		break;
	}
	case VansTimelineTrackType::SubTimeline:
	{
		const auto& typed = std::get<VansTimelineSubTimelineTrackConfig>(config);
		PushOutput(compiledTrack, target,
			VansTimelineSubTimelineOutput{ section.assetGuid, section.assetPath, typed, mapped.localTick, inside, entered, exited },
			"subTimeline:" + section.id, writerId, outputs, typed.hierarchicalBias);
		break;
	}
	case VansTimelineTrackType::TimeScale:
	{
		if (!inside) break;
		const auto& typed = std::get<VansTimelineTimeScaleTrackConfig>(config);
		double scale = 1.0;
		if (!section.channels.empty()) if (const auto sampled = SampleChannel(section.channels.front(), mapped.localTick)) scale = NumericValue(*sampled, scale);
		PushOutput(compiledTrack, target, VansTimelineTimeScaleOutput{ std::clamp(scale, typed.minimum, typed.maximum), typed },
			"localTimeWarp", writerId, outputs);
		break;
	}
	case VansTimelineTrackType::Spawnable:
	case VansTimelineTrackType::SceneState:
	case VansTimelineTrackType::Custom:
		break;
	}

	VansTimelineCompletionMode completionMode = section.completionMode;
	if (completionMode == VansTimelineCompletionMode::ProjectDefault)
		completionMode = track.completionMode;
	if (completionMode == VansTimelineCompletionMode::ProjectDefault)
		completionMode = timeline.Source().defaultCompletionMode;
	const std::string sectionWriterId = writerId + "/" + track.id + "/" + section.id;
	for (std::size_t index = firstOutput; index < outputs.size(); ++index)
	{
		VansTimelineEvaluationOutput& output = outputs[index];
		output.sourceSectionId = section.id;
		output.rootWriterId = writerId;
		output.writerId = sectionWriterId;
		output.completionMode = completionMode;
		output.retainsPreAnimatedState = inside;
		if (track.type == VansTimelineTrackType::Activation && !inside)
		{
			const auto& activation = std::get<VansTimelineActivationTrackConfig>(config);
			if ((exited && Lower(activation.stateAfter) != "restore") ||
				(segment.currentTick < section.startTick && Lower(activation.stateBefore) != "restore"))
				output.completionMode = VansTimelineCompletionMode::KeepState;
		}
	}
}
}

double VansTimelineEvaluator::EvaluateLocalTimeScale(
	const VansCompiledTimeline& timeline,
	VansTimelineTick tick)
{
	double result = 1.0;
	for (const VansCompiledTimelineTrack& compiled : timeline.PostScriptTracks())
	{
		if (compiled.source.type != VansTimelineTrackType::TimeScale ||
			!compiled.source.enabled || compiled.source.runtimeMuted)
			continue;
		for (const VansTimelineSection& section : compiled.source.sections)
		{
			if (!IsInside(section, tick))
				continue;
			const auto mapped = VansTimelineSectionTimeMapper::Map(
				tick, section.startTick, section.durationTicks, section.sourceInTick, section.sourceOutTick,
				section.playRate, section.reverse, section.loopMode, section.loopCount);
			const VansTimelineTrackConfig& config = EffectiveConfig(compiled.source, section);
			const auto* typed = std::get_if<VansTimelineTimeScaleTrackConfig>(&config);
			if (!typed || typed->scope != "LocalTimeWarp")
				continue;
			double scale = 1.0;
			if (!section.channels.empty())
				if (const auto sampled = SampleChannel(section.channels.front(), mapped.localTick))
					scale = NumericValue(*sampled, scale);
			scale = std::clamp(scale, typed->minimum, typed->maximum);
			switch (compiled.source.blendMode)
			{
			case VansTimelineBlendMode::Additive: result += scale; break;
			case VansTimelineBlendMode::Multiply:
			case VansTimelineBlendMode::Relative: result *= scale; break;
			case VansTimelineBlendMode::Override: result = scale; break;
			}
		}
	}
	return std::max(0.0, result);
}

void VansTimelineEvaluator::Evaluate(
	const VansCompiledTimeline& timeline,
	VansTimelineEvaluationPhase phase,
	const std::vector<VansTimelineEvaluationSegment>& segments,
	const std::unordered_map<std::string, VansTimelineKeyValue>& parameters,
	VansTimelineBindingResolver& bindings,
	const std::string& writerId,
	std::vector<VansTimelineEvaluationOutput>& outputs,
	VansTimelineDiagnostics& diagnostics)
{
	outputs.clear();
	bindings.Resolve(timeline.Source().bindings, diagnostics);
	const auto& tracks = phase == VansTimelineEvaluationPhase::PostScript
		? timeline.PostScriptTracks() : timeline.CameraTracks();
	for (const auto& segment : segments)
	{
		for (const auto& compiledTrack : tracks)
		{
			const VansTimelineTrack& track = compiledTrack.source;
			if (!ConditionPasses(track.condition, parameters)) continue;
			VansResolvedTimelineTarget target;
			if (!track.bindingId.empty())
			{
				if (const auto* binding = FindBinding(timeline.Source(), track.bindingId))
					target = bindings.ResolveOne(*binding);
				if (!target.valid) continue;
			}
			for (const auto& section : track.sections)
				EvaluateSection(timeline, compiledTrack, section, segment, target, bindings, writerId, outputs);
		}
	}
	std::stable_sort(outputs.begin(), outputs.end(), [](const auto& left, const auto& right)
	{
		if (left.hierarchicalBias != right.hierarchicalBias) return left.hierarchicalBias > right.hierarchicalBias;
		if (left.priority != right.priority) return left.priority > right.priority;
		if (left.trackOrder != right.trackOrder) return left.trackOrder < right.trackOrder;
		return left.sourceTrackId < right.sourceTrackId;
	});
}
}
