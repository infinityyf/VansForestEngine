#pragma once

#include "../RuntimeCore/VansGenerationPool.h"
#include "../RuntimeCore/VansStableIdentity.h"

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vans
{
struct VansCameraViewIdTag;
struct VansCameraRigIdTag;
struct VansCameraShakeIdTag;
struct VansCameraContributionDomainIdTag;

using VansCameraViewId = VansStableId<VansCameraViewIdTag>;
using VansCameraRigId = VansStableId<VansCameraRigIdTag>;
using VansCameraShakeId = VansStableId<VansCameraShakeIdTag>;
using VansCameraContributionDomainId = VansStableId<VansCameraContributionDomainIdTag>;

struct VansCameraRigHandleTag;
struct VansCameraContributionHandleTag;
struct VansCameraShakeHandleTag;

struct VansCameraRigHandle
{
	VansGenerationHandle value;
	bool IsValid() const { return value.IsValid(); }
	explicit operator bool() const { return IsValid(); }
	friend bool operator==(VansCameraRigHandle left, VansCameraRigHandle right)
	{
		return left.value == right.value;
	}
	friend bool operator!=(VansCameraRigHandle left, VansCameraRigHandle right)
	{
		return !(left == right);
	}
};

struct VansCameraContributionHandle
{
	VansGenerationHandle value;
	bool IsValid() const { return value.IsValid(); }
	explicit operator bool() const { return IsValid(); }
	friend bool operator==(VansCameraContributionHandle left, VansCameraContributionHandle right)
	{
		return left.value == right.value;
	}
	friend bool operator!=(VansCameraContributionHandle left, VansCameraContributionHandle right)
	{
		return !(left == right);
	}
};

struct VansCameraShakeHandle
{
	VansGenerationHandle value;
	bool IsValid() const { return value.IsValid(); }
	explicit operator bool() const { return IsValid(); }
	friend bool operator==(VansCameraShakeHandle left, VansCameraShakeHandle right)
	{
		return left.value == right.value;
	}
	friend bool operator!=(VansCameraShakeHandle left, VansCameraShakeHandle right)
	{
		return !(left == right);
	}
};

enum VansCameraChannel : std::uint32_t
{
	VansCameraChannel_Position = 1u << 0u,
	VansCameraChannel_Rotation = 1u << 1u,
	VansCameraChannel_FieldOfView = 1u << 2u,
	VansCameraChannel_NearClip = 1u << 3u,
	VansCameraChannel_FarClip = 1u << 4u,
	VansCameraChannel_All = (1u << 5u) - 1u
};

struct VansCameraPose
{
	glm::vec3 position{ 0.0f };
	glm::vec3 rotationDegrees{ 0.0f };
};

struct VansCameraLens
{
	float fieldOfView = 45.0f;
	float nearClip = 0.1f;
	float farClip = 10000.0f;
};

struct VansCameraViewSnapshot
{
	VansCameraPose pose;
	VansCameraLens lens;
};

struct VansCameraFollowProfile
{
	bool enabled = false;
	std::string mode = "Fixed";
	std::string targetBinding;
	glm::vec3 localOffset{ 0.0f };
	float positionDamping = 0.0f;
};

struct VansCameraLookAtProfile
{
	bool enabled = false;
	std::string targetBinding;
	glm::vec3 worldOffset{ 0.0f };
	float rotationDamping = 0.0f;
};

struct VansCameraCollisionProfile
{
	bool enabled = false;
	float radius = 0.2f;
	float minimumDistance = 0.1f;
	float padding = 0.05f;
	float recoverySeconds = 0.2f;
	std::vector<std::string> layers;
};

struct VansCameraCompositionProfile
{
	float screenX = 0.5f;
	float screenY = 0.5f;
	float deadZoneX = 0.1f;
	float deadZoneY = 0.1f;
};

struct VansCameraRigDefinition
{
	VansCameraRigId id;
	std::string stableName;
	VansCameraViewSnapshot initialView;
	VansCameraFollowProfile follow;
	VansCameraLookAtProfile lookAt;
	VansCameraCollisionProfile collision;
	float focusDistance = 10.0f;
	VansCameraCompositionProfile composition;
};

struct VansCameraBindingSnapshot
{
	VansCameraPose pose;
};

struct VansCameraCollisionQuery
{
	VansGenerationHandle bindingContext;
	glm::vec3 origin{ 0.0f };
	glm::vec3 desiredPosition{ 0.0f };
	float radius = 0.0f;
	std::vector<std::string> layers;
};

struct VansCameraCollisionResult
{
	bool blocked = false;
	float distance = 0.0f;
};

using VansCameraBindingResolver = std::function<bool(
	VansGenerationHandle, std::string_view, VansCameraBindingSnapshot&)>;
using VansCameraCollisionResolver = std::function<bool(
	const VansCameraCollisionQuery&, VansCameraCollisionResult&)>;

struct VansCameraShakeDefinition
{
	VansCameraShakeId id;
	std::string stableName;
	glm::vec3 translationAmplitude{ 0.0f };
	glm::vec3 rotationAmplitude{ 0.0f };
	float frequency = 12.0f;
	float attackSeconds = 0.05f;
	float sustainSeconds = 0.1f;
	float releaseSeconds = 0.15f;
	float minimumDistance = 0.0f;
	float maximumDistance = 25.0f;
	float falloffExponent = 1.0f;
	std::uint64_t seed = 0;
};

enum class VansCameraContributionKind : std::uint8_t
{
	Shot,
	Lens,
	PoseOffset,
	Shake,
	Impulse,
	LockOn,
	Custom
};

enum class VansCameraBlendMode : std::uint8_t
{
	Exclusive,
	Weighted,
	Additive
};

enum class VansCameraSpace : std::uint8_t
{
	World,
	CameraLocal
};

struct VansCameraContributionOwner
{
	VansCameraContributionDomainId domain;
	VansGenerationHandle writer;

	bool IsValid() const { return domain.IsValid() && writer.IsValid(); }
	friend bool operator==(const VansCameraContributionOwner& left,
		const VansCameraContributionOwner& right)
	{
		return left.domain == right.domain && left.writer == right.writer;
	}
	friend bool operator!=(const VansCameraContributionOwner& left,
		const VansCameraContributionOwner& right)
	{
		return !(left == right);
	}
};

struct VansCameraContributionSortKey
{
	std::int32_t layer = 0;
	std::int32_t hierarchicalBias = 0;
	std::int32_t priority = 0;
	std::uint64_t stableSequence = 0;
};

struct VansCameraContribution
{
	VansCameraViewId view;
	VansCameraContributionOwner owner;
	VansCameraContributionKind kind = VansCameraContributionKind::Custom;
	VansCameraViewSnapshot value;
	VansCameraBlendMode blendMode = VansCameraBlendMode::Exclusive;
	VansCameraSpace space = VansCameraSpace::World;
	VansCameraContributionSortKey order;
	float weight = 1.0f;
	std::uint32_t channels = VansCameraChannel_All;
	bool enabled = true;
	bool consumeAfterResolve = false;
	VansCameraShakeHandle shake;
	float shakeScale = 1.0f;
	double shakeElapsedSeconds = 0.0;
	std::uint64_t shakeSeed = 0;
	bool hasShakeOrigin = false;
	glm::vec3 shakeOrigin{ 0.0f };
	VansCameraRigHandle rig;
	VansGenerationHandle bindingContext;
	bool rigSolveInitialized = false;
	bool hasCollisionDistance = false;
	float collisionDistance = 0.0f;
	bool suppressUserLook = false;
};

struct VansCameraContributionSnapshot
{
	VansCameraContributionHandle handle;
	VansCameraContribution contribution;
};

struct VansResolvedCameraView
{
	VansCameraViewId view;
	VansCameraViewSnapshot snapshot;
	std::vector<VansCameraContributionHandle> appliedContributions;
};

class VansCameraRuntime
{
public:
	static VansCameraViewId MainView();

	VansCameraRigHandle RegisterRig(VansCameraRigDefinition definition, std::string& error);
	bool UnregisterRig(VansCameraRigHandle rig);
	const VansCameraRigDefinition* ResolveRig(VansCameraRigHandle rig) const;
	const VansCameraRigDefinition* ResolveRig(VansCameraRigId rig) const;
	bool BindViewRig(VansCameraViewId view, VansCameraRigHandle rig, std::string& error,
		VansGenerationHandle bindingContext = {});
	bool SetBaseView(VansCameraViewId view, VansCameraViewSnapshot snapshot, std::string& error);
	void SetBindingResolver(VansCameraBindingResolver resolver)
	{
		m_BindingResolver = std::move(resolver);
	}
	void SetCollisionResolver(VansCameraCollisionResolver resolver)
	{
		m_CollisionResolver = std::move(resolver);
	}
	VansCameraShakeHandle RegisterShake(VansCameraShakeDefinition definition, std::string& error);
	bool UnregisterShake(VansCameraShakeHandle shake);
	const VansCameraShakeDefinition* ResolveShake(VansCameraShakeHandle shake) const;
	const VansCameraShakeDefinition* ResolveShake(VansCameraShakeId shake) const;
	void Advance(double deltaSeconds);

	VansCameraContributionHandle AddContribution(
		VansCameraContribution contribution, std::string& error);
	VansCameraContributionHandle UpsertContribution(
		VansCameraContribution contribution, std::string& error);
	bool UpdateContribution(
		VansCameraContributionHandle handle,
		VansCameraContribution contribution,
		std::string& error);
	const VansCameraContribution* ResolveContribution(
		VansCameraContributionHandle handle) const;
	bool ReleaseContribution(VansCameraContributionHandle handle);
	bool ReleaseOwner(VansCameraContributionOwner owner);
	std::size_t ReleaseDomain(VansCameraContributionDomainId domain);
	void ClearContributions();
	void Clear();

	VansResolvedCameraView ResolveView(VansCameraViewId view) const;
	VansResolvedCameraView ResolveAndConsumeView(VansCameraViewId view);
	std::vector<VansCameraContributionSnapshot> Contributions(VansCameraViewId view = {}) const;
	bool IsUserLookSuppressed(VansCameraViewId view = {}) const;
	std::size_t ContributionCount() const { return m_Contributions.ActiveCount(); }

private:
	struct OwnerHash
	{
		std::size_t operator()(const VansCameraContributionOwner& owner) const noexcept;
	};
	struct ViewState
	{
		VansCameraRigHandle rig;
		VansGenerationHandle bindingContext;
		VansCameraViewSnapshot base;
		bool hasExplicitBase = false;
		VansCameraViewSnapshot solved;
		bool rigSolveInitialized = false;
		bool hasCollisionDistance = false;
		float collisionDistance = 0.0f;
	};

	static bool ValidateView(VansCameraViewSnapshot& view, std::string& error);
	static bool ValidateContribution(VansCameraContribution& contribution, std::string& error);
	VansCameraViewSnapshot SolveRig(
		const VansCameraRigDefinition& definition,
		VansGenerationHandle bindingContext,
		VansCameraViewSnapshot previous,
		bool& initialized,
		bool& hasCollisionDistance,
		float& collisionDistance,
		double deltaSeconds);
	VansCameraViewSnapshot BaseFor(VansCameraViewId view) const;

	VansGenerationPool<VansCameraRigDefinition> m_Rigs;
	VansGenerationPool<VansCameraContribution> m_Contributions;
	VansGenerationPool<VansCameraShakeDefinition> m_Shakes;
	std::unordered_map<VansCameraRigId, VansCameraRigHandle> m_RigIds;
	std::unordered_map<VansCameraShakeId, VansCameraShakeHandle> m_ShakeIds;
	std::unordered_map<VansCameraContributionOwner, VansCameraContributionHandle, OwnerHash> m_Owners;
	std::unordered_map<VansCameraViewId, ViewState> m_Views;
	VansCameraBindingResolver m_BindingResolver;
	VansCameraCollisionResolver m_CollisionResolver;
	std::uint64_t m_NextSequence = 1;
};
}
