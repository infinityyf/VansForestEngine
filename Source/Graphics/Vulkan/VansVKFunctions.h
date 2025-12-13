#ifndef VK_FUNCTIONS
#define VK_FUNCTIONS

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#elif defined __linux

#endif

#include "vulkan/vulkan.h"
#include <vector>

namespace VansGraphics
{
	#define EXPORTED_VULKAN_FUNCTION( name ) extern PFN_##name name;
	#define GLOBAL_LEVEL_VULKAN_FUNCTION( name ) extern PFN_##name name;
	#define INSTANCE_LEVEL_VULKAN_FUNCTION( name ) extern PFN_##name name;
	#define INSTANCE_LEVEL_VULKAN_FUNCTION_FROM_EXTENSION( name, extension) extern PFN_##name name;
	#define DEVICE_LEVEL_VULKAN_FUNCTION( name ) extern PFN_##name name;
	#define DEVICE_LEVEL_VULKAN_FUNCTION_FROM_EXTENSION( name, extension) extern PFN_##name name;
	#include "ListOfVulkanFunctions.inl"
}

namespace VansGraphics
{
#if defined _WIN32
	extern HMODULE vulkan_library;
#elif defined __linux
	extern void* vulkan_library;
#endif

	bool LoadVulkanLibrary();

	bool UnloadVulkanLibrary();

	bool LoadVulkanExportedFunction();

	bool LoadVulkanGlobalLevelFunctions();

	bool LoadVulkanInstanceLevelFunctions(VkInstance instance);

	bool LoadVulkanInstanceLevelFunctionFromExtension(VkInstance instance, std::vector<char const*>& enabled_extensions);

	bool LoadVulkanDeviceLevelFunctions(VkDevice device);

	bool LoadVulkanDeviceLevelFunctionFromExtension(VkDevice device, std::vector<char const*>& enabled_extensions);
}

#endif // !VK_FUNCTIONS

