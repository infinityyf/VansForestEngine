#include "VansCameraControlArbiter.h"

#include "VansCamera.h"

#include <algorithm>

namespace VansGraphics
{
namespace
{
Vans::VansCameraContributionDomainId CoreDomain(VansCameraControlDomainId domain)
{
	return { domain.value };
}

Vans::VansCameraContributionOwner CoreOwner(VansCameraControlOwner owner)
{
	return { CoreDomain(owner.domain), owner.handle };
}

Vans::VansCameraViewSnapshot CorePose(const VansCameraControlPose& pose)
{
	Vans::VansCameraViewSnapshot result;
	result.pose.position = pose.position;
	result.pose.rotationDegrees = pose.rotationDegrees;
	result.lens.fieldOfView = pose.fieldOfView;
	result.lens.nearClip = pose.nearClip;
	result.lens.farClip = pose.farClip;
	return result;
}

VansCameraControlPose GraphicsPose(const Vans::VansCameraViewSnapshot& snapshot)
{
	VansCameraControlPose result;
	result.position = snapshot.pose.position;
	result.rotationDegrees = snapshot.pose.rotationDegrees;
	result.fieldOfView = snapshot.lens.fieldOfView;
	result.nearClip = snapshot.lens.nearClip;
	result.farClip = snapshot.lens.farClip;
	return result;
}
}

VansCameraControlDomainId VansCameraControlArbiter::TimelineDomain()
{
	return Vans::VansMakeStableId<VansCameraControlDomainTag>("CameraControl.Timeline");
}

void VansCameraControlArbiter::BeginFrame(VansCamera& camera)
{
	if (m_AppliedControl) camera.ApplyControlPose(m_BasePose);
	m_AppliedControl = false;
	for (VansCameraControlOwner owner : m_TransientOwners)
		m_Runtime.ReleaseOwner(CoreOwner(owner));
	m_TransientOwners.clear();
}
void VansCameraControlArbiter::CaptureBase(VansCamera& camera)
{
	camera.SyncFromTransform();
	CaptureBase(camera.CaptureControlPose());
}
void VansCameraControlArbiter::CaptureBase(VansCameraControlPose pose)
{
	m_BasePose = pose;
	std::string ignored;
	m_Runtime.SetBaseView(Vans::VansCameraRuntime::MainView(), CorePose(pose), ignored);
}
bool VansCameraControlArbiter::Submit(VansCameraControlContribution contribution)
{
	if (!contribution.owner.IsValid()) return false;
	if (contribution.owner.domain != TimelineDomain() &&
		contribution.priority >= TimelinePriority) return false;
	contribution.weight = std::clamp(contribution.weight, 0.0f, 1.0f);
	Vans::VansCameraContribution core;
	core.view = Vans::VansCameraRuntime::MainView();
	core.owner = CoreOwner(contribution.owner);
	core.kind = contribution.mode == VansCameraControlMode::Additive
		? Vans::VansCameraContributionKind::PoseOffset
		: Vans::VansCameraContributionKind::Shot;
	core.value = CorePose(contribution.pose);
	core.blendMode = contribution.mode == VansCameraControlMode::Exclusive
		? Vans::VansCameraBlendMode::Exclusive
		: contribution.mode == VansCameraControlMode::Weighted
			? Vans::VansCameraBlendMode::Weighted
			: Vans::VansCameraBlendMode::Additive;
	core.space = contribution.space == VansCameraControlSpace::World
		? Vans::VansCameraSpace::World : Vans::VansCameraSpace::CameraLocal;
	core.order.priority = contribution.priority;
	core.order.stableSequence = contribution.sequence;
	core.weight = contribution.weight;
	core.channels = contribution.channels;
	core.suppressUserLook = contribution.suppressUserLook;
	std::string error;
	if (!m_Runtime.UpsertContribution(std::move(core), error)) return false;
	if (contribution.suppressUserLook) m_UserLookSuppressed = true;
	const auto found = std::find_if(m_TransientOwners.begin(), m_TransientOwners.end(),
		[&](const VansCameraControlOwner& owner)
		{
			return owner.domain == contribution.owner.domain &&
				owner.handle == contribution.owner.handle;
		});
	if (found == m_TransientOwners.end()) m_TransientOwners.push_back(contribution.owner);
	return true;
}
void VansCameraControlArbiter::Release(VansCameraControlOwner owner)
{
	m_Runtime.ReleaseOwner(CoreOwner(owner));
	m_UserLookSuppressed = m_Runtime.IsUserLookSuppressed();
	m_TransientOwners.erase(std::remove_if(m_TransientOwners.begin(), m_TransientOwners.end(),
		[&](const VansCameraControlOwner& candidate)
		{
			return candidate.domain == owner.domain && candidate.handle == owner.handle;
		}), m_TransientOwners.end());
}
void VansCameraControlArbiter::ReleaseDomain(VansCameraControlDomainId domain)
{
	m_Runtime.ReleaseDomain(CoreDomain(domain));
	m_UserLookSuppressed = m_Runtime.IsUserLookSuppressed();
	m_TransientOwners.erase(std::remove_if(m_TransientOwners.begin(), m_TransientOwners.end(),
		[domain](const VansCameraControlOwner& owner) { return owner.domain == domain; }),
		m_TransientOwners.end());
}

VansCameraControlPose VansCameraControlArbiter::ResolvePose() const
{
	return GraphicsPose(m_Runtime.ResolveView(Vans::VansCameraRuntime::MainView()).snapshot);
}

void VansCameraControlArbiter::Resolve(VansCamera& camera)
{
	m_UserLookSuppressed = m_Runtime.IsUserLookSuppressed();
	if (m_Runtime.ContributionCount() == 0) return;
	camera.ApplyControlPose(GraphicsPose(m_Runtime.ResolveAndConsumeView(
		Vans::VansCameraRuntime::MainView()).snapshot));
	m_AppliedControl = true;
}
void VansCameraControlArbiter::Clear(VansCamera* camera)
{
	if (camera && m_AppliedControl) camera->ApplyControlPose(m_BasePose);
	m_Runtime.ClearContributions();
	m_TransientOwners.clear();
	m_AppliedControl = false;
	m_UserLookSuppressed = false;
}
}
