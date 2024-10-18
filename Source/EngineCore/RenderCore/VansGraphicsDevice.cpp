#include "VansGraphicsDevice.h"

float VansGraphics::VansGraphicsDevice::GetAspectRatio()
{
	return (float)m_RenderWidth / (float)m_RenderHeight;
}

VansGraphics::VansGraphicsDevice* m_GraphicsDevice = nullptr;
VansGraphics::VansGUIBackEnd* m_GUIBackEnd = nullptr;

