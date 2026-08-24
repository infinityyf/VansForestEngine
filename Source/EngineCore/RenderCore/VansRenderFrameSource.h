#pragma once

#include "VansRenderFrame.h"

#include <optional>

namespace VansGraphics
{
	struct VansRenderFramePreparationContext final
	{
		VansRenderFrameId frameId;
		VansLogicFrameId logicFrameId;
		VansSurfaceEpoch surfaceEpoch;
		VansRenderViewSnapshot view;
		VansRenderFrameTimingSnapshot timing;
	};

	struct VansRenderFrameSourceOutput final
	{
		VansRenderMutationBatch mutationsBeforeFrame;
		VansRenderSceneFrameSnapshot scene;
	};

	// Main-thread producer boundary. Implementations resolve gameplay-derived
	// render state and return owned value data, but must not record commands,
	// submit queues, present, or retain references from the preparation context.
	class IVansRenderFrameSource
	{
	public:
		virtual ~IVansRenderFrameSource() = default;
		virtual std::optional<VansRenderFrameSourceOutput> PrepareMainThreadRenderFrame(
			const VansRenderFramePreparationContext& context) = 0;
	};
}
