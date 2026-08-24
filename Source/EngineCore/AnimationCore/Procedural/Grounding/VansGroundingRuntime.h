#pragma once

#include "VansGroundingTypes.h"
#include "../VansPoseWorkspace.h"

#include <unordered_map>

namespace VansGraphics
{
	class VansGroundingRuntime
	{
	public:
		bool Configure(const VansCompiledAnimationRig& rig,
		               const VansCompiledGroundingSettings& settings,
		               std::string& error);
		void Reset(std::uint64_t resetToken = 0);

		bool Prepare(VansPoseWorkspace& workspace,
		             const VansAnimationExternalInputSnapshot& input,
		             std::vector<VansWorldQueryRequest>& outRequests);
		bool Resolve(float deltaTime,
		             VansPoseWorkspace& workspace,
		             const VansAnimationExternalInputSnapshot& input,
		             const std::vector<VansWorldQueryResult>& results,
		             std::vector<VansProceduralGoal>& outGoals,
		             VansProceduralSolverResult& outResult);
		void CommitResolvedState();
		void RollbackResolvedState();
		void ReportLimbSolve(int chainIndex, const VansProceduralSolverResult& result);

	private:
		enum class PlantState { Unplanted, Candidate, Planted, Replant };

		struct ContactState
		{
			PlantState plantState = PlantState::Unplanted;
			VansSupportHandle support;
			glm::vec3 lockedPositionWorld{ 0.0f };
			glm::quat lockedRotationWorld{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::vec3 lockedSupportPosition{ 0.0f };
			glm::quat lockedSupportRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::vec3 groundNormalWorld{ 0.0f, 1.0f, 0.0f };
			float weight = 0.0f;
			bool solveRejected = false;
			bool hasGroundNormal = false;
		};

		struct PreparedContact
		{
			int rigContactIndex = -1;
			glm::vec3 animatedFootModel{ 0.0f };
			glm::quat animatedFootRotationModel{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::vec3 animatedFootScaleModel{ 1.0f };
			std::vector<glm::vec3> samplePositionsWorld;
			std::vector<std::uint64_t> requestIds;
		};

		const VansContactAttribute* FindContactAttribute(
			const VansAnimationExternalInputSnapshot& input,
			const std::string& provider,
			const std::string& contactId) const;
		bool ApplyPelvis(float deltaTime,
		                 VansPoseWorkspace& workspace,
		                 std::vector<VansProceduralGoal>& goals,
		                 const glm::vec3& modelUp,
		                 bool& outReachRejected);

		const VansCompiledAnimationRig* m_Rig = nullptr;
		VansCompiledGroundingSettings m_Settings;
		std::vector<ContactState> m_ContactStates;
		std::vector<ContactState> m_TransactionContactStates;
		std::vector<PreparedContact> m_PreparedContacts;
		std::vector<const VansWorldQueryResult*> m_AcceptedHitsScratch;
		std::vector<const VansWorldQueryResult*> m_SupportHitsScratch;
		VansBoneTransform m_OwnerTransform;
		std::uint64_t m_ResetToken = 0;
		std::uint64_t m_QuerySequence = 1;
		glm::vec3 m_PelvisOffsetModel{ 0.0f };
		glm::vec3 m_TransactionPelvisOffsetModel{ 0.0f };
		bool m_HasResolvedTransaction = false;
		bool m_HasPreparedContacts = false;
	};
}
