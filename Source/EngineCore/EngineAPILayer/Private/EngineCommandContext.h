#pragma once

namespace Vans::EditorAPI
{
	using RuntimeSceneHandle = void*;
	using RuntimeRenderDeviceHandle = void*;

	class EngineCommandContext
	{
	public:
		EngineCommandContext(RuntimeSceneHandle scene, RuntimeRenderDeviceHandle device)
			: m_Scene(scene)
			, m_Device(device)
		{
		}

		RuntimeSceneHandle GetScene() const { return m_Scene; }
		RuntimeRenderDeviceHandle GetDevice() const { return m_Device; }

	private:
		RuntimeSceneHandle m_Scene = nullptr;
		RuntimeRenderDeviceHandle m_Device = nullptr;
	};
}
