#pragma once
#include "../ScriptCore/VansCommonUtils.h"

namespace VansGraphics
{
	enum GRAPHICS_API
	{
		INVALIDE = 0,
		VULKAN = 1,
	};


	class VansGraphicsDevice
	{
	public:
		GRAPHICS_API m_GraphicsAPI;

	protected:

		uint32_t m_RenderWidth;
		uint32_t m_RenderHeight;

	public :
		//��ʼ������Ⱦ������
		virtual void BeforeRendering() = 0;

		virtual void Rendering() = 0;

		virtual void Present() = 0;

		//�ͷű���Ⱦ����
		virtual void AfterRendering() = 0;

		//��ȡ�ײ�deviceָ��
		virtual void* GetNativeGraphicsDevice() = 0;

		virtual void* GetNativeCommandBuffer() = 0;

		float GetAspectRatio();

		float GetNativeRenderWidth() { return m_RenderWidth; }

		float GetNativeRenderHeight() { return m_RenderHeight; }
	};

	class VansGUIBackEnd
	{
	public:
		virtual void InitBackEnd(VansGraphicsDevice& device, GLFWwindow* window) = 0;
	};

}

extern VansGraphics::VansGraphicsDevice* m_GraphicsDevice;
extern VansGraphics::VansGUIBackEnd* m_GUIBackEnd;