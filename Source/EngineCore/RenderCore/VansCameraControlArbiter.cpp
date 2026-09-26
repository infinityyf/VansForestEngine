#include "VansCameraControlArbiter.h"

#include "VansCamera.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace VansGraphics
{
Vans::VansCameraContributionDomainId VansCameraControlArbiter::TimelineDomain()
{
	return Vans::VansMakeStableId<Vans::VansCameraContributionDomainIdTag>(
		"CameraControl.Timeline");
}

void VansCameraControlArbiter::BeginFrame(VansCamera& camera)
{
	assert(m_FrameStage != FrameStage::BaseCaptured);
	if (m_AppliedControl) camera.ApplyView(m_BaseView);
	m_AppliedControl = false;
	for (Vans::VansCameraContributionOwner owner : m_TransientOwners)
		m_Runtime.ReleaseOwner(owner);
	m_TransientOwners.clear();
	m_FrameStage = FrameStage::AwaitingBaseCapture;
}

void VansCameraControlArbiter::CaptureBase(VansCamera& camera)
{
	assert(m_FrameStage == FrameStage::AwaitingBaseCapture);
	camera.SyncFromTransform();
	m_BaseView = camera.CaptureView();
	std::string error;
	if (!m_Runtime.SetBaseView(Vans::VansCameraRuntime::MainView(), m_BaseView, error))
		VANS_LOG_ERROR("[Camera] Failed to capture base view: " << error);
	m_FrameStage = FrameStage::BaseCaptured;
}

bool VansCameraControlArbiter::Submit(
	Vans::VansCameraContributionRequest contribution,
	std::string& error)
{
	error.clear();
	if (!contribution.owner.IsValid())
	{
		error = "Camera contribution owner is invalid";
		return false;
	}
	if (contribution.owner.domain != TimelineDomain() &&
		contribution.order.priority >= TimelinePriority)
	{
		error = "Non-Timeline camera contribution entered the reserved Timeline priority range";
		return false;
	}
	const Vans::VansCameraContributionOwner owner = contribution.owner;
	if (!m_Runtime.UpsertContribution(std::move(contribution), error)) return false;
	m_UserLookSuppressed = m_Runtime.IsUserLookSuppressed();
	const auto found = std::find(m_TransientOwners.begin(), m_TransientOwners.end(), owner);
	if (found == m_TransientOwners.end()) m_TransientOwners.push_back(owner);
	return true;
}

void VansCameraControlArbiter::Release(Vans::VansCameraContributionOwner owner)
{
	m_Runtime.ReleaseOwner(owner);
	m_UserLookSuppressed = m_Runtime.IsUserLookSuppressed();
	m_TransientOwners.erase(std::remove(
		m_TransientOwners.begin(), m_TransientOwners.end(), owner), m_TransientOwners.end());
}

void VansCameraControlArbiter::ReleaseDomain(
	Vans::VansCameraContributionDomainId domain)
{
	m_Runtime.ReleaseDomain(domain);
	m_UserLookSuppressed = m_Runtime.IsUserLookSuppressed();
	m_TransientOwners.erase(std::remove_if(m_TransientOwners.begin(), m_TransientOwners.end(),
		[domain](const Vans::VansCameraContributionOwner& owner)
		{ return owner.domain == domain; }),
		m_TransientOwners.end());
}

bool VansCameraControlArbiter::GetLastResolvedView(
	Vans::VansCameraViewSnapshot& outView) const
{
	if (!m_HasLastResolvedView) return false;
	outView = m_LastResolvedView;
	return true;
}

void VansCameraControlArbiter::Resolve(VansCamera& camera)
{
	assert(m_FrameStage == FrameStage::BaseCaptured);
	m_UserLookSuppressed = m_Runtime.IsUserLookSuppressed();
	const bool hadContributions = m_Runtime.ContributionCount() != 0;
	m_LastResolvedView = m_Runtime.ResolveAndConsumeView(
		Vans::VansCameraRuntime::MainView()).snapshot;
	m_HasLastResolvedView = true;
	if (hadContributions)
	{
		camera.ApplyView(m_LastResolvedView);
		m_AppliedControl = true;
	}
	m_FrameStage = FrameStage::BetweenFrames;
}

void VansCameraControlArbiter::Clear(VansCamera* camera)
{
	if (camera && m_AppliedControl) camera->ApplyView(m_BaseView);
	m_Runtime.Clear();
	m_TransientOwners.clear();
	m_AppliedControl = false;
	m_HasLastResolvedView = false;
	m_UserLookSuppressed = false;
	m_FrameStage = FrameStage::BetweenFrames;
}

bool VansCameraControlArbiter::IsBaseCameraWriteWindowOpen() const
{
	return m_FrameStage != FrameStage::BaseCaptured;
}
}
