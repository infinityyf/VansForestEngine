#pragma once

#include <cstdint>

namespace Vans
{
using VansHistorySequence = std::uint64_t;

struct VansAuthoringHistorySnapshot
{
	VansHistorySequence undoSequence = 0;
	VansHistorySequence redoSequence = 0;

	bool CanUndo() const { return undoSequence != 0; }
	bool CanRedo() const { return redoSequence != 0; }
};

// 为彼此独立的作者态命令栈提供同一条进程内时间轴。
// 只有成功的新编辑会推进 revision；Undo/Redo 保留原命令序号。
class VansAuthoringHistory
{
public:
	static VansHistorySequence IssueEditSequence();
	static VansHistorySequence CurrentRevision();
};
}
