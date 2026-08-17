#include "VansActionTask.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
VansActionTaskHandle VansActionTaskSet::Create(VansActionTaskDesc desc, std::string& error)
{
	if (!m_AcceptingTasks || !desc.type || desc.debugName.empty() ||
		!std::isfinite(desc.timeoutSeconds) || desc.timeoutSeconds < 0.0)
	{
		error = "Action Task descriptor is invalid or parent is ending";
		return {};
	}
	if (m_MaximumTasks == 0 || m_Tasks.ActiveCount() >= m_MaximumTasks)
	{
		error = "Action Task budget exceeded";
		return {};
	}
	return { m_Tasks.Emplace(Task{ std::move(desc), VansActionTaskState::Waiting, 0.0 }) };
}

bool VansActionTaskSet::Complete(VansActionTaskHandle handle, std::string& error)
{
	return End(handle, VansActionTaskState::Completed, error);
}

bool VansActionTaskSet::Fail(VansActionTaskHandle handle, std::string& error)
{
	return End(handle, VansActionTaskState::Failed, error);
}

bool VansActionTaskSet::Cancel(VansActionTaskHandle handle, std::string& error)
{
	return End(handle, VansActionTaskState::Cancelled, error);
}

void VansActionTaskSet::CancelAll()
{
	m_AcceptingTasks = false;
	std::vector<VansActionTaskHandle> handles;
	m_Tasks.ForEach([&](VansGenerationHandle handle, const Task&) { handles.push_back({ handle }); });
	for (VansActionTaskHandle handle : handles)
	{
		std::string ignored;
		Cancel(handle, ignored);
	}
}

void VansActionTaskSet::Tick(double deltaSeconds)
{
	if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) return;
	std::vector<VansActionTaskHandle> timedOut;
	m_Tasks.ForEach([&](VansGenerationHandle handle, Task& task)
	{
		if (task.state != VansActionTaskState::Waiting) return;
		task.elapsedSeconds += deltaSeconds;
		if (task.desc.timeoutSeconds > 0.0 && task.elapsedSeconds >= task.desc.timeoutSeconds)
			timedOut.push_back({ handle });
	});
	for (VansActionTaskHandle handle : timedOut)
	{
		std::string ignored;
		End(handle, VansActionTaskState::TimedOut, ignored);
	}
}

VansActionTaskState VansActionTaskSet::State(VansActionTaskHandle handle) const
{
	const Task* task = m_Tasks.Resolve(handle.value);
	return task ? task->state : VansActionTaskState::Failed;
}

std::vector<VansActionTaskSnapshot> VansActionTaskSet::Snapshot() const
{
	std::vector<VansActionTaskSnapshot> result;
	m_Tasks.ForEach([&](VansGenerationHandle handle, const Task& task)
	{
		result.push_back({ { handle }, task.desc.type, task.desc.debugName, task.state,
			task.elapsedSeconds, task.desc.timeoutSeconds });
	});
	std::sort(result.begin(), result.end(), [](const auto& left, const auto& right)
	{
		return left.handle.value.index < right.handle.value.index;
	});
	return result;
}

bool VansActionTaskSet::End(
	VansActionTaskHandle handle,
	VansActionTaskState state,
	std::string& error)
{
	Task* task = m_Tasks.Resolve(handle.value);
	if (!task || task->state != VansActionTaskState::Waiting)
	{
		error = "Action Task handle is stale or already terminal";
		return false;
	}
	if (state == VansActionTaskState::Cancelled || state == VansActionTaskState::TimedOut)
		if (task->desc.cancel) task->desc.cancel();
	task->state = state;
	if (task->desc.terminal) task->desc.terminal(state);
	return m_Tasks.Release(handle.value);
}
}
