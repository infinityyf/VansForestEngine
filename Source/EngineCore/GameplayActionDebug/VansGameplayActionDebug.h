#pragma once

#include "../GameplayActionCore/VansGameplayRuntime.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Vans
{
struct VansActionHostDebugSnapshot
{
	VansEntityHandle owner;
	bool enabled = false;
	bool commitFrozen = false;
	std::vector<std::pair<VansGameplayTagId, std::uint32_t>> tags;
	std::vector<VansAttributeSnapshot> attributes;
	std::vector<VansActiveEffectSnapshot> effects;
	std::size_t activeCueCount = 0;
	std::vector<VansGrantedActionSpecSnapshot> grants;
	std::vector<VansActionInstanceSnapshot> actions;
};

struct VansGameplayDebugSnapshot
{
	std::uint64_t frame = 0;
	double timeSeconds = 0.0;
	std::uint64_t contentManifestHash = 0;
	std::vector<VansActionHostDebugSnapshot> hosts;
};

class VansGameplayActionDebugService
{
public:
	static VansGameplayDebugSnapshot Capture(const VansGameplayRuntime& runtime,
		std::uint64_t frame, double timeSeconds, std::uint64_t contentManifestHash = 0);
};

enum class VansActionBreakpointKind : std::uint8_t
{
	Action,
	State,
	Node,
	Event,
	Error,
	Prediction,
	Attribute,
	Window
};

enum class VansActionBreakpointComparison : std::uint8_t
{
	Changed,
	Equal,
	Less,
	LessOrEqual,
	Greater,
	GreaterOrEqual
};

struct VansActionBreakpoint
{
	std::uint64_t id = 0;
	VansActionBreakpointKind kind = VansActionBreakpointKind::Action;
	std::string expression;
	VansActionId action;
	VansActionInstanceState state = VansActionInstanceState::Running;
	std::string node;
	std::string event;
	VansActionError error = VansActionError::None;
	VansPredictionKey prediction;
	VansAttributeId attribute;
	VansActionBreakpointComparison comparison = VansActionBreakpointComparison::Changed;
	double value = 0.0;
	double epsilon = 1e-6;
	std::string window;
	bool enabled = true;
};

struct VansActionBreakpointHit
{
	std::uint64_t breakpoint = 0;
	VansEntityHandle owner;
	VansActionHandle action;
	std::string reason;
};

class VansGameplayActionBreakpointSet
{
public:
	std::uint64_t Add(VansActionBreakpoint breakpoint);
	bool Remove(std::uint64_t breakpoint);
	bool SetEnabled(std::uint64_t breakpoint, bool enabled);
	void Clear();
	std::vector<VansActionBreakpointHit> Evaluate(
		const VansGameplayDebugSnapshot& previous,
		const VansGameplayDebugSnapshot& current) const;
	const std::vector<VansActionBreakpoint>& All() const { return m_Breakpoints; }

private:
	std::vector<VansActionBreakpoint> m_Breakpoints;
	std::uint64_t m_NextId = 1;
};

struct VansGameplayTraceArchive
{
	std::uint32_t formatVersion = 1;
	std::uint64_t contentManifestHash = 0;
	std::vector<VansGameplayDebugSnapshot> frames;
};

class VansGameplayTraceRecorder
{
public:
	bool Begin(std::uint64_t contentManifestHash, std::size_t maximumFrames,
		std::size_t maximumApproximateBytes, std::string& error);
	bool Record(VansGameplayDebugSnapshot snapshot, std::string& error);
	VansGameplayTraceArchive End();
	bool IsRecording() const { return m_Recording; }
	static bool Save(const std::filesystem::path& path,
		const VansGameplayTraceArchive& archive, std::string& error);
	static bool Load(const std::filesystem::path& path,
		VansGameplayTraceArchive& archive, std::string& error);

private:
	VansGameplayTraceArchive m_Archive;
	std::size_t m_MaximumFrames = 0;
	std::size_t m_MaximumApproximateBytes = 0;
	std::size_t m_ApproximateBytes = 0;
	bool m_Recording = false;
};

class VansGameplayReplaySession
{
public:
	bool Load(VansGameplayTraceArchive archive, std::string& error);
	bool SeekFrame(std::size_t frame);
	bool Step(std::int32_t direction);
	const VansGameplayDebugSnapshot* Current() const;
	std::size_t FrameIndex() const { return m_Frame; }
	std::size_t FrameCount() const { return m_Archive.frames.size(); }

private:
	VansGameplayTraceArchive m_Archive;
	std::size_t m_Frame = 0;
};
}
