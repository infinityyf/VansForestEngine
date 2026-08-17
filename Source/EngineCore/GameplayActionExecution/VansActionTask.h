#pragma once

#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"
#include "../RuntimeCore/VansGenerationPool.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Vans
{
enum class VansActionTaskState : std::uint8_t
{
	Waiting,
	Completed,
	Cancelled,
	Failed,
	TimedOut
};

struct VansActionTaskDesc
{
	VansActionGraphNodeTypeId type;
	std::string debugName;
	double timeoutSeconds = 0.0;
	std::function<void()> cancel;
	std::function<void(VansActionTaskState)> terminal;
};

struct VansActionTaskSnapshot
{
	VansActionTaskHandle handle;
	VansActionGraphNodeTypeId type;
	std::string debugName;
	VansActionTaskState state = VansActionTaskState::Waiting;
	double elapsedSeconds = 0.0;
	double timeoutSeconds = 0.0;
};

class VansActionTaskSet
{
public:
	VansActionTaskSet() = default;
	explicit VansActionTaskSet(std::size_t maximumTasks)
		: m_MaximumTasks(maximumTasks) {}
	void SetMaximumTasks(std::size_t maximumTasks) { m_MaximumTasks = maximumTasks; }
	VansActionTaskHandle Create(VansActionTaskDesc desc, std::string& error);
	bool Complete(VansActionTaskHandle handle, std::string& error);
	bool Fail(VansActionTaskHandle handle, std::string& error);
	bool Cancel(VansActionTaskHandle handle, std::string& error);
	void CancelAll();
	void Tick(double deltaSeconds);
	VansActionTaskState State(VansActionTaskHandle handle) const;
	std::vector<VansActionTaskSnapshot> Snapshot() const;
	std::size_t ActiveCount() const { return m_Tasks.ActiveCount(); }
	bool AcceptingTasks() const { return m_AcceptingTasks; }
	void StopAcceptingTasks() { m_AcceptingTasks = false; }

private:
	struct Task
	{
		VansActionTaskDesc desc;
		VansActionTaskState state = VansActionTaskState::Waiting;
		double elapsedSeconds = 0.0;
	};

	bool End(VansActionTaskHandle handle, VansActionTaskState state, std::string& error);

	VansGenerationPool<Task> m_Tasks;
	std::size_t m_MaximumTasks = 64;
	bool m_AcceptingTasks = true;
};
}
