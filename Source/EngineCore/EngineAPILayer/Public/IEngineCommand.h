#pragma once

#include <string>

namespace Vans::EditorAPI
{
	class EngineCommandContext;

	class IEngineCommand
	{
	public:
		virtual ~IEngineCommand() = default;

		virtual void Execute(EngineCommandContext& context) = 0;
		virtual void Undo(EngineCommandContext& context) = 0;
		virtual std::string GetDescription() const = 0;
		virtual bool CanMergeWith(const IEngineCommand&) const { return false; }
		virtual bool MergeWith(const IEngineCommand&, EngineCommandContext&) { return false; }
	};
}
