#include "VansTimelineTypes.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace Vans
{
namespace
{
VansTimelineTick ApplyRounding(long double value, VansTimelineRoundingMode rounding)
{
	long double rounded = 0.0L;
	switch (rounding)
	{
	case VansTimelineRoundingMode::Floor: rounded = std::floor(value); break;
	case VansTimelineRoundingMode::Ceil: rounded = std::ceil(value); break;
	case VansTimelineRoundingMode::Round: rounded = std::round(value); break;
	}
	const long double minimum = static_cast<long double>(std::numeric_limits<VansTimelineTick>::min());
	const long double maximum = static_cast<long double>(std::numeric_limits<VansTimelineTick>::max());
	return static_cast<VansTimelineTick>(std::clamp(rounded, minimum, maximum));
}
}

VansTimelineValueType VansTimelineTypeOf(const VansTimelineValue& value)
{
	return std::visit([](const auto& item)
	{
		using Type = std::decay_t<decltype(item)>;
		if constexpr (std::is_same_v<Type, std::monostate>) return VansTimelineValueType::Null;
		else if constexpr (std::is_same_v<Type, bool>) return VansTimelineValueType::Bool;
		else if constexpr (std::is_same_v<Type, std::int32_t>) return VansTimelineValueType::Int32;
		else if constexpr (std::is_same_v<Type, std::int64_t>) return VansTimelineValueType::Int64;
		else if constexpr (std::is_same_v<Type, float>) return VansTimelineValueType::Float;
		else if constexpr (std::is_same_v<Type, double>) return VansTimelineValueType::Double;
		else if constexpr (std::is_same_v<Type, std::string>) return VansTimelineValueType::String;
		else if constexpr (std::is_same_v<Type, VansTimelineVec2>) return VansTimelineValueType::Vec2;
		else if constexpr (std::is_same_v<Type, VansTimelineVec3>) return VansTimelineValueType::Vec3;
		else if constexpr (std::is_same_v<Type, VansTimelineVec4>) return VansTimelineValueType::Vec4;
		else if constexpr (std::is_same_v<Type, VansTimelineQuaternion>) return VansTimelineValueType::Quaternion;
		else if constexpr (std::is_same_v<Type, VansTimelineColorLinear>) return VansTimelineValueType::ColorLinear;
		else if constexpr (std::is_same_v<Type, VansTimelineColorSrgb>) return VansTimelineValueType::ColorSrgb;
		else if constexpr (std::is_same_v<Type, VansTimelineObjectReference>) return VansTimelineValueType::ObjectReference;
		else return VansTimelineValueType::Struct;
	}, value);
}

bool VansTimelineValuesEqual(const VansTimelineValue& left, const VansTimelineValue& right)
{
	if (left.index() != right.index()) return false;
	return std::visit([&](const auto& leftValue)
	{
		using Type = std::decay_t<decltype(leftValue)>;
		const Type* rightValue = std::get_if<Type>(&right);
		if (!rightValue) return false;
		if constexpr (std::is_same_v<Type, std::monostate>) return true;
		else if constexpr (std::is_same_v<Type, bool> || std::is_integral_v<Type> ||
			std::is_floating_point_v<Type> || std::is_same_v<Type, std::string>) return leftValue == *rightValue;
		else if constexpr (std::is_same_v<Type, VansTimelineObjectReference>)
			return leftValue.guid == rightValue->guid && leftValue.path == rightValue->path &&
				leftValue.objectKind == rightValue->objectKind;
		else if constexpr (std::is_same_v<Type, VansTimelineStructValue>)
			return leftValue.type == rightValue->type;
		else return leftValue.value == rightValue->value;
	}, left);
}

double VansTimelineTime::TickToSeconds(VansTimelineTick tick, const VansTimelineTimebase& timebase)
{
	if (timebase.ticksPerSecond <= 0)
		return 0.0;
	return static_cast<double>(tick) / static_cast<double>(timebase.ticksPerSecond);
}

VansTimelineTick VansTimelineTime::SecondsToTick(
	double seconds,
	const VansTimelineTimebase& timebase,
	VansTimelineRoundingMode rounding)
{
	if (!std::isfinite(seconds) || timebase.ticksPerSecond <= 0)
		return 0;
	return ApplyRounding(
		static_cast<long double>(seconds) * static_cast<long double>(timebase.ticksPerSecond),
		rounding);
}

VansTimelineTick VansTimelineTime::FrameToTick(std::int64_t frame, const VansTimelineTimebase& timebase)
{
	if (timebase.ticksPerSecond <= 0 || timebase.displayRateNumerator <= 0 ||
		timebase.displayRateDenominator <= 0)
	{
		return 0;
	}
	return ApplyRounding(
		static_cast<long double>(frame) * static_cast<long double>(timebase.ticksPerSecond) *
		static_cast<long double>(timebase.displayRateDenominator) /
		static_cast<long double>(timebase.displayRateNumerator),
		VansTimelineRoundingMode::Round);
}

std::int64_t VansTimelineTime::TickToFrame(
	VansTimelineTick tick,
	const VansTimelineTimebase& timebase,
	VansTimelineRoundingMode rounding)
{
	if (timebase.ticksPerSecond <= 0 || timebase.displayRateNumerator <= 0 ||
		timebase.displayRateDenominator <= 0)
	{
		return 0;
	}
	return ApplyRounding(
		static_cast<long double>(tick) * static_cast<long double>(timebase.displayRateNumerator) /
		(static_cast<long double>(timebase.ticksPerSecond) *
		 static_cast<long double>(timebase.displayRateDenominator)),
		rounding);
}

std::string VansTimelineTime::FormatTimecode(
	VansTimelineTick tick,
	const VansTimelineTimebase& timebase,
	bool dropFrame)
{
	const bool supportedDropRate = timebase.displayRateDenominator == 1001 &&
		(timebase.displayRateNumerator == 30000 || timebase.displayRateNumerator == 60000);
	const std::int64_t nominalFps = std::max<std::int64_t>(1,
		static_cast<std::int64_t>(std::llround(
			static_cast<double>(timebase.displayRateNumerator) /
			static_cast<double>(std::max(1, timebase.displayRateDenominator)))));
	std::int64_t frame = TickToFrame(tick, timebase, VansTimelineRoundingMode::Floor);
	const bool negative = frame < 0;
	frame = std::llabs(frame);
	if (dropFrame && supportedDropRate)
	{
		const std::int64_t dropCount = nominalFps == 60 ? 4 : 2;
		const std::int64_t framesPerTenMinutes = nominalFps * 60 * 10 - dropCount * 9;
		const std::int64_t framesPerMinute = nominalFps * 60 - dropCount;
		const std::int64_t tenMinuteBlocks = frame / framesPerTenMinutes;
		const std::int64_t remainder = frame % framesPerTenMinutes;
		frame += dropCount * 9 * tenMinuteBlocks;
		if (remainder >= dropCount)
			frame += dropCount * ((remainder - dropCount) / framesPerMinute);
	}
	const std::int64_t hours = frame / (nominalFps * 3600);
	frame %= nominalFps * 3600;
	const std::int64_t minutes = frame / (nominalFps * 60);
	frame %= nominalFps * 60;
	const std::int64_t seconds = frame / nominalFps;
	const std::int64_t frames = frame % nominalFps;
	std::ostringstream stream;
	if (negative) stream << '-';
	stream << std::setfill('0') << std::setw(2) << hours << ':'
		<< std::setw(2) << minutes << ':' << std::setw(2) << seconds
		<< ((dropFrame && supportedDropRate) ? ';' : ':') << std::setw(2) << frames;
	return stream.str();
}

VansTimelineSectionTimeMap VansTimelineSectionTimeMapper::Map(
	VansTimelineTick timelineTick,
	VansTimelineTick startTick,
	VansTimelineTick durationTicks,
	VansTimelineTick sourceInTick,
	VansTimelineTick sourceOutTick,
	double playRate,
	bool reverse,
	VansTimelineLoopMode loopMode,
	std::int32_t loopCount)
{
	VansTimelineSectionTimeMap result;
	if (durationTicks <= 0 || playRate <= 0.0 || !std::isfinite(playRate) ||
		timelineTick < startTick || timelineTick >= startTick + durationTicks)
	{
		return result;
	}
	const VansTimelineTick sourceDuration = sourceOutTick > sourceInTick
		? sourceOutTick - sourceInTick
		: durationTicks;
	if (sourceDuration <= 0)
		return result;
	const long double scaled = static_cast<long double>(timelineTick - startTick) * playRate;
	VansTimelineTick sourceOffset = ApplyRounding(scaled, VansTimelineRoundingMode::Floor);
	const std::int32_t availableLoops = loopMode == VansTimelineLoopMode::None ? 1 : std::max(1, loopCount);
	result.loopIndex = static_cast<std::int32_t>(sourceOffset / sourceDuration);
	if (result.loopIndex >= availableLoops)
		return result;
	VansTimelineTick inLoop = sourceOffset % sourceDuration;
	const bool pingPongReverse = loopMode == VansTimelineLoopMode::PingPong && (result.loopIndex % 2) != 0;
	result.reversed = reverse != pingPongReverse;
	if (result.reversed)
		inLoop = std::max<VansTimelineTick>(0, sourceDuration - 1 - inLoop);
	result.localTick = sourceInTick + inLoop;
	result.active = true;
	return result;
}

bool VansTimelineEdgeCrossing::Crossed(
	VansTimelineTick previousTick,
	VansTimelineTick currentTick,
	VansTimelineTick keyTick,
	bool exactSeek)
{
	if (exactSeek)
		return currentTick == keyTick;
	if (currentTick >= previousTick)
		return previousTick < keyTick && keyTick <= currentTick;
	return currentTick <= keyTick && keyTick < previousTick;
}
}
