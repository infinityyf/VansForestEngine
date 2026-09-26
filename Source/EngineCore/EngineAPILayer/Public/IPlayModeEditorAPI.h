#pragma once

#include "EngineDTOs.h"

namespace Vans::EditorAPI
{
	class IPlayModeEditorAPI
	{
	public:
		virtual ~IPlayModeEditorAPI() = default;
		// 只管理游戏视口内的鼠标显隐，不捕获整个 Editor 窗口。
		virtual void UpdateGameCursorViewport(bool interactive) = 0;
		virtual bool IsGameCursorHidden() const = 0;
		virtual EnginePlayState GetPlayState() const = 0;
		virtual void SetPlayState(EnginePlayState state) = 0;
	};
}
