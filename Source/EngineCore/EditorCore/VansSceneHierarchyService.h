#pragma once

#include <cstddef>
#include "VansSceneEditService.h"

#include <optional>
#include <string>

namespace Vans
{
	class VansSceneDocument;
	class VansSceneEditService;
	struct VansSerializedValue;

	struct SceneReparentRequest
	{
		std::string childEntityGuid;
		std::optional<VansSceneParentReference> newParent;
		std::optional<std::size_t> siblingIndex;
		ReparentTransformPolicy transformPolicy = ReparentTransformPolicy::KeepWorld;
		std::optional<EditorAPI::RuntimeTransformSnapshot> resolvedLocalTransform;
	};

	struct SceneHierarchyEditResult
	{
		bool success = false;
		bool changed = false;
		std::string message;

		explicit operator bool() const { return success; }
	};

	class VansSceneHierarchyService
	{
	public:
		static SceneHierarchyEditResult Reparent(
			const VansSceneDocument& document,
			VansSceneEditService& editService,
			const SceneReparentRequest& request);
	};
}
