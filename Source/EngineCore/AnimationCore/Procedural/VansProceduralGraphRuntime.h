#pragma once

#include "VansAnimationRig.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VansGraphics
{
	class VansAnimGraph;

	struct VansProceduralParameterAccessor
	{
		const void* context = nullptr;
		bool (*readFloat)(const void*, const std::string&, float&) = nullptr;
		bool (*readVector3)(const void*, const std::string&, glm::vec3&) = nullptr;
		bool (*readQuaternion)(const void*, const std::string&, glm::quat&) = nullptr;
	};

	using VansGroundQueryProfileResolver = std::function<bool(
		const std::string& profile, std::uint32_t& collisionMask, std::string& error)>;

	enum class VansProceduralDebugKind { Goal, Aim, Grounding, LimbIK, ChainIK };

	struct VansProceduralDebugRecord
	{
		int nodeId = -1;
		VansProceduralDebugKind kind = VansProceduralDebugKind::Goal;
		int chainIndex = -1;
		int goalIndex = -1;
		VansProceduralGoal goal;
		VansProceduralSolverResult result;
	};

	// Compiled runtime for the only legal procedural execution boundary: the
	// target-skeleton post-process graph. It owns all mutable solver/plant state.
	class VansProceduralGraphRuntime
	{
	public:
		VansProceduralGraphRuntime();
		~VansProceduralGraphRuntime();
		VansProceduralGraphRuntime(VansProceduralGraphRuntime&&) noexcept;
		VansProceduralGraphRuntime& operator=(VansProceduralGraphRuntime&&) noexcept;
		VansProceduralGraphRuntime(const VansProceduralGraphRuntime&) = delete;
		VansProceduralGraphRuntime& operator=(const VansProceduralGraphRuntime&) = delete;

		bool Configure(const VansAnimGraph& graph,
		               const VansCompiledAnimationRig& rig,
		               const VansGroundQueryProfileResolver& queryProfileResolver,
		               std::string& error);
		void Reset(std::uint64_t resetToken = 0);

		bool Prepare(float deltaTime,
		             const std::vector<VansBoneTransform>& localPose,
		             const std::vector<int>& activeNodeIds,
		             const VansProceduralParameterAccessor& parameters,
		             const VansAnimationExternalInputSnapshot& input,
		             std::vector<VansWorldQueryRequest>& outRequests,
		             std::vector<VansBoneTransform>& outCompletedPose,
		             bool& outNeedsResolve,
		             std::string& error);
		bool Resolve(const std::vector<VansWorldQueryResult>& results,
		             std::vector<VansBoneTransform>& outCompletedPose,
		             std::string& error);

		bool IsConfigured() const;
		bool HasPreparedQueries() const;
		const VansCompiledAnimationRig* GetRig() const;
		const std::vector<VansProceduralDebugRecord>& GetDebugRecords() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
