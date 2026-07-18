#pragma once

#include "../VansScene.h"

namespace VansGraphics
{
	class VansVKDevice;

	class VansSceneRenderPreparationExecutor
	{
	public:
		static void PrepareAfterSceneContentLoaded(VansScene& scene, VansVKDevice& device);

	private:
		static void BindVideoComponentsToPreparedMaterials(VansScene& scene);
	};
}
