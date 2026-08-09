#pragma once

#include "VansTimelineDependencyBuilder.h"
#include "VansTimelineValidator.h"

#include <memory>

namespace Vans
{
enum class VansTimelineEvaluationPhase
{
	PostScript,
	Camera
};

struct VansCompiledTimelineTrack
{
	VansTimelineTrack source;
	VansTimelineEvaluationPhase phase = VansTimelineEvaluationPhase::PostScript;
	bool evaluatesContinuousValues = false;
	bool evaluatesEdges = false;
};

class VansCompiledTimeline
{
public:
	const VansTimelineAsset& Source() const { return m_Source; }
	const std::vector<VansCompiledTimelineTrack>& PostScriptTracks() const { return m_PostScriptTracks; }
	const std::vector<VansCompiledTimelineTrack>& CameraTracks() const { return m_CameraTracks; }
	const std::vector<VansTimelineTick>& DeterminismFences() const { return m_DeterminismFences; }
	const VansTimelineDependencyClosure& Dependencies() const { return m_Dependencies; }

private:
	friend class VansTimelineCompiler;
	VansTimelineAsset m_Source;
	std::vector<VansCompiledTimelineTrack> m_PostScriptTracks;
	std::vector<VansCompiledTimelineTrack> m_CameraTracks;
	std::vector<VansTimelineTick> m_DeterminismFences;
	VansTimelineDependencyClosure m_Dependencies;
};

struct VansTimelineCompileOptions
{
	VansTimelineValidationContext validation;
	VansTimelineDependencyAssetLoader dependencyLoader;
};

struct VansTimelineCompileResult
{
	std::shared_ptr<const VansCompiledTimeline> timeline;
	VansTimelineDiagnostics diagnostics;

	explicit operator bool() const { return timeline != nullptr; }
};

class VansTimelineCompiler
{
public:
	static VansTimelineCompileResult Compile(
		const VansTimelineAsset& source,
		const VansTimelineCompileOptions& options = {});
};
}
