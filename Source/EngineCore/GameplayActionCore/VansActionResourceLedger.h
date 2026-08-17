#pragma once

#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"
#include "../RuntimeCore/VansGenerationPool.h"

#include <functional>
#include <string>
#include <vector>

namespace Vans
{
enum class VansActionPredictionResourcePolicy : std::uint8_t
{
	NotPredictable,
	UndoOnly,
	UndoRedo
};

struct VansActionResourceEntry
{
	std::string type;
	std::string debugName;
	VansActionServiceId service;
	VansGenerationHandle externalResource;
	VansActionResourceHandle dependsOn;
	VansActionPredictionResourcePolicy prediction = VansActionPredictionResourcePolicy::NotPredictable;
	std::function<bool()> release;
	std::function<bool()> undo;
	std::function<bool()> redo;
};

struct VansActionResourceSnapshot
{
	VansActionResourceHandle handle;
	std::string type;
	std::string debugName;
	VansActionResourceHandle dependsOn;
	VansActionPredictionResourcePolicy prediction = VansActionPredictionResourcePolicy::NotPredictable;
	bool undone = false;
};

class VansActionResourceLedger
{
public:
	VansActionResourceHandle Register(VansActionResourceEntry entry, std::string& error);
	bool Release(VansActionResourceHandle handle, std::string& error);
	bool ForgetExternalResource(VansActionServiceId service, VansGenerationHandle resource);
	bool ReleaseAll(std::vector<std::string>& errors);
	bool RollbackPredicted(std::vector<std::string>& errors);
	bool ReplayPredicted(std::vector<std::string>& errors);
	std::vector<VansActionResourceSnapshot> Snapshot() const;
	std::size_t ActiveCount() const { return m_Entries.ActiveCount(); }
	bool IsReleased() const { return m_Released; }

private:
	struct State
	{
		VansActionResourceEntry entry;
		bool undone = false;
	};

	VansGenerationPool<State> m_Entries;
	std::vector<VansActionResourceHandle> m_Order;
	bool m_Released = false;
};

struct VansActionCommitStep
{
	std::string name;
	std::function<bool(std::string&)> preflight;
	std::function<bool(std::string&)> apply;
	std::function<bool(std::string&)> compensate;
};

class VansActionCommitTransaction
{
public:
	bool AddStep(VansActionCommitStep step, std::string& error);
	bool Commit(std::string& error);
	bool CompensationFailed() const { return m_CompensationFailed; }
	bool IsCommitted() const { return m_Committed; }
	std::size_t AppliedStepCount() const { return m_AppliedCount; }

private:
	std::vector<VansActionCommitStep> m_Steps;
	std::size_t m_AppliedCount = 0;
	bool m_Committed = false;
	bool m_CompensationFailed = false;
};
}
