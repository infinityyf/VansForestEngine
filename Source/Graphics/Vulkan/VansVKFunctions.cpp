#include "VansVKFunctions.h"
#include <iostream>

#if defined _WIN32
#define LoadFunction GetProcAddress
#elif defined __linux
#define LoadFunction dlsym
#endif

namespace VansVulkan
{
	//funtion points definition
	#define EXPORTED_VULKAN_FUNCTION( name ) PFN_##name name;
	#define GLOBAL_LEVEL_VULKAN_FUNCTION( name ) PFN_##name name;
	#define INSTANCE_LEVEL_VULKAN_FUNCTION( name ) PFN_##name name;
	#define INSTANCE_LEVEL_VULKAN_FUNCTION_FROM_EXTENSION( name, extension) PFN_##name name;
	#define DEVICE_LEVEL_VULKAN_FUNCTION( name ) PFN_##name name;
	#define DEVICE_LEVEL_VULKAN_FUNCTION_FROM_EXTENSION( name, extension) PFN_##name name;
	#include "ListOfVulkanFunctions.inl"

#if defined _WIN32
	HMODULE vulkan_library;
#elif defined __linux
	void* vulkan_library;
#endif

	bool LoadVulkanLibrary()
	{
#if defined _WIN32
		vulkan_library = LoadLibrary(TEXT("vulkan-1.dll"));
#elif defined __linux
		vulkan_library = dlopen("libvulkan.so.1", RTLD_NOW);
#endif
		if (vulkan_library == nullptr)
		{
			std::cout << "Could not connect with a Vulkan Runtime library." << std::endl;
			return false;
		}
		return true;
	}

	bool UnloadVulkanLibrary()
	{
#if defined _WIN32
		FreeLibrary(vulkan_library);
#elif defined __linux
		dlclose(vulkan_library);
#endif
		vulkan_library = nullptr;
		return true;
	}

	bool LoadVulkanExportedFunction()
	{
#define EXPORTED_VULKAN_FUNCTION( name ) name = (PFN_##name)LoadFunction( vulkan_library, #name );\
		if( name == nullptr )\
		{\
			std::cout << "Could not load exported Vulkan function: " << #name << std::endl;\
			return false;\
		}
		//call the function £º EXPORTED_VULKAN_FUNCTION(vkGetInstanceProcAddr), not declare function
#include "ListOfVulkanFunctions.inl"
		return true;
	}

	bool LoadVulkanGlobalLevelFunctions()
	{
#define GLOBAL_LEVEL_VULKAN_FUNCTION( name ) name = (PFN_##name)vkGetInstanceProcAddr( nullptr, #name );\
		if( name == nullptr )\
		{\
			std::cout << "Could not load global level Vulkan function: " << #name << std::endl;\
			return false;\
		}
#include "ListOfVulkanFunctions.inl"
		return true;
	}

	bool LoadVulkanInstanceLevelFunctions(VkInstance instance)
	{
#define INSTANCE_LEVEL_VULKAN_FUNCTION( name ) name = (PFN_##name)vkGetInstanceProcAddr( instance, #name ); \
		if( name == nullptr ) \
		{ \
			std::cout << "Could not load instance-level Vulkan function named: " << #name << std::endl; \
			return false; \
		}
#include "ListOfVulkanFunctions.inl"
		return true;
	}

	bool LoadVulkanInstanceLevelFunctionFromExtension(VkInstance instance, std::vector<char const*>& enabled_extensions)
	{
#define INSTANCE_LEVEL_VULKAN_FUNCTION_FROM_EXTENSION( name, extension ) \
		for (auto& enabled_extension : enabled_extensions) \
		{\
			if (std::string(enabled_extension) == std::string(extension))\
			{\
				name = (PFN_##name)vkGetInstanceProcAddr(instance, #name); \
				if (name == nullptr) \
				{ \
					std::cout << "Could not load instance-level Vulkan function named: "<< #name << std::endl; \
					return false; \
				} \
			} \
		}
#include "ListOfVulkanFunctions.inl"
		return true;
	}

	bool LoadVulkanDeviceLevelFunctions(VkDevice device)
	{
#define DEVICE_LEVEL_VULKAN_FUNCTION( name ) name = (PFN_##name)vkGetDeviceProcAddr( device, #name ); \
		if( name == nullptr )\
		{ \
			std::cout << "Could not load device-level Vulkan function named: " #name << std::endl; \
			return false; \
		}
#include "ListOfVulkanFunctions.inl"
		return true;
	}

	bool LoadVulkanDeviceLevelFunctionFromExtension(VkDevice device, std::vector<char const*>& enabled_extensions)
	{
#define DEVICE_LEVEL_VULKAN_FUNCTION_FROM_EXTENSION( name, extension) \
		for (auto& enabled_extension : enabled_extensions) \
		{\
			if (std::string(enabled_extension) == std::string(extension)) \
			{\
				name = (PFN_##name)vkGetDeviceProcAddr(device, #name); \
				if (name == nullptr) \
				{\
					std::cout << "Could not load device-level Vulkan function named: " #name << std::endl; \
					return false; \
				} \
			} \
		}
#include "ListOfVulkanFunctions.inl"
		return true;
	}
} 

