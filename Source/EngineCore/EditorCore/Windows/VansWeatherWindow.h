#pragma once

#include "VansBaseWindowComponent.h"

#include <string>

namespace VansGraphics
{
class VansWeatherWindow final : public VansBaseWindowComponent
{
private:
	void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;

	std::string m_Status;
	bool m_StatusIsError = false;
};
}
