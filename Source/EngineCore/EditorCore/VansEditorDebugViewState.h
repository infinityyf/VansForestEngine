#pragma once

namespace VansGraphics
{
	struct VansEditorDebugViewState final
	{
		bool wireframeMode = false;
		bool vehicleDebugGizmos = false;
		bool hiZCullDebugVisualization = false;
		bool skeletonDebugGizmos = false;
		bool skeletonDebugSelectedOnly = true;
		bool skeletonDebugShowNames = false;
		bool skeletonDebugShowRetargetSource = true;
	};
}
