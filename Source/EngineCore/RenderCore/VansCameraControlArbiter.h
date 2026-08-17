#pragma once

#include "../CameraCore/VansCameraCore.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace VansGraphics
{
class VansCamera;
struct VansCameraControlDomainTag;
using VansCameraControlDomainId = Vans::VansStableId<VansCameraControlDomainTag>;
struct VansCameraControlPose
{
	glm::vec3 position{ 0.0f };
	glm::vec3 rotationDegrees{ 0.0f };
	float fieldOfView = 45.0f;
	float nearClip = 0.1f;
	float farClip = 10000.0f;
};
enum class VansCameraControlMode : std::uint8_t { Exclusive, Weighted, Additive };
enum class VansCameraControlSpace : std::uint8_t { World, CameraLocal };
struct VansCameraControlOwner
{
	VansCameraControlDomainId domain;
	Vans::VansGenerationHandle handle;
	bool IsValid() const { return domain.IsValid() && handle.IsValid(); }
};
struct VansCameraControlContribution
{
	VansCameraControlOwner owner;
	VansCameraControlPose pose;
	VansCameraControlMode mode = VansCameraControlMode::Exclusive;
	VansCameraControlSpace space = VansCameraControlSpace::World;
	std::int32_t priority = 0;
	std::uint64_t sequence = 0;
	float weight = 1.0f;
	std::uint32_t channels = 0x1Fu;
	bool suppressUserLook = false;
};
class VansCameraControlArbiter
{
public:
	static constexpr std::int32_t ScriptBasePriority = 0;
	static constexpr std::int32_t TimelinePriority = 1000;
	static VansCameraControlDomainId TimelineDomain();
	void BeginFrame(VansCamera& camera);
	void CaptureBase(VansCamera& camera);
	void CaptureBase(VansCameraControlPose pose);
	bool Submit(VansCameraControlContribution contribution);
	void Release(VansCameraControlOwner owner);
	void ReleaseDomain(VansCameraControlDomainId domain);
	VansCameraControlPose ResolvePose() const;
	void Resolve(VansCamera& camera);
	void Clear(VansCamera* camera = nullptr);
	bool IsUserLookSuppressed() const { return m_UserLookSuppressed; }
	std::size_t ContributionCount() const { return m_Runtime.ContributionCount(); }
	Vans::VansCameraRuntime& CoreRuntime() { return m_Runtime; }
	const Vans::VansCameraRuntime& CoreRuntime() const { return m_Runtime; }
private:
	VansCameraControlPose m_BasePose;
	Vans::VansCameraRuntime m_Runtime;
	std::vector<VansCameraControlOwner> m_TransientOwners;
	bool m_AppliedControl = false;
	bool m_UserLookSuppressed = false;
};
}
