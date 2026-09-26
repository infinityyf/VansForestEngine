#include "VansCameraCore.h"

#include "../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace Vans
{
namespace
{
bool Finite(const glm::vec3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float ShortestAngleDelta(float fromDegrees, float toDegrees)
{
	float delta = std::fmod(toDegrees - fromDegrees, 360.0f);
	if (delta > 180.0f) delta -= 360.0f;
	if (delta < -180.0f) delta += 360.0f;
	return delta;
}

glm::vec3 BlendRotation(const glm::vec3& from, const glm::vec3& to, float weight)
{
	return { from.x + ShortestAngleDelta(from.x, to.x) * weight,
		from.y + ShortestAngleDelta(from.y, to.y) * weight,
		from.z + ShortestAngleDelta(from.z, to.z) * weight };
}

float DampingAlpha(float dampingSeconds, double deltaSeconds, bool initialized)
{
	if (!initialized || dampingSeconds <= 0.0f || deltaSeconds <= 0.0) return 1.0f;
	return 1.0f - std::exp(-static_cast<float>(deltaSeconds) / dampingSeconds);
}

glm::vec3 LookRotationDegrees(const glm::vec3& from, const glm::vec3& to,
	const glm::vec3& fallback)
{
	const glm::vec3 direction = to - from;
	const float length = glm::length(direction);
	if (length <= 0.0001f) return fallback;
	const glm::vec3 normalized = direction / length;
	return { glm::degrees(std::asin(std::clamp(normalized.y, -1.0f, 1.0f))),
		glm::degrees(std::atan2(normalized.z, normalized.x)), 0.0f };
}

}

bool VansValidateCameraLensLimits(
	const VansCameraLensLimits& limits,
	std::string& error)
{
	if (!std::isfinite(limits.minimumFieldOfView) ||
		!std::isfinite(limits.maximumFieldOfView) ||
		!std::isfinite(limits.minimumNearClip) ||
		!std::isfinite(limits.minimumClipSeparation) ||
		limits.minimumFieldOfView <= 0.0f ||
		limits.maximumFieldOfView >= 180.0f ||
		limits.maximumFieldOfView <= limits.minimumFieldOfView ||
		limits.minimumNearClip <= 0.0f ||
		limits.minimumClipSeparation <= 0.0f)
	{
		error = "Camera lens limits require finite FOV bounds within (0, 180), "
			"an increasing FOV range, and positive near/separation limits";
		return false;
	}
	error.clear();
	return true;
}

bool VansValidateCameraView(
	const VansCameraViewSnapshot& view,
	const VansCameraLensLimits& limits,
	std::string& error)
{
	if (!VansValidateCameraLensLimits(limits, error)) return false;
	if (!Finite(view.pose.position) || !Finite(view.pose.rotationDegrees) ||
		!std::isfinite(view.lens.fieldOfView) || !std::isfinite(view.lens.nearClip) ||
		!std::isfinite(view.lens.farClip))
	{
		error = "Camera view contains a non-finite value";
		return false;
	}
	if (view.lens.fieldOfView < limits.minimumFieldOfView ||
		view.lens.fieldOfView > limits.maximumFieldOfView ||
		view.lens.nearClip < limits.minimumNearClip ||
		view.lens.farClip < view.lens.nearClip + limits.minimumClipSeparation)
	{
		error = "Camera view is outside the configured lens limits";
		return false;
	}
	error.clear();
	return true;
}

bool VansClampCameraView(
	VansCameraViewSnapshot& view,
	const VansCameraLensLimits& limits,
	std::string& diagnostic)
{
	diagnostic.clear();
	std::string error;
	if (!VansValidateCameraLensLimits(limits, error))
	{
		diagnostic = std::move(error);
		return false;
	}
	if (!Finite(view.pose.position) || !Finite(view.pose.rotationDegrees) ||
		!std::isfinite(view.lens.fieldOfView) || !std::isfinite(view.lens.nearClip) ||
		!std::isfinite(view.lens.farClip))
	{
		diagnostic = "Camera view contains a non-finite value";
		return false;
	}

	const VansCameraLens original = view.lens;
	view.lens.fieldOfView = std::clamp(
		view.lens.fieldOfView,
		limits.minimumFieldOfView,
		limits.maximumFieldOfView);
	view.lens.nearClip = std::max(view.lens.nearClip, limits.minimumNearClip);
	view.lens.farClip = std::max(
		view.lens.farClip,
		view.lens.nearClip + limits.minimumClipSeparation);
	if (view.lens.fieldOfView != original.fieldOfView ||
		view.lens.nearClip != original.nearClip ||
		view.lens.farClip != original.farClip)
	{
		diagnostic = "Camera lens was clamped to the configured FOV, near-clip, and clip-separation limits";
	}
	return true;
}

VansCameraViewId VansCameraRuntime::MainView()
{
	return VansMakeStableId<VansCameraViewIdTag>("Camera.View.Main");
}

std::size_t VansCameraRuntime::OwnerHash::operator()(
	const VansCameraContributionOwner& owner) const noexcept
{
	std::uint64_t hash = owner.domain.value;
	hash ^= (static_cast<std::uint64_t>(owner.writer.generation) << 32u) |
		owner.writer.index;
	hash ^= hash >> 33u;
	hash *= 0xff51afd7ed558ccdull;
	hash ^= hash >> 33u;
	return static_cast<std::size_t>(hash);
}

bool VansCameraRuntime::SetLensLimits(
	VansCameraLensLimits limits,
	std::string& error)
{
	if (!VansValidateCameraLensLimits(limits, error)) return false;
	m_LensLimits = limits;
	m_ReportedLensClamps.clear();
	error.clear();
	return true;
}

bool VansCameraRuntime::ClampView(
	VansCameraViewSnapshot& view,
	std::string_view source,
	std::string& error) const
{
	std::string diagnostic;
	if (!VansClampCameraView(view, m_LensLimits, diagnostic))
	{
		error = std::move(diagnostic);
		return false;
	}
	if (!diagnostic.empty())
		VANS_LOG_WARN("[CameraCore] " << source << ": " << diagnostic);
	error.clear();
	return true;
}

bool VansCameraRuntime::ValidateContribution(
	VansCameraContributionRequest& request,
	std::string& error) const
{
	if (!request.view) request.view = MainView();
	if (!request.owner.IsValid() || request.channels == 0 ||
		!std::isfinite(request.weight) || !std::isfinite(request.shakeScale) ||
		(request.hasShakeOrigin && !Finite(request.shakeOrigin)))
	{
		error = "Camera contribution identity, channels, weight or shake input is invalid";
		return false;
	}
	request.weight = std::clamp(request.weight, 0.0f, 1.0f);
	return ClampView(request.value, "contribution", error);
}

VansCameraRigHandle VansCameraRuntime::RegisterRig(
	VansCameraRigDefinition definition,
	std::string& error)
{
	if (!definition.id || definition.stableName.empty() ||
		definition.id != VansMakeStableId<VansCameraRigIdTag>(definition.stableName) ||
		m_RigIds.find(definition.id) != m_RigIds.end() ||
		!ClampView(definition.initialView, "rig initial view", error) ||
		!std::isfinite(definition.follow.positionDamping) ||
		!std::isfinite(definition.lookAt.rotationDamping) ||
		!std::isfinite(definition.collision.radius) ||
		!std::isfinite(definition.collision.minimumDistance) ||
		!std::isfinite(definition.collision.padding) ||
		!std::isfinite(definition.collision.recoverySeconds) ||
		!std::isfinite(definition.focusDistance) ||
		!std::isfinite(definition.composition.screenX) ||
		!std::isfinite(definition.composition.screenY) ||
		!std::isfinite(definition.composition.deadZoneX) ||
		!std::isfinite(definition.composition.deadZoneY) ||
		definition.follow.positionDamping < 0.0f ||
		definition.lookAt.rotationDamping < 0.0f ||
		definition.collision.radius < 0.0f ||
		definition.collision.minimumDistance < 0.0f ||
		definition.collision.padding < 0.0f ||
		definition.collision.recoverySeconds < 0.0f ||
		definition.focusDistance < 0.0f ||
		definition.composition.screenX < 0.0f || definition.composition.screenX > 1.0f ||
		definition.composition.screenY < 0.0f || definition.composition.screenY > 1.0f ||
		definition.composition.deadZoneX < 0.0f || definition.composition.deadZoneX > 1.0f ||
		definition.composition.deadZoneY < 0.0f || definition.composition.deadZoneY > 1.0f)
	{
		if (error.empty()) error = "Camera rig definition is invalid or duplicated";
		return {};
	}
	const VansCameraRigId id = definition.id;
	const VansCameraRigHandle handle{ m_Rigs.Emplace(std::move(definition)) };
	m_RigIds.emplace(id, handle);
	return handle;
}

bool VansCameraRuntime::UnregisterRig(VansCameraRigHandle rig)
{
	const VansCameraRigDefinition* definition = m_Rigs.Resolve(rig.value);
	if (!definition) return false;
	const VansCameraRigId id = definition->id;
	for (auto& [view, state] : m_Views)
	{
		(void)view;
		if (state.rig == rig)
		{
			state.rig = {};
			state.rigSolveInitialized = false;
			state.hasCollisionDistance = false;
		}
	}
	std::vector<VansCameraContributionHandle> active;
	m_Contributions.ForEach([&](VansGenerationHandle handle,
		const ContributionState& state)
	{
		if (state.request.rig == rig) active.push_back({ handle });
	});
	for (VansCameraContributionHandle handle : active) ReleaseContribution(handle);
	m_RigIds.erase(id);
	return m_Rigs.Release(rig.value);
}

const VansCameraRigDefinition* VansCameraRuntime::ResolveRig(VansCameraRigHandle rig) const
{
	return m_Rigs.Resolve(rig.value);
}

const VansCameraRigDefinition* VansCameraRuntime::ResolveRig(VansCameraRigId rig) const
{
	const auto found = m_RigIds.find(rig);
	return found == m_RigIds.end() ? nullptr : ResolveRig(found->second);
}

bool VansCameraRuntime::BindViewRig(
	VansCameraViewId view,
	VansCameraRigHandle rig,
	std::string& error,
	VansGenerationHandle bindingContext)
{
	if (!view || !m_Rigs.Contains(rig.value))
	{
		error = "Camera view or rig handle is invalid";
		return false;
	}
	ViewState& state = m_Views[view];
	state.rig = rig;
	state.bindingContext = bindingContext;
	state.rigSolveInitialized = false;
	state.hasCollisionDistance = false;
	return true;
}

bool VansCameraRuntime::SetBaseView(
	VansCameraViewId view,
	VansCameraViewSnapshot snapshot,
	std::string& error)
{
	if (!view || !ClampView(snapshot, "base view", error))
	{
		if (error.empty()) error = "Camera base view identity is invalid";
		return false;
	}
	ViewState& state = m_Views[view];
	state.base = std::move(snapshot);
	state.hasExplicitBase = true;
	state.rigSolveInitialized = false;
	state.hasCollisionDistance = false;
	return true;
}

VansCameraShakeHandle VansCameraRuntime::RegisterShake(
	VansCameraShakeDefinition definition,
	std::string& error)
{
	const float duration = definition.attackSeconds + definition.sustainSeconds +
		definition.releaseSeconds;
	if (!definition.id || definition.stableName.empty() ||
		definition.id != VansMakeStableId<VansCameraShakeIdTag>(definition.stableName) ||
		m_ShakeIds.find(definition.id) != m_ShakeIds.end() ||
		!Finite(definition.translationAmplitude) || !Finite(definition.rotationAmplitude) ||
		!std::isfinite(definition.frequency) || !std::isfinite(duration) ||
		!std::isfinite(definition.minimumDistance) ||
		!std::isfinite(definition.maximumDistance) ||
		!std::isfinite(definition.falloffExponent) || definition.frequency < 0.0f ||
		definition.attackSeconds < 0.0f || definition.sustainSeconds < 0.0f ||
		definition.releaseSeconds < 0.0f || duration <= 0.0f ||
		definition.minimumDistance < 0.0f ||
		definition.maximumDistance < definition.minimumDistance ||
		definition.falloffExponent <= 0.0f)
	{
		error = "Camera shake definition is invalid or duplicated";
		return {};
	}
	const VansCameraShakeId id = definition.id;
	const VansCameraShakeHandle handle{ m_Shakes.Emplace(std::move(definition)) };
	m_ShakeIds.emplace(id, handle);
	return handle;
}

bool VansCameraRuntime::UnregisterShake(VansCameraShakeHandle shake)
{
	const VansCameraShakeDefinition* definition = ResolveShake(shake);
	if (!definition) return false;
	const VansCameraShakeId id = definition->id;
	std::vector<VansCameraContributionHandle> active;
	m_Contributions.ForEach([&](VansGenerationHandle handle, const ContributionState& state)
	{
		if (state.request.shake == shake) active.push_back({ handle });
	});
	for (VansCameraContributionHandle handle : active) ReleaseContribution(handle);
	m_ShakeIds.erase(id);
	return m_Shakes.Release(shake.value);
}

const VansCameraShakeDefinition* VansCameraRuntime::ResolveShake(VansCameraShakeHandle shake) const
{
	return m_Shakes.Resolve(shake.value);
}

const VansCameraShakeDefinition* VansCameraRuntime::ResolveShake(VansCameraShakeId shake) const
{
	const auto found = m_ShakeIds.find(shake);
	return found == m_ShakeIds.end() ? nullptr : ResolveShake(found->second);
}

void VansCameraRuntime::Advance(double deltaSeconds)
{
	if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0) return;
	for (auto& [view, state] : m_Views)
	{
		(void)view;
		const VansCameraRigDefinition* rig = ResolveRig(state.rig);
		if (!rig) continue;
		const VansCameraViewSnapshot previous = state.rigSolveInitialized
			? state.solved : (state.hasExplicitBase ? state.base : rig->initialView);
		state.solved = SolveRig(*rig, state.bindingContext, previous,
			state.rigSolveInitialized, state.hasCollisionDistance,
			state.collisionDistance, deltaSeconds);
	}
	std::vector<VansCameraContributionHandle> expired;
	m_Contributions.ForEach([&](VansGenerationHandle handle, ContributionState& state)
	{
		const VansCameraContributionRequest& request = state.request;
		if (request.rig)
		{
			const VansCameraRigDefinition* rig = ResolveRig(request.rig);
			if (!rig)
			{
				expired.push_back({ handle });
				return;
			}
			state.value = SolveRig(*rig, request.bindingContext,
				state.value, state.rigSolveInitialized,
				state.hasCollisionDistance, state.collisionDistance,
				deltaSeconds);
		}
		if (request.kind != VansCameraContributionKind::Shake || !request.shake)
			return;
		const VansCameraShakeDefinition* shake = ResolveShake(request.shake);
		if (!shake)
		{
			expired.push_back({ handle });
			return;
		}
		state.shakeElapsedSeconds += deltaSeconds;
		const double attackEnd = shake->attackSeconds;
		const double sustainEnd = attackEnd + shake->sustainSeconds;
		const double end = sustainEnd + shake->releaseSeconds;
		if (state.shakeElapsedSeconds > end + 1e-12)
		{
			expired.push_back({ handle });
			return;
		}
		float envelope = 1.0f;
		if (shake->attackSeconds > 0.0f && state.shakeElapsedSeconds < attackEnd)
			envelope = static_cast<float>(state.shakeElapsedSeconds / shake->attackSeconds);
		else if (shake->releaseSeconds > 0.0f && state.shakeElapsedSeconds > sustainEnd)
			envelope = static_cast<float>((end - state.shakeElapsedSeconds) /
				shake->releaseSeconds);
		envelope = std::clamp(envelope, 0.0f, 1.0f);
		float distanceScale = 1.0f;
		if (request.hasShakeOrigin && shake->maximumDistance > shake->minimumDistance)
		{
			const float distance = glm::length(
				BaseFor(request.view).pose.position - request.shakeOrigin);
			const float normalized = std::clamp((distance - shake->minimumDistance) /
				(shake->maximumDistance - shake->minimumDistance), 0.0f, 1.0f);
			distanceScale = std::pow(1.0f - normalized, shake->falloffExponent);
		}
		const std::uint64_t seed = request.shakeSeed != 0
			? request.shakeSeed : shake->seed;
		const float seedPhase = static_cast<float>((seed % 104729ull) * 0.000060001f);
		const float phase = static_cast<float>(state.shakeElapsedSeconds) *
			shake->frequency * 6.28318530718f + seedPhase;
		const glm::vec3 noise{
			std::sin(phase), std::sin(phase * 1.371f + 1.7f),
			std::sin(phase * 1.917f + 3.1f) };
		const float scale = request.shakeScale * envelope * distanceScale;
		state.value.pose.position = shake->translationAmplitude * noise * scale;
		state.value.pose.rotationDegrees = shake->rotationAmplitude *
			glm::vec3(noise.z, noise.x, noise.y) * scale;
	});
	for (VansCameraContributionHandle handle : expired) ReleaseContribution(handle);
}

VansCameraContributionHandle VansCameraRuntime::AddContribution(
	VansCameraContributionRequest request,
	std::string& error)
{
	if (!ValidateContribution(request, error)) return {};
	if (request.kind == VansCameraContributionKind::Shake &&
		(!request.shake || !ResolveShake(request.shake)))
	{
		error = "Camera shake contribution references an invalid profile";
		return {};
	}
	if (request.rig && !ResolveRig(request.rig))
	{
		error = "Camera contribution references an invalid rig";
		return {};
	}
	if (m_Owners.find(request.owner) != m_Owners.end())
	{
		error = "Camera contribution owner is already registered";
		return {};
	}
	if (request.order.stableSequence == 0)
		request.order.stableSequence = m_NextSequence++;
	const VansCameraContributionOwner owner = request.owner;
	ContributionState state;
	state.value = request.value;
	state.request = std::move(request);
	const VansCameraContributionHandle handle{ m_Contributions.Emplace(std::move(state)) };
	m_Owners.emplace(owner, handle);
	return handle;
}

VansCameraContributionHandle VansCameraRuntime::UpsertContribution(
	VansCameraContributionRequest request,
	std::string& error)
{
	const auto found = m_Owners.find(request.owner);
	if (found == m_Owners.end()) return AddContribution(std::move(request), error);
	if (!UpdateContribution(found->second, std::move(request), error)) return {};
	return found->second;
}

bool VansCameraRuntime::UpdateContribution(
	VansCameraContributionHandle handle,
	VansCameraContributionRequest request,
	std::string& error)
{
	ContributionState* existing = m_Contributions.Resolve(handle.value);
	if (!existing)
	{
		error = "Camera contribution handle is stale";
		return false;
	}
	if (request.owner != existing->request.owner)
	{
		error = "Camera contribution owner cannot change during update";
		return false;
	}
	if (!ValidateContribution(request, error)) return false;
	if (request.kind == VansCameraContributionKind::Shake &&
		(!request.shake || !ResolveShake(request.shake)))
	{
		error = "Camera shake contribution references an invalid profile";
		return false;
	}
	if (request.rig && !ResolveRig(request.rig))
	{
		error = "Camera contribution references an invalid rig";
		return false;
	}
	if (request.order.stableSequence == 0)
		request.order.stableSequence = existing->request.order.stableSequence;
	const bool sameRig = request.rig && request.rig == existing->request.rig &&
		request.bindingContext == existing->request.bindingContext;
	const bool sameShake = request.kind == VansCameraContributionKind::Shake &&
		existing->request.kind == VansCameraContributionKind::Shake &&
		request.shake && request.shake == existing->request.shake;
	ContributionState updated;
	updated.request = std::move(request);
	updated.value = updated.request.value;
	if (sameRig)
	{
		updated.value = existing->value;
		updated.rigSolveInitialized = existing->rigSolveInitialized;
		updated.hasCollisionDistance = existing->hasCollisionDistance;
		updated.collisionDistance = existing->collisionDistance;
	}
	if (sameShake)
	{
		updated.value = existing->value;
		updated.shakeElapsedSeconds = existing->shakeElapsedSeconds;
	}
	*existing = std::move(updated);
	return true;
}

bool VansCameraRuntime::ReadContribution(
	VansCameraContributionHandle handle,
	VansCameraContributionRequest& outRequest) const
{
	const ContributionState* state = m_Contributions.Resolve(handle.value);
	if (!state) return false;
	outRequest = state->request;
	return true;
}

bool VansCameraRuntime::ReleaseContribution(VansCameraContributionHandle handle)
{
	const ContributionState* state = m_Contributions.Resolve(handle.value);
	if (!state) return false;
	m_Owners.erase(state->request.owner);
	return m_Contributions.Release(handle.value);
}

bool VansCameraRuntime::ReleaseOwner(VansCameraContributionOwner owner)
{
	const auto found = m_Owners.find(owner);
	return found != m_Owners.end() && ReleaseContribution(found->second);
}

std::size_t VansCameraRuntime::ReleaseDomain(VansCameraContributionDomainId domain)
{
	std::vector<VansCameraContributionHandle> handles;
	for (const auto& [owner, handle] : m_Owners)
		if (owner.domain == domain) handles.push_back(handle);
	for (VansCameraContributionHandle handle : handles) ReleaseContribution(handle);
	return handles.size();
}

void VansCameraRuntime::ClearContributions()
{
	m_Contributions.Clear();
	m_Owners.clear();
	m_NextSequence = 1;
}

void VansCameraRuntime::Clear()
{
	ClearContributions();
	m_Rigs.Clear();
	m_Shakes.Clear();
	m_RigIds.clear();
	m_ShakeIds.clear();
	m_Views.clear();
	m_ReportedLensClamps.clear();
}

VansCameraViewSnapshot VansCameraRuntime::BaseFor(VansCameraViewId view) const
{
	const auto found = m_Views.find(view);
	if (found == m_Views.end()) return {};
	if (found->second.rigSolveInitialized) return found->second.solved;
	if (found->second.hasExplicitBase) return found->second.base;
	if (const VansCameraRigDefinition* rig = ResolveRig(found->second.rig))
		return rig->initialView;
	return {};
}

VansCameraViewSnapshot VansCameraRuntime::SolveRig(
	const VansCameraRigDefinition& definition,
	VansGenerationHandle bindingContext,
	VansCameraViewSnapshot previous,
	bool& initialized,
	bool& hasCollisionDistance,
	float& collisionDistance,
	double deltaSeconds)
{
	VansCameraViewSnapshot solved = previous;
	VansCameraBindingSnapshot followTarget;
	const bool hasFollowTarget = definition.follow.enabled && m_BindingResolver &&
		m_BindingResolver(bindingContext, definition.follow.targetBinding, followTarget);
	glm::vec3 collisionOrigin = solved.pose.position;
	bool hasCollisionOrigin = false;
	if (hasFollowTarget)
	{
		glm::vec3 offset = definition.follow.localOffset;
		if (definition.follow.mode != VansCameraFollowMode::Fixed)
			offset = glm::quat(glm::radians(followTarget.pose.rotationDegrees)) * offset;
		const glm::vec3 desired = followTarget.pose.position + offset;
		const float alpha = DampingAlpha(
			definition.follow.positionDamping, deltaSeconds, initialized);
		solved.pose.position += (desired - solved.pose.position) * alpha;
		collisionOrigin = followTarget.pose.position;
		hasCollisionOrigin = true;
	}

	VansCameraBindingSnapshot lookTarget;
	const bool hasLookTarget = definition.lookAt.enabled && m_BindingResolver &&
		m_BindingResolver(bindingContext, definition.lookAt.targetBinding, lookTarget);
	if (hasLookTarget)
	{
		const glm::vec3 target = lookTarget.pose.position + definition.lookAt.worldOffset;
		const glm::vec3 desired = LookRotationDegrees(
			solved.pose.position, target, solved.pose.rotationDegrees);
		const float alpha = DampingAlpha(
			definition.lookAt.rotationDamping, deltaSeconds, initialized);
		solved.pose.rotationDegrees = BlendRotation(
			solved.pose.rotationDegrees, desired, alpha);
		if (!hasCollisionOrigin)
		{
			collisionOrigin = target;
			hasCollisionOrigin = true;
		}
	}

	if (definition.collision.enabled && hasCollisionOrigin && m_CollisionResolver)
	{
		const glm::vec3 arm = solved.pose.position - collisionOrigin;
		const float desiredDistance = glm::length(arm);
		if (desiredDistance > 0.0001f)
		{
			VansCameraCollisionQuery query;
			query.bindingContext = bindingContext;
			query.origin = collisionOrigin;
			query.desiredPosition = solved.pose.position;
			query.radius = definition.collision.radius;
			query.layers = definition.collision.layers;
			VansCameraCollisionResult hit;
			const bool queried = m_CollisionResolver(query, hit);
			const float minimum = std::min(
				definition.collision.minimumDistance, desiredDistance);
			if (queried && hit.blocked && std::isfinite(hit.distance))
			{
				collisionDistance = std::clamp(
					hit.distance - definition.collision.padding, minimum, desiredDistance);
				hasCollisionDistance = true;
			}
			else if (hasCollisionDistance)
			{
				const float alpha = DampingAlpha(
					definition.collision.recoverySeconds, deltaSeconds, true);
				collisionDistance += (desiredDistance - collisionDistance) * alpha;
				if (std::abs(collisionDistance - desiredDistance) <= 0.001f)
				{
					collisionDistance = desiredDistance;
					hasCollisionDistance = false;
				}
			}
			if (hasCollisionDistance)
				solved.pose.position = collisionOrigin + arm / desiredDistance *
					std::clamp(collisionDistance, minimum, desiredDistance);
		}
	}
	else
	{
		hasCollisionDistance = false;
		collisionDistance = 0.0f;
	}
	initialized = true;
	std::string ignored;
	ClampView(solved, "rig solve", ignored);
	return solved;
}

void VansCameraRuntime::ReportResolvedClamp(
	VansCameraViewId view,
	const std::string& diagnostic)
{
	if (diagnostic.empty())
	{
		m_ReportedLensClamps.erase(view);
		return;
	}
	const auto found = m_ReportedLensClamps.find(view);
	if (found != m_ReportedLensClamps.end() && found->second == diagnostic) return;
	m_ReportedLensClamps[view] = diagnostic;
	VANS_LOG_WARN("[CameraCore] resolved view: " << diagnostic);
}

VansResolvedCameraView VansCameraRuntime::ResolveView(VansCameraViewId view)
{
	if (!view) view = MainView();
	VansResolvedCameraView result;
	result.view = view;
	result.snapshot = BaseFor(view);
	std::vector<ContributionSnapshot> contributions = SnapshotContributions(view);
	std::stable_sort(contributions.begin(), contributions.end(),
		[](const ContributionSnapshot& left, const ContributionSnapshot& right)
		{
			const VansCameraContributionSortKey& a = left.order;
			const VansCameraContributionSortKey& b = right.order;
			if (a.layer != b.layer) return a.layer < b.layer;
			if (a.hierarchicalBias != b.hierarchicalBias)
				return a.hierarchicalBias < b.hierarchicalBias;
			if (a.priority != b.priority) return a.priority < b.priority;
			if (a.stableSequence != b.stableSequence)
				return a.stableSequence < b.stableSequence;
			return left.handle.value.index < right.handle.value.index;
		});
	for (const ContributionSnapshot& item : contributions)
	{
		if (!item.enabled || item.weight <= 0.0f) continue;
		const float weight = item.blendMode == VansCameraBlendMode::Exclusive
			? 1.0f : item.weight;
		auto blend = [weight](auto base, auto value) { return base + (value - base) * weight; };
		if (item.channels & VansCameraChannel_Position)
		{
			glm::vec3 position = item.value.pose.position;
			if (item.blendMode == VansCameraBlendMode::Additive &&
				item.space == VansCameraSpace::CameraLocal)
			{
				position = glm::quat(glm::radians(result.snapshot.pose.rotationDegrees)) * position;
			}
			result.snapshot.pose.position = item.blendMode == VansCameraBlendMode::Additive
				? result.snapshot.pose.position + position * weight
				: blend(result.snapshot.pose.position, position);
		}
		if (item.channels & VansCameraChannel_Rotation)
		{
			result.snapshot.pose.rotationDegrees =
				item.blendMode == VansCameraBlendMode::Additive
				? result.snapshot.pose.rotationDegrees + item.value.pose.rotationDegrees * weight
				: BlendRotation(result.snapshot.pose.rotationDegrees,
					item.value.pose.rotationDegrees, weight);
		}
		if (item.channels & VansCameraChannel_FieldOfView)
			result.snapshot.lens.fieldOfView = blend(
				result.snapshot.lens.fieldOfView, item.value.lens.fieldOfView);
		if (item.channels & VansCameraChannel_NearClip)
			result.snapshot.lens.nearClip = blend(
				result.snapshot.lens.nearClip, item.value.lens.nearClip);
		if (item.channels & VansCameraChannel_FarClip)
			result.snapshot.lens.farClip = blend(
				result.snapshot.lens.farClip, item.value.lens.farClip);
		result.appliedContributions.push_back(item.handle);
	}
	std::string diagnostic;
	if (!VansClampCameraView(result.snapshot, m_LensLimits, diagnostic))
	{
		VANS_LOG_ERROR("[CameraCore] Failed to resolve camera view: " << diagnostic);
	}
	ReportResolvedClamp(view, diagnostic);
	return result;
}

VansResolvedCameraView VansCameraRuntime::ResolveAndConsumeView(VansCameraViewId view)
{
	VansResolvedCameraView result = ResolveView(view);
	std::vector<VansCameraContributionHandle> consumed;
	for (VansCameraContributionHandle handle : result.appliedContributions)
	{
		const ContributionState* state = m_Contributions.Resolve(handle.value);
		if (state && state->request.lifetime == VansCameraContributionLifetime::ResolveOnce)
			consumed.push_back(handle);
	}
	for (VansCameraContributionHandle handle : consumed) ReleaseContribution(handle);
	return result;
}

std::vector<VansCameraRuntime::ContributionSnapshot> VansCameraRuntime::SnapshotContributions(
	VansCameraViewId view) const
{
	std::vector<ContributionSnapshot> result;
	m_Contributions.ForEach([&](VansGenerationHandle handle,
		const ContributionState& state)
	{
		if (!view || state.request.view == view)
			result.push_back({ { handle }, state.value, state.request.blendMode,
				state.request.space, state.request.order, state.request.weight,
				state.request.channels, state.request.enabled });
	});
	return result;
}

bool VansCameraRuntime::IsUserLookSuppressed(VansCameraViewId view) const
{
	if (!view) view = MainView();
	bool suppressed = false;
	m_Contributions.ForEach([&](VansGenerationHandle,
		const ContributionState& state)
	{
		if (state.request.view == view && state.request.enabled &&
			state.request.suppressUserLook)
			suppressed = true;
	});
	return suppressed;
}
}
