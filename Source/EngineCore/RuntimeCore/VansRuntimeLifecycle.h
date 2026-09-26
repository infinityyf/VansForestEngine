#pragma once

namespace Vans
{
enum class VansRuntimeLifecycleState
{
	Stopped,
	Starting,
	ProjectReady,
	GraphicsReady,
	SceneReady,
	Running,
	Quiesced,
	Failed,
	Stopping,
};
} // namespace Vans
