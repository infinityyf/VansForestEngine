#pragma once

#include "../AuthoringCore/VansAuthoringHistory.h"

#include <functional>
#include <vector>

namespace Vans
{
enum class VansEditorHistorySource
{
	Runtime,
	Scene,
	Asset,
	Terrain,
	PcgSpline
};

struct VansEditorHistoryResult
{
	bool available = false;
	bool success = false;
	VansEditorHistorySource source = VansEditorHistorySource::Runtime;
};

// 将独立领域栈按共享序号组合为编辑器主菜单的一条时间轴；
// 命令及其具体副作用仍由各领域 owner 执行。
class VansEditorHistoryService
{
public:
	using Operation = std::function<bool()>;

	void AddSource(
		VansEditorHistorySource source,
		VansAuthoringHistorySnapshot snapshot,
		Operation undo,
		Operation redo);
	bool CanUndo() const;
	bool CanRedo() const;
	VansEditorHistoryResult Undo() const;
	VansEditorHistoryResult Redo() const;

private:
	struct SourceEntry
	{
		VansEditorHistorySource source = VansEditorHistorySource::Runtime;
		VansAuthoringHistorySnapshot snapshot;
		Operation undo;
		Operation redo;
	};

	const SourceEntry* LatestUndo() const;
	const SourceEntry* EarliestRedo() const;
	static int TiePriority(VansEditorHistorySource source);
	std::vector<SourceEntry> m_Sources;
};
}
