#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace Vans
{
	class VansSceneDocument;
	class VansSceneEditService;
	struct VansSerializedValue;

	enum class ReparentTransformPolicy
	{
		KeepWorld,
		KeepLocal
	};

	struct SceneReparentRequest
	{
		std::string childEntityGuid;
		std::string newParentEntityGuid;
		std::optional<std::size_t> siblingIndex;
		ReparentTransformPolicy transformPolicy = ReparentTransformPolicy::KeepWorld;
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

