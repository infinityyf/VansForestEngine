#pragma once
#include "VansCommonUtils.h"

namespace VansGraphics
{
	enum GRAPHICS_API
	{
		INVALIDE = 0,
		VULKAN = 1,
	};

	struct alignas(16) CameraDataStruct
	{
		glm::mat4x4 ModelMatrix;
		glm::vec4   CameraPosition;
		glm::vec4	CameraDirection;
		glm::mat4x4 ViewMatrix;
		glm::mat4x4 ProjectionMatrix;
	};


	class VansGraphicsDevice
	{
	public:
		GRAPHICS_API m_GraphicsAPI;

	protected:

		CameraDataStruct m_CameraData;

		uint32_t m_RenderWidth;
		uint32_t m_RenderHeight;

	public :
		//初始化被渲染的数据
		virtual void BeforeRendering() = 0;

		virtual void Rendering() = 0;

		virtual void Present() = 0;

		//释放被渲染数据
		virtual void AfterRendering() = 0;

		//获取底层device指针
		virtual void* GetNativeGraphicsDevice() = 0;

		virtual void* GetNativeCommandBuffer() = 0;

		void SetCameraData(const glm::mat4& view_matrix, const glm::mat4& projective_matrix);

		float GetAspectRatio();
	};

	class VansGUIBackEnd
	{
	public:
		virtual void InitBackEnd(VansGraphicsDevice& device, GLFWwindow* window) = 0;
	};

}

extern VansGraphics::VansGraphicsDevice* m_GraphicsDevice;
extern VansGraphics::VansGUIBackEnd* m_GUIBackEnd;