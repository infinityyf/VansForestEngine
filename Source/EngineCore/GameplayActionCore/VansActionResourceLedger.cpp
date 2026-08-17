#include "VansActionResourceLedger.h"

namespace Vans
{
VansActionResourceHandle VansActionResourceLedger::Register(
	VansActionResourceEntry entry,
	std::string& error)
{
	if (m_Released)
	{
		error = "Action ResourceLedger already released";
		return {};
	}
	if (entry.type.empty() || !entry.release)
	{
		error = "Action resource needs a type and release callback";
		return {};
	}
	if (entry.dependsOn && !m_Entries.Contains(entry.dependsOn.value))
	{
		error = "Action resource dependency is stale or not registered earlier";
		return {};
	}
	if (entry.prediction == VansActionPredictionResourcePolicy::UndoOnly && !entry.undo)
	{
		error = "predicted Action resource needs Undo";
		return {};
	}
	if (entry.prediction == VansActionPredictionResourcePolicy::UndoRedo && (!entry.undo || !entry.redo))
	{
		error = "predicted Action resource needs Undo and Redo";
		return {};
	}
	const VansActionResourceHandle handle{ m_Entries.Emplace(State{ std::move(entry), false }) };
	m_Order.push_back(handle);
	return handle;
}

bool VansActionResourceLedger::Release(VansActionResourceHandle handle, std::string& error)
{
	State* state = m_Entries.Resolve(handle.value);
	if (!state)
	{
		error = "Action resource handle is stale";
		return false;
	}
	if (!state->entry.release())
	{
		error = "failed to release Action resource: " + state->entry.debugName;
		return false;
	}
	return m_Entries.Release(handle.value);
}

bool VansActionResourceLedger::ForgetExternalResource(
	VansActionServiceId service,
	VansGenerationHandle resource)
{
	if (!service || !resource) return false;
	for (auto it = m_Order.rbegin(); it != m_Order.rend(); ++it)
	{
		State* state = m_Entries.Resolve(it->value);
		if (!state || state->entry.service != service ||
			state->entry.externalResource != resource) continue;
		return m_Entries.Release(it->value);
	}
	return false;
}

bool VansActionResourceLedger::ReleaseAll(std::vector<std::string>& errors)
{
	if (m_Released) return true;
	bool succeeded = true;
	for (auto it = m_Order.rbegin(); it != m_Order.rend(); ++it)
	{
		State* state = m_Entries.Resolve(it->value);
		if (!state) continue;
		if (state->undone &&
			state->entry.prediction == VansActionPredictionResourcePolicy::UndoOnly)
		{
			m_Entries.Release(it->value);
			continue;
		}
		std::string error;
		if (!Release(*it, error))
		{
			succeeded = false;
			errors.push_back(std::move(error));
		}
	}
	m_Order.clear();
	m_Released = true;
	return succeeded;
}

bool VansActionResourceLedger::RollbackPredicted(std::vector<std::string>& errors)
{
	bool succeeded = true;
	for (auto it = m_Order.rbegin(); it != m_Order.rend(); ++it)
	{
		State* state = m_Entries.Resolve(it->value);
		if (!state || state->undone ||
			state->entry.prediction == VansActionPredictionResourcePolicy::NotPredictable) continue;
		if (!state->entry.undo || !state->entry.undo())
		{
			errors.push_back("failed to Undo predicted Action resource: " + state->entry.debugName);
			succeeded = false;
			continue;
		}
		state->undone = true;
	}
	return succeeded;
}

bool VansActionResourceLedger::ReplayPredicted(std::vector<std::string>& errors)
{
	bool succeeded = true;
	for (VansActionResourceHandle handle : m_Order)
	{
		State* state = m_Entries.Resolve(handle.value);
		if (!state || !state->undone) continue;
		if (state->entry.prediction != VansActionPredictionResourcePolicy::UndoRedo ||
			!state->entry.redo || !state->entry.redo())
		{
			errors.push_back("failed to Redo predicted Action resource: " + state->entry.debugName);
			succeeded = false;
			continue;
		}
		state->undone = false;
	}
	return succeeded;
}

std::vector<VansActionResourceSnapshot> VansActionResourceLedger::Snapshot() const
{
	std::vector<VansActionResourceSnapshot> result;
	result.reserve(m_Entries.ActiveCount());
	m_Entries.ForEach([&](VansGenerationHandle handle, const State& state)
	{
		result.push_back({ { handle }, state.entry.type, state.entry.debugName,
			state.entry.dependsOn, state.entry.prediction, state.undone });
	});
	return result;
}

bool VansActionCommitTransaction::AddStep(VansActionCommitStep step, std::string& error)
{
	if (m_Committed || step.name.empty() || !step.preflight || !step.apply || !step.compensate)
	{
		error = "Action Commit step is invalid or transaction already committed";
		return false;
	}
	m_Steps.push_back(std::move(step));
	return true;
}

bool VansActionCommitTransaction::Commit(std::string& error)
{
	if (m_Committed)
	{
		error = "Action Commit transaction cannot be committed twice";
		return false;
	}
	for (VansActionCommitStep& step : m_Steps)
	{
		if (!step.preflight(error))
		{
			error = "Action Commit preflight failed at " + step.name + ": " + error;
			return false;
		}
	}
	for (std::size_t index = 0; index < m_Steps.size(); ++index)
	{
		if (m_Steps[index].apply(error))
		{
			m_AppliedCount = index + 1;
			continue;
		}
		error = "Action Commit apply failed at " + m_Steps[index].name + ": " + error;
		for (std::size_t rollback = m_AppliedCount; rollback > 0; --rollback)
		{
			std::string compensationError;
			if (!m_Steps[rollback - 1].compensate(compensationError))
			{
				m_CompensationFailed = true;
				error += "; compensation failed at " + m_Steps[rollback - 1].name +
					": " + compensationError;
			}
		}
		m_AppliedCount = 0;
		return false;
	}
	m_Committed = true;
	return true;
}
}
