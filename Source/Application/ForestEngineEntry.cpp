#include "VansEditorRuntimeHost.h"

#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include "../EngineCore/Util/VansLog.h"

#include <cstdlib>

int main()
{
	VANS_INIT_MAIN_THREAD();
	VANS_LOG("=== ForestEngine Starting ===");

	VansEngine::VansEditorRuntimeHost runtime;
	if (!runtime.Start())
		return -1;

	runtime.Run();
	runtime.Stop();

	VANS_LOG("=== ForestEngine Exited ===");
	// Shader cooking completes explicit shutdown before process-global
	// third-party objects begin their destructor pass.
	if (std::getenv("FORESTENGINE_COOK_SHADERS_AND_EXIT") != nullptr)
		std::_Exit(0);
	return 0;
}
