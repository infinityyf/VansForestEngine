#pragma once

#include <string>

namespace Vans::EditorAPI
{
	class EngineCommandContext;

	class IVansEditorRuntimeCommand
	{
	public:
		virtual ~IVansEditorRuntimeCommand() = default;

		virtual void Execute(EngineCommandContext& context) = 0;
		virtual void Undo(EngineCommandContext& context) = 0;
		virtual std::string GetDescription() const = 0;
		virtual bool CanMergeWith(const IVansEditorRuntimeCommand&) const { return false; }
		virtual bool MergeWith(const IVansEditorRuntimeCommand&, EngineCommandContext&) { return false; }
	};
}
