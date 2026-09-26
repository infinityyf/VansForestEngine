#pragma once

#include "../CameraCore/VansCameraCore.h"

#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
class VansCamera;
class VansCameraControlArbiter
{
public:
	static constexpr std::int32_t TimelinePriority = 1000;
	static Vans::VansCameraContributionDomainId TimelineDomain();
	void BeginFrame(VansCamera& camera);
	void CaptureBase(VansCamera& camera);
	bool Submit(Vans::VansCameraContributionRequest contribution, std::string& error);
	void Release(Vans::VansCameraContributionOwner owner);
	void ReleaseDomain(Vans::VansCameraContributionDomainId domain);
	bool GetLastResolvedView(Vans::VansCameraViewSnapshot& outView) const;
	void Resolve(VansCamera& camera);
	void Clear(VansCamera* camera = nullptr);
	bool IsBaseCameraWriteWindowOpen() const;
	bool IsUserLookSuppressed() const { return m_UserLookSuppressed; }
	Vans::VansCameraRuntime& Runtime() { return m_Runtime; }
	const Vans::VansCameraRuntime& Runtime() const { return m_Runtime; }
private:
	enum class FrameStage : std::uint8_t
	{
		BetweenFrames,
		AwaitingBaseCapture,
		BaseCaptured
	};

	Vans::VansCameraViewSnapshot m_BaseView;
	Vans::VansCameraViewSnapshot m_LastResolvedView;
	Vans::VansCameraRuntime m_Runtime;
	std::vector<Vans::VansCameraContributionOwner> m_TransientOwners;
	bool m_AppliedControl = false;
	bool m_HasLastResolvedView = false;
	bool m_UserLookSuppressed = false;
	FrameStage m_FrameStage = FrameStage::BetweenFrames;
};
}
