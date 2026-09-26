#include "VansEditorHistoryService.h"

#include <utility>

namespace Vans
{
void VansEditorHistoryService::AddSource(
	VansEditorHistorySource source,
	VansAuthoringHistorySnapshot snapshot,
	Operation undo,
	Operation redo)
{
	m_Sources.push_back({ source, snapshot, std::move(undo), std::move(redo) });
}

bool VansEditorHistoryService::CanUndo() const
{
	return LatestUndo() != nullptr;
}

bool VansEditorHistoryService::CanRedo() const
{
	return EarliestRedo() != nullptr;
}

VansEditorHistoryResult VansEditorHistoryService::Undo() const
{
	const SourceEntry* entry = LatestUndo();
	if (!entry)
		return {};
	return { true, entry->undo && entry->undo(), entry->source };
}

VansEditorHistoryResult VansEditorHistoryService::Redo() const
{
	const SourceEntry* entry = EarliestRedo();
	if (!entry)
		return {};
	return { true, entry->redo && entry->redo(), entry->source };
}

const VansEditorHistoryService::SourceEntry* VansEditorHistoryService::LatestUndo() const
{
	const SourceEntry* selected = nullptr;
	for (const SourceEntry& entry : m_Sources)
	{
		if (!entry.snapshot.CanUndo() || !entry.undo)
			continue;
		if (!selected || entry.snapshot.undoSequence > selected->snapshot.undoSequence ||
			(entry.snapshot.undoSequence == selected->snapshot.undoSequence &&
				TiePriority(entry.source) > TiePriority(selected->source)))
		{
			selected = &entry;
		}
	}
	return selected;
}

const VansEditorHistoryService::SourceEntry* VansEditorHistoryService::EarliestRedo() const
{
	const SourceEntry* selected = nullptr;
	for (const SourceEntry& entry : m_Sources)
	{
		if (!entry.snapshot.CanRedo() || !entry.redo)
			continue;
		if (!selected || entry.snapshot.redoSequence < selected->snapshot.redoSequence ||
			(entry.snapshot.redoSequence == selected->snapshot.redoSequence &&
				TiePriority(entry.source) > TiePriority(selected->source)))
		{
			selected = &entry;
		}
	}
	return selected;
}

int VansEditorHistoryService::TiePriority(VansEditorHistorySource source)
{
	switch (source)
	{
	case VansEditorHistorySource::PcgSpline: return 5;
	case VansEditorHistorySource::Terrain: return 4;
	case VansEditorHistorySource::Asset: return 3;
	case VansEditorHistorySource::Scene: return 2;
	case VansEditorHistorySource::Runtime: return 1;
	}
	return 0;
}
}
