#pragma once

#include <memory>
#include <string>

namespace Vans
{
	struct VansOpenAssetDocument;

	struct VansAnimationRigSaveResult
	{
		bool success = false;
		bool published = false;
		std::string message;

		explicit operator bool() const { return success; }
	};

	// Publishes only the Animation Rig asset. Scene attachment state is
	// intentionally outside this service because preview bindings are transient.
	class VansAnimationRigSaveService
	{
	public:
		static VansAnimationRigSaveResult Save(
			const std::shared_ptr<VansOpenAssetDocument>& rigDocument);
	};
}
