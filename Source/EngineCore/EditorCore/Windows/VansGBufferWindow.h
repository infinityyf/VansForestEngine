#pragma once
#include "VansBaseWindowComponent.h"
#include <string>
#include <vector>
namespace VansGraphics
{
	class VansGBufferWindow : public VansBaseWindowComponent
	{
	public:

	private:
		void ShowWindow(VansVKDevice& device) override;
	};
}