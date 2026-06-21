#pragma once
#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
	class VansProjectWindow : public VansBaseWindowComponent
	{
	public:

	private:
		void ShowWindow(VansVKDevice& device) override;
	};
}
