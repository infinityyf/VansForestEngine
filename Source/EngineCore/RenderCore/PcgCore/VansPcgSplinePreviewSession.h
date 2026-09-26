#pragma once

#include "../../AuthoringCore/Pcg/VansPcgSplineAuthoringSession.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Vans
{
class VansAssetObjectRepository;
struct VansPcgSplineFieldSnapshot;
}

namespace VansGraphics
{
class VansScene;

class VansPcgSplinePreviewSession
{
public:
	VansPcgSplinePreviewSession();
	~VansPcgSplinePreviewSession();
	VansPcgSplinePreviewSession(const VansPcgSplinePreviewSession&) = delete;
	VansPcgSplinePreviewSession& operator=(const VansPcgSplinePreviewSession&) = delete;

	void Reset();
	bool QueueBuild(
		Vans::VansPcgSplineBuildRequest request,
		std::shared_ptr<const Vans::VansPcgSplineFieldSnapshot> previous);
	void Tick(
		VansScene& scene,
		const std::string& recipeGuid,
		const Vans::VansAssetObjectRepository& repository,
		std::uint64_t currentRequest,
		bool dragging,
		std::string& message);
	bool IsBuilding() const;

private:
	struct State;
	std::unique_ptr<State> m_State;
};
}
