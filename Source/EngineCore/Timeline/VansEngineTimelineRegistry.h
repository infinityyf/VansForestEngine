#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace VansGraphics
{
class VansCamera;
class VansCameraControlArbiter;
class VansScene;
class VansVirtualCameraParameterStore;
}

namespace Vans
{
class VansGameplayRuntime;
class VansRuntimeWorld;
class VansTimelineApplierRegistry;
class VansTimelineClockRegistry;
class VansTimelinePropertyAccessRegistry;
class VansTimelineTrackExtensionRegistry;

struct VansEngineTimelineCatalog
{
	const VansTimelineTrackExtensionRegistry* trackExtensions = nullptr;
	const VansTimelinePropertyAccessRegistry* propertyAccess = nullptr;
	const VansTimelineClockRegistry* clocks = nullptr;
	std::string_view error;

	explicit operator bool() const
	{
		return trackExtensions != nullptr && propertyAccess != nullptr && clocks != nullptr;
	}
};

struct VansEngineTimelineContext
{
	VansGraphics::VansScene& scene;
	VansRuntimeWorld& world;
	VansGameplayRuntime& gameplay;
	VansGraphics::VansCamera& camera;
	VansGraphics::VansCameraControlArbiter& cameraControl;
	VansGraphics::VansVirtualCameraParameterStore& virtualCameraParameters;
};

struct VansEngineTimelineRegistries
{
	std::shared_ptr<VansTimelineApplierRegistry> appliers;
	std::uint64_t manifestHash = 0;
	explicit operator bool() const { return appliers != nullptr && manifestHash != 0; }
};

bool VansValidateEngineTimelineRegistries(
	const VansTimelineTrackExtensionRegistry& extensions,
	const VansTimelineApplierRegistry& appliers,
	std::string& error);

VansEngineTimelineCatalog VansGetEngineTimelineCatalog();

VansEngineTimelineRegistries VansBuildEngineTimelineRegistries(
	const VansEngineTimelineContext& context,
	std::string& error);
}
