#pragma once
#include "VansBaseWindowComponent.h"
#include <string>
#include <vector>
namespace VansGraphics
{
	class VansRenderDebugWindow : public VansBaseWindowComponent
	{
	public:

	private:
		void ShowWindow(VansVKDevice& device) override;
	};
}
