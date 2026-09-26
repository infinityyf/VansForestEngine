#include "VansCombatActionService.h"
#include "VansDamageProfile.h"
#include "../../GameplayTargeting/VansSurfaceImpact.h"

#include "../VansActionServiceAdapter.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../GameplayActionSchema/VansGameplayAssetLibrary.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../RuntimeCore/VansCharacterMotion.h"
#include "../../SceneRuntime/VansComponentStorage.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace Vans
{
const VansActionServiceCapability& VansCombatActionCapability()
{
	using ValueKind = VansActionCommandValueKind;
	using ResourcePolicy = VansActionCommandResourcePolicy;
	const auto asset = [](std::string name)
	{
		return VansActionCommandField(std::move(name), ValueKind::String, true);
	};
	const auto optionalString = [](std::string name)
	{
		return VansActionCommandField(std::move(name), ValueKind::String, false,
			VansSerializedValue::String({}));
	};
	static const VansActionServiceCapability capability =
		VansActionServiceCapabilityDescriptor("Service.Combat", {
			VansActionCommandCapability("Combat.FireHitscan", ResourcePolicy::None, {
				asset("targetLayer"),
				optionalString("targetTag"), optionalString("responseAction"),
				VansActionCommandField("blockingLayers", ValueKind::Array, true,
					VansSerializedValue::Array({})),
				VansActionCommandNumberField("range", ValueKind::Float, true,
					VansSerializedValue::Float(100.0), 0.001, 1000000.0),
				VansActionCommandNumberField("pitchOffsetDegrees", ValueKind::Float, false,
					VansSerializedValue::Float(0.0), -89.0, 89.0),
				VansActionCommandNumberField("yawOffsetDegrees", ValueKind::Float, false,
					VansSerializedValue::Float(0.0), -180.0, 180.0),
				VansActionCommandNumberField("spreadRight", ValueKind::Float, false,
					VansSerializedValue::Float(0.0), -2.0, 2.0),
				VansActionCommandNumberField("spreadUp", ValueKind::Float, false,
					VansSerializedValue::Float(0.0), -2.0, 2.0)
			}),
			VansActionCommandCapability("Combat.BeginMeleeWindow", ResourcePolicy::Create, {
				asset("sourceBase"), asset("sourceTip"), optionalString("targetLayer"),
				optionalString("targetTag"), optionalString("responseAction"), optionalString("window"),
				VansActionCommandNumberField("startSeconds", ValueKind::Float, true,
					VansSerializedValue::Float(0.0), 0.0, 3600.0),
				VansActionCommandNumberField("endSeconds", ValueKind::Float, true,
					VansSerializedValue::Float(0.5), 0.0, 3600.0),
				VansActionCommandNumberField("sweepRadius", ValueKind::Float, false,
					VansSerializedValue::Float(0.1), 0.001, 1000.0),
				VansActionCommandNumberField("range", ValueKind::Float, false,
					VansSerializedValue::Float(2.0), 0.001, 1000000.0),
				VansActionCommandNumberField("halfAngleDegrees", ValueKind::Float, false,
					VansSerializedValue::Float(90.0), 0.0, 180.0),
				VansActionCommandNumberField("verticalTolerance", ValueKind::Float, false,
					VansSerializedValue::Float(1.0), 0.0, 1000000.0),
				VansActionCommandNumberField("maximumHits", ValueKind::Int, false,
					VansSerializedValue::Int(1), 1.0, 65535.0)
			}),
			VansActionCommandCapability("Combat.ResolveHit", ResourcePolicy::None, {
				VansActionCommandField("targets", ValueKind::Array, true,
					VansSerializedValue::Array({})), asset("damageProfile"), optionalString("policy")
			}),
			VansActionCommandCapability("Combat.ApplyDamageProfile", ResourcePolicy::None, {
				VansActionCommandField("target", ValueKind::Object, true,
					VansSerializedValue::Object({})), asset("damageProfile"),
				VansActionCommandNumberField("scale", ValueKind::Float, false,
					VansSerializedValue::Float(1.0), 0.0, 1000000.0)
			})
		});
	return capability;
}

namespace
{
constexpr float kGeometryEpsilon = 1.0e-5f;

std::uint64_t EntityKey(VansEntityHandle entity)
{
	return (static_cast<std::uint64_t>(entity.generation) << 32) |
		static_cast<std::uint64_t>(entity.index);
}

float ReadNumberField(
	const VansSerializedValue& object,
	std::string_view name,
	float fallback)
{
	const VansSerializedValue* value = FindObjectField(object, std::string(name));
	return value ? static_cast<float>(ReadSerializedNumber(*value, fallback)) : fallback;
}

template <typename T>
VansComponentStorage<T>* FindStorage(VansRuntimeWorld& world, std::uint16_t type)
{
	return world.FindStorage<T>(type);
}

bool ResolveWorldTransform(
	VansRuntimeWorld& world,
	VansEntityHandle entity,
	Vans::VansTransform& transform)
{
	auto* storage = FindStorage<VansRuntimeTransformComponent>(
		world, VansRuntimeComponentType_Transform);
	if (!storage || !world.IsAlive(entity)) return false;
	for (VansComponentHandle component : world.CollectComponentsOwnedBy(entity))
	{
		if (component.typeId != VansRuntimeComponentType_Transform) continue;
		const VansRuntimeTransformComponent* runtimeTransform = storage->Get(component);
		if (!runtimeTransform || runtimeTransform->transformStoreId == UINT32_MAX ||
			!Vans::VansTransformStore::IsAllocated(runtimeTransform->transformStoreId))
			return false;
		transform = Vans::VansTransformStore::Read(
			runtimeTransform->transformStoreId);
		return true;
	}
	return false;
}

float DistanceToSegment(
	const glm::vec3& point,
	const glm::vec3& first,
	const glm::vec3& second)
{
	const glm::vec3 delta = second - first;
	const float lengthSquared = glm::dot(delta, delta);
	if (!std::isfinite(lengthSquared) || lengthSquared <= kGeometryEpsilon)
		return glm::length(point - first);
	const float t = std::clamp(glm::dot(point - first, delta) / lengthSquared, 0.0f, 1.0f);
	return glm::length(point - (first + delta * t));
}

float DistanceToTriangle(
	const glm::vec3& point,
	const glm::vec3& first,
	const glm::vec3& second,
	const glm::vec3& third)
{
	// Real-Time Collision Detection 的最近点区域划分；退化三角形回退到边。
	const glm::vec3 firstSecond = second - first;
	const glm::vec3 firstThird = third - first;
	const glm::vec3 triangleNormal = glm::cross(firstSecond, firstThird);
	if (glm::dot(triangleNormal, triangleNormal) <= kGeometryEpsilon)
		return (std::min)({ DistanceToSegment(point, first, second),
			DistanceToSegment(point, second, third),
			DistanceToSegment(point, third, first) });
	const glm::vec3 firstPoint = point - first;
	const float d1 = glm::dot(firstSecond, firstPoint);
	const float d2 = glm::dot(firstThird, firstPoint);
	if (d1 <= 0.0f && d2 <= 0.0f) return glm::length(firstPoint);

	const glm::vec3 secondPoint = point - second;
	const float d3 = glm::dot(firstSecond, secondPoint);
	const float d4 = glm::dot(firstThird, secondPoint);
	if (d3 >= 0.0f && d4 <= d3) return glm::length(secondPoint);
	const float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
		return glm::length(point - (first + firstSecond * (d1 / (d1 - d3))));

	const glm::vec3 thirdPoint = point - third;
	const float d5 = glm::dot(firstSecond, thirdPoint);
	const float d6 = glm::dot(firstThird, thirdPoint);
	if (d6 >= 0.0f && d5 <= d6) return glm::length(thirdPoint);
	const float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
		return glm::length(point - (first + firstThird * (d2 / (d2 - d6))));

	const float va = d3 * d6 - d5 * d4;
	if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
	{
		const glm::vec3 edge = third - second;
		const float weight = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return glm::length(point - (second + edge * weight));
	}
	const float denominator = 1.0f / (va + vb + vc);
	const float v = vb * denominator;
	const float w = vc * denominator;
	return glm::length(point - (first + firstSecond * v + firstThird * w));
}

struct HurtBodyGeometry
{
	glm::vec3 center{ 0.0f };
	float radius = 0.0f;
	float halfHeight = 0.0f;
	glm::vec3 axis{ 1.0f, 0.0f, 0.0f };
};

bool ResolveHurtBodyGeometry(
	const VansEngine::VansPhysicsNode& node,
	HurtBodyGeometry& geometry)
{
	const std::uint32_t transformId = node.GetTransformID();
	if (transformId == UINT32_MAX ||
		!Vans::VansTransformStore::IsAllocated(transformId)) return false;
	Vans::VansTransform transform =
		Vans::VansTransformStore::Read(transformId);
	const VansEngine::PhysicsNodeProperties& properties = node.GetProperties();
	const glm::quat rotation = glm::quat(glm::radians(transform.m_Rotation));
	// 与 VansPhysicsNode 的 Shape 本地偏移、PhysX 胶囊 X 轴和缩放约定一致。
	geometry.center = transform.m_Position + rotation * properties.shapeOffset;
	geometry.axis = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
	const glm::vec3 scale = glm::abs(transform.m_Scale);
	const float maximumScale = (std::max)({ scale.x, scale.y, scale.z, kGeometryEpsilon });
	switch (properties.colliderType)
	{
	case VansEngine::PhysicsColliderType::Sphere:
		geometry.radius = properties.sphereRadius * (scale.x + scale.y + scale.z) / 3.0f;
		geometry.halfHeight = 0.0f;
		break;
	case VansEngine::PhysicsColliderType::Capsule:
		geometry.radius = properties.capsuleRadius * (scale.x + scale.z) * 0.5f;
		geometry.halfHeight = properties.capsuleHalfHeight * scale.y;
		break;
	case VansEngine::PhysicsColliderType::Box:
		geometry.radius = glm::length(properties.boxExtents * scale);
		geometry.halfHeight = 0.0f;
		break;
	default:
		geometry.radius = maximumScale;
		geometry.halfHeight = 0.0f;
		break;
	}
	return std::isfinite(geometry.radius) && geometry.radius > 0.0f;
}
}

bool VansPointInsideMeleeSector(
	const glm::vec3& point,
	float pointRadius,
	const glm::vec3& origin,
	const glm::vec3& forward,
	float range,
	float halfAngleDegrees,
	float verticalTolerance)
{
	if (!std::isfinite(pointRadius) || !std::isfinite(range) ||
		!std::isfinite(halfAngleDegrees) || !std::isfinite(verticalTolerance) ||
		pointRadius < 0.0f || range < 0.0f || halfAngleDegrees < 0.0f ||
		halfAngleDegrees > 180.0f || verticalTolerance < 0.0f) return false;
	if (std::abs(point.y - origin.y) > verticalTolerance + pointRadius) return false;
	glm::vec2 toPoint(point.x - origin.x, point.z - origin.z);
	const float distance = glm::length(toPoint);
	if (!std::isfinite(distance) || distance > range + pointRadius) return false;
	if (distance <= pointRadius + kGeometryEpsilon || halfAngleDegrees >= 180.0f) return true;
	glm::vec2 planarForward(forward.x, forward.z);
	const float forwardLength = glm::length(planarForward);
	if (!std::isfinite(forwardLength) || forwardLength <= kGeometryEpsilon) return false;
	planarForward /= forwardLength;
	toPoint /= distance;
	const float angularRadius = glm::degrees(std::asin(std::clamp(
		pointRadius / (std::max)(distance, pointRadius + kGeometryEpsilon), 0.0f, 1.0f)));
	const float acceptedHalfAngle = (std::min)(180.0f, halfAngleDegrees + angularRadius);
	return glm::dot(planarForward, toPoint) + kGeometryEpsilon >=
		std::cos(glm::radians(acceptedHalfAngle));
}

bool VansContinuousWeaponPathIntersectsSphere(
	const glm::vec3& previousBase,
	const glm::vec3& previousTip,
	const glm::vec3& currentBase,
	const glm::vec3& currentTip,
	const glm::vec3& center,
	float combinedRadius)
{
	if (!std::isfinite(combinedRadius) || combinedRadius < 0.0f) return false;
	// 两个三角形覆盖相邻帧武器线段形成的完整扫掠面，避免只检查轨迹边线时
	// 高帧间位移从目标两侧跨过却漏判。
	if (DistanceToTriangle(center, previousBase, previousTip, currentTip) <= combinedRadius ||
		DistanceToTriangle(center, previousBase, currentTip, currentBase) <= combinedRadius)
		return true;
	const glm::vec3 segments[][2] = {
		{ previousBase, previousTip },
		{ currentBase, currentTip },
		{ previousBase, currentBase },
		{ previousTip, currentTip },
		{ previousBase, currentTip },
		{ previousTip, currentBase }
	};
	for (const auto& segment : segments)
		if (DistanceToSegment(center, segment[0], segment[1]) <= combinedRadius)
			return true;
	return false;
}

namespace
{
glm::vec3 ClosestSegmentPoint(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b)
{
	const glm::vec3 d = b - a;
	const float length2 = glm::dot(d, d);
	return a + d * (length2 > 1.0e-12f
		? std::clamp(glm::dot(p - a, d) / length2, 0.0f, 1.0f) : 0.0f);
}

struct ClosestPair
{
	glm::vec3 axisPoint{ 0.0f };
	glm::vec3 weaponPoint{ 0.0f };
	float distance2 = std::numeric_limits<float>::max();
	void Consider(const glm::vec3& a, const glm::vec3& b)
	{
		const float candidate = glm::dot(a - b, a - b);
		if (candidate < distance2) { distance2 = candidate; axisPoint = a; weaponPoint = b; }
	}
};

void ClosestSegments(const glm::vec3& p, const glm::vec3& q,
	const glm::vec3& a, const glm::vec3& b, ClosestPair& best)
{
	const glm::vec3 u = q - p, v = b - a, w = p - a;
	const float uu = glm::dot(u, u), vv = glm::dot(v, v), uv = glm::dot(u, v);
	// 端点候选处理平行或退化线段，内部候选处理两条线段的公垂线。
	best.Consider(p, ClosestSegmentPoint(p, a, b));
	best.Consider(q, ClosestSegmentPoint(q, a, b));
	best.Consider(ClosestSegmentPoint(a, p, q), a);
	best.Consider(ClosestSegmentPoint(b, p, q), b);
	const float determinant = uu * vv - uv * uv;
	if (determinant > 1.0e-12f)
	{
		const float uw = glm::dot(u, w), vw = glm::dot(v, w);
		const float s = (uv * vw - vv * uw) / determinant;
		const float t = (uu * vw - uv * uw) / determinant;
		if (s >= 0.0f && s <= 1.0f && t >= 0.0f && t <= 1.0f)
			best.Consider(p + s * u, a + t * v);
	}
}

bool InTriangle(const glm::vec3& p, const glm::vec3& a,
	const glm::vec3& b, const glm::vec3& c, const glm::vec3& n)
{
	const float epsilon = -1.0e-6f * glm::dot(n, n);
	return glm::dot(glm::cross(b - a, p - a), n) >= epsilon &&
		glm::dot(glm::cross(c - b, p - b), n) >= epsilon &&
		glm::dot(glm::cross(a - c, p - c), n) >= epsilon;
}

void ClosestSegmentTriangle(const glm::vec3& p, const glm::vec3& q,
	const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, ClosestPair& best)
{
	ClosestSegments(p, q, a, b, best);
	ClosestSegments(p, q, b, c, best);
	ClosestSegments(p, q, c, a, best);
	const glm::vec3 n = glm::cross(b - a, c - a);
	const float nn = glm::dot(n, n);
	if (nn <= 1.0e-12f) return;
	for (const glm::vec3& endpoint : { p, q })
	{
		const glm::vec3 projected = endpoint - n * (glm::dot(endpoint - a, n) / nn);
		if (InTriangle(projected, a, b, c, n)) best.Consider(endpoint, projected);
	}
	const float denominator = glm::dot(q - p, n);
	if (std::abs(denominator) > 1.0e-12f)
	{
		const float t = glm::dot(a - p, n) / denominator;
		const glm::vec3 crossing = p + (q - p) * t;
		if (t >= 0.0f && t <= 1.0f && InTriangle(crossing, a, b, c, n))
			best.Consider(crossing, crossing);
	}
}
}

bool VansContinuousWeaponPathIntersectsCapsule(
	const glm::vec3& previousBase, const glm::vec3& previousTip,
	const glm::vec3& currentBase, const glm::vec3& currentTip,
	const glm::vec3& capsuleStart, const glm::vec3& capsuleEnd,
	float combinedRadius, glm::vec3* axisPoint, glm::vec3* weaponPoint)
{
	if (!std::isfinite(combinedRadius) || combinedRadius < 0.0f) return false;
	ClosestPair best;
	ClosestSegmentTriangle(capsuleStart, capsuleEnd, previousBase, previousTip, currentTip, best);
	ClosestSegmentTriangle(capsuleStart, capsuleEnd, previousBase, currentTip, currentBase, best);
	if (best.distance2 > combinedRadius * combinedRadius) return false;
	if (axisPoint) *axisPoint = best.axisPoint;
	if (weaponPoint) *weaponPoint = best.weaponPoint;
	return true;
}

VansCombatActionService::VansCombatActionService(
	VansRuntimeWorld& world,
	IVansGameplayServiceRuntime& runtime,
	const VansGameplayAssetLibrary& assets,
	VansActionServiceCapability capability, VansCombatSceneBackend backend)
	: m_World(world)
	, m_Runtime(runtime)
	, m_Assets(assets)
	, m_Capability(std::move(capability))
	, m_Backend(std::move(backend))
{
}

std::shared_ptr<VansCombatActionService> VansCombatActionService::Create(
	VansRuntimeWorld& world,
	IVansGameplayServiceRuntime& runtime,
	const VansGameplayAssetLibrary& assets,
	std::string& error, VansCombatSceneBackend backend)
{
	(void)error;
	return std::shared_ptr<VansCombatActionService>(
		new VansCombatActionService(
			world, runtime, assets, VansCombatActionCapability(), std::move(backend)));
}

VansActionCommandResult VansCombatActionService::Execute(const VansActionCommand& command)
{
	if (command.stableName == "Combat.FireHitscan") return FireHitscan(command);
	if (command.stableName == "Combat.ApplyDamageProfile") return ApplyDamageProfile(command);
	if (command.stableName != "Combat.BeginMeleeWindow")
	{
		return { VansActionError::InvalidDefinition, {}, VansSerializedValue::Object({}),
			"The production Combat service does not implement this command" };
	}
	MeleeWindow window;
	window.action = command.action;
	window.owner = command.context.Entity(VansActionContextSlots::Owner);
	window.instigator = command.context.Entity(VansActionContextSlots::Instigator);
	if (!window.instigator.IsValid()) window.instigator = window.owner;
	window.baseEntityGuid = ReadSerializedStringField(command.payload, "sourceBase");
	window.tipEntityGuid = ReadSerializedStringField(command.payload, "sourceTip");
	window.targetLayer = ReadSerializedStringField(command.payload, "targetLayer", "Enemy");
	window.targetTag = ReadSerializedStringField(command.payload, "targetTag");
	window.responseAction = ReadSerializedStringField(command.payload, "responseAction");
	window.windowName = ReadSerializedStringField(command.payload, "window", "Hit");
	window.startSeconds = ReadNumberField(command.payload, "startSeconds", 0.0f);
	window.endSeconds = ReadNumberField(command.payload, "endSeconds", 0.0f);
	window.sweepRadius = ReadNumberField(command.payload, "sweepRadius", 0.1f);
	window.range = ReadNumberField(command.payload, "range", 2.0f);
	window.halfAngleDegrees = ReadNumberField(command.payload, "halfAngleDegrees", 90.0f);
	window.verticalTolerance = ReadNumberField(command.payload, "verticalTolerance", 1.0f);
	window.maximumHits = static_cast<std::size_t>((std::max<std::int64_t>)(1,
		ReadSerializedIntField(command.payload, "maximumHits", 1)));
	if (!window.owner.IsValid() || window.baseEntityGuid.empty() ||
		window.tipEntityGuid.empty() ||
		window.endSeconds <= window.startSeconds || window.sweepRadius <= 0.0f ||
		window.range <= 0.0f || window.verticalTolerance < 0.0f)
	{
		return { VansActionError::InvalidDefinition, {}, VansSerializedValue::Object({}),
			"Combat melee window configuration is incomplete or invalid" };
	}
	if (!m_World.Entities().FindByGuid(window.baseEntityGuid).IsValid() ||
		!m_World.Entities().FindByGuid(window.tipEntityGuid).IsValid())
	{
		return { VansActionError::Rejected, {}, VansSerializedValue::Object({}),
			"Combat melee window source nodes do not resolve in the runtime scene" };
	}
	const VansGenerationHandle resource = m_Windows.Emplace(std::move(window));
	return { VansActionError::None, resource, VansSerializedValue::Object({}), {} };
}

VansActionCommandResult VansCombatActionService::FireHitscan(const VansActionCommand& command)
{
	const auto reject = [](std::string message)
	{
		return VansActionCommandResult{ VansActionError::Rejected, {}, {}, std::move(message) };
	};
	const auto owner = command.context.Entity(VansActionContextSlots::Owner);
	auto instigator = command.context.Entity(VansActionContextSlots::Instigator);
	if (!instigator.IsValid()) instigator = owner;
	glm::vec3 origin, direction;
	if (!m_Runtime.HasHost(owner) || !m_Backend.viewRay || !m_Backend.viewRay(origin, direction))
		return reject("Hitscan requires the resolved scene camera view");
	const float length = glm::length(direction);
	const float range = ReadNumberField(command.payload, "range", 0.0f);
	if (!std::isfinite(glm::length(origin)) || !std::isfinite(length) || length < kGeometryEpsilon
		|| !std::isfinite(range) || range <= 0.0f || range > 1000000.0f)
		return reject("Hitscan source direction or range is invalid");
	direction /= length;
	// 弹道偏转只作用于本发查询，不改相机；未配置的动作继续使用原视线。
	const float pitchOffset = ReadNumberField(command.payload, "pitchOffsetDegrees", 0.0f);
	const float yawOffset = ReadNumberField(command.payload, "yawOffsetDegrees", 0.0f);
	const float spreadRight = ReadNumberField(command.payload, "spreadRight", 0.0f);
	const float spreadUp = ReadNumberField(command.payload, "spreadUp", 0.0f);
	if (!std::isfinite(pitchOffset) || std::abs(pitchOffset) > 89.0f ||
		!std::isfinite(yawOffset) || std::abs(yawOffset) > 180.0f ||
		!std::isfinite(spreadRight) || std::abs(spreadRight) > 2.0f ||
		!std::isfinite(spreadUp) || std::abs(spreadUp) > 2.0f)
		return reject("Hitscan ballistic offsets are invalid");
	if (pitchOffset != 0.0f || yawOffset != 0.0f || spreadRight != 0.0f || spreadUp != 0.0f)
	{
		const float yaw = std::atan2(direction.z, direction.x) + glm::radians(yawOffset);
		const float pitch = std::asin(glm::clamp(direction.y, -1.0f, 1.0f)) + glm::radians(pitchOffset);
		const glm::vec3 forward(std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
		const glm::vec3 right(-std::sin(yaw), 0.0f, std::cos(yaw));
		const glm::vec3 up = glm::cross(right, forward);
		direction = glm::normalize(forward + right * spreadRight + up * spreadUp);
	}
	const std::string targetLayer = ReadSerializedStringField(command.payload, "targetLayer");
	const std::string targetTag = ReadSerializedStringField(command.payload, "targetTag");
	const std::string response = ReadSerializedStringField(command.payload, "responseAction");
	auto& layers = VansEngine::VansCollisionLayerManager::Get();
	int targetIndex = 0;
	if (!layers.TryGetLayerIndex(targetLayer, targetIndex)) return reject("Hitscan target layer is unknown");
	const auto* blockers = FindObjectField(command.payload, "blockingLayers");
	if (!blockers || blockers->kind != VansSerializedValue::Kind::Array)
		return reject("Hitscan blockingLayers must be an array of layer names");
	std::uint32_t blockingMask = 0;
	for (const auto& layer : blockers->arrayItems)
	{
		int index = 0;
		if (layer.kind != VansSerializedValue::Kind::String || !layers.TryGetLayerIndex(layer.stringValue, index))
			return reject("Hitscan blocking layer is unknown");
		blockingMask |= 1u << index;
	}
	std::optional<VansGameplayTagId> tag;
	if (!targetTag.empty()) tag = m_Assets.Tags().FindId(targetTag);
	if (!targetTag.empty() && !tag) return reject("Hitscan target tag is unknown");
	if (!response.empty() && !m_Assets.ResolveAction(response))
		return reject("Hitscan response Action is unknown");
	VansSurfaceQueryRequest surfaceRequest;
	surfaceRequest.origin = origin;
	surfaceRequest.direction = direction;
	surfaceRequest.maximumDistance = range;
	surfaceRequest.blockingLayerMask = blockingMask;
	surfaceRequest.regionalLayerIndex = static_cast<std::uint32_t>(targetIndex);
	surfaceRequest.owner = owner;
	surfaceRequest.instigator = instigator;
	// 旧行为只在 Default 位属于 blockingLayers 时命中无 Collider 渲染面；现改为显式请求。
	surfaceRequest.includeColliderlessSurfaces = (blockingMask & 1u) != 0u;
	VansSurfaceImpact impact;
	std::string surfaceError;
	if (!m_Backend.querySurface || !m_Backend.querySurface(surfaceRequest, impact, surfaceError))
		return reject(surfaceError.empty() ? "Hitscan surface query is unavailable" : surfaceError);
	if (impact.kind == VansSurfaceImpactKind::Regional || impact.kind == VansSurfaceImpactKind::Rigid)
		impact.hit.entity = ResolveHitTarget(impact.hit.hitEntity);
	VansTargetData hits;
	bool confirmed = false, responseActivated = false;
	if (impact.kind == VansSurfaceImpactKind::Regional)
	{
		auto hit = VansToTargetHitResult(impact);
		if (m_Runtime.HasHost(hit.entity) && (!tag || m_Runtime.HasTag(hit.entity, *tag)))
		{
			confirmed = true;
			hits.values.emplace_back(hit);
			// 查询层释放物理锁后才进入目标 GAF，受击动画和移动占用沿用目标配置。
			const bool accepted = ConfirmHit(
				command.action, owner, instigator, response, "Shot", "hitscan", hit);
			responseActivated = !response.empty() && accepted;
		}
	}
	const auto shotId = m_NextShotId++;
	auto& receipt = m_Shots[shotId % m_Shots.size()];
	receipt = {shotId, command.action, owner, instigator,
		confirmed ? std::get<VansTargetHitResult>(hits.values.front()) : VansTargetHitResult{}, direction, false};
	const auto encodeVector = [](const glm::vec3& value)
	{
		return VansSerializedValue::Object({ { "x", VansSerializedValue::Float(value.x) },
			{ "y", VansSerializedValue::Float(value.y) }, { "z", VansSerializedValue::Float(value.z) } });
	};
	VansSerializedValue output = VansSerializedValue::Object({
		{ "shot", VansSerializedValue::Object({{"id", VansSerializedValue::Int(shotId)}}) },
		{ "hit", VansSerializedValue::Bool(confirmed) }, { "blocked", VansSerializedValue::Bool(impact.kind != VansSurfaceImpactKind::None && !confirmed) },
		{ "responseActivated", VansSerializedValue::Bool(responseActivated) },
		{ "origin", encodeVector(origin) }, { "direction", encodeVector(direction) },
		{ "distance", VansSerializedValue::Float(impact.kind != VansSurfaceImpactKind::None ? impact.hit.distance : range) },
		{ "targetData", VansEncodeTargetData(hits) }
		, { "surfaceImpact", VansEncodeSurfaceImpact(impact) }
	});
	VansActionEvent event;
	event.stableName = "Combat.Shot";
	event.type = VansMakeStableId<VansActionFieldIdTag>(event.stableName);
	event.source = owner;
	event.target = owner;
	event.payload = output;
	std::string eventError;
	if (!m_Runtime.EnqueueActionEvent(owner, command.action, std::move(event), eventError))
		VANS_LOG_WARN("[GAF Combat] Could not emit shot event: " << eventError);
	VANS_LOG("[GAF Combat] Hitscan shot source=" << owner.index << " hit=" << confirmed
		<< " blocked=" << (impact.kind != VansSurfaceImpactKind::None && !confirmed) << " response=" << responseActivated
		<< " origin=" << origin.x << "," << origin.y << "," << origin.z
		<< " direction=" << direction.x << "," << direction.y << "," << direction.z
		<< " surfaceKind=" << static_cast<int>(impact.kind) << " component=" << impact.hit.componentGuid
		<< " point=" << impact.hit.position[0] << "," << impact.hit.position[1] << "," << impact.hit.position[2]
		<< " normal=" << impact.hit.normal[0] << "," << impact.hit.normal[1] << "," << impact.hit.normal[2]);
	// 空枪也是一次成功执行的射击，动画按原来的完成事件结束。
	return { VansActionError::None, {}, std::move(output), {} };
}

void VansCombatActionService::EmitWindowEvent(MeleeWindow& window, std::string_view edge)
{
	if (!m_Runtime.HasHost(window.owner)) return;
	VansActionEvent event;
	event.stableName = "Action.Window." + window.windowName + "." + std::string(edge);
	event.type = VansMakeStableId<VansActionFieldIdTag>(event.stableName);
	event.source = window.owner;
	event.target = window.owner;
	event.payload = VansSerializedValue::Object({
		{ "elapsedSeconds", VansSerializedValue::Float(window.elapsedSeconds) }
	});
	std::string error;
	if (!m_Runtime.EnqueueActionEvent(window.owner, window.action, std::move(event), error))
		VANS_LOG_WARN("[GAF Combat] Could not emit window edge: " << error);
}

VansActionCommandResult VansCombatActionService::ApplyDamageProfile(const VansActionCommand& command)
{
    using V = VansSerializedValue;
    const auto skip = [](const char* reason) {
        return VansActionCommandResult{VansActionError::None, {},
            V::Object({{"applied", V::Bool(false)}, {"reason", V::String(reason)}}), {}};
    };
    const auto reject = [](const char* reason) {
        return VansActionCommandResult{VansActionError::Rejected, {}, V::Object({}), reason};
    };
    const auto* target = FindObjectField(command.payload, "target");
    const auto id = target ? ReadSerializedIntField(*target, "id", 0) : 0;
    if (id <= 0) return reject("Damage requires a hitscan receipt");
    auto& shot = m_Shots[static_cast<std::uint64_t>(id) % m_Shots.size()];
    const auto owner = command.context.Entity(VansActionContextSlots::Owner);
    if (shot.id != static_cast<std::uint64_t>(id) || shot.owner != owner || shot.action != command.action)
        return reject("Damage receipt is stale or belongs to another Action");
    if (shot.consumed) return skip("AlreadyApplied");
    const auto* profile = m_Assets.ResolveExtensionAssetAs<VansDamageProfile>(
        ReadSerializedStringField(command.payload, "damageProfile"), VansDamageProfileType);
    if (!profile) return reject("Damage profile is unresolved");
    const auto healthId = VansMakeStableId<VansAttributeIdTag>(profile->healthAttribute);
    if (!m_Assets.Attributes().Resolve(healthId)) return reject("Damage Health attribute is unknown");
    const auto* scaleField = FindObjectField(command.payload, "scale");
    const double scale = scaleField ? ReadSerializedNumber(*scaleField, 1) : 1;
    if (!std::isfinite(scale) || scale < 0 || scale > 1000000) return reject("Damage scale is invalid");
    if (!m_Runtime.HasHost(shot.hit.entity) || !m_World.IsAlive(shot.hit.entity))
    { shot.consumed = true; return skip("NoTarget"); }
    const auto region = profile->regionMultipliers.find(shot.hit.region);
    if (region == profile->regionMultipliers.end()) return reject("Damage profile does not define the hit region");
    double previous = 0.0;
    if (!m_Runtime.ReadAttribute(shot.hit.entity, healthId, previous))
    { shot.consumed = true; return skip("NoTarget"); }
    if (previous <= 0) { shot.consumed = true; return skip("Dead"); }
    const double damage = profile->baseDamage * region->second * scale *
        std::pow(profile->rangeModifier, shot.hit.distance / profile->rangeStepMeters);
    if (!std::isfinite(damage) || damage < 0) return reject("Damage calculation is invalid");
    // 先消耗凭据和提交 HP，再异步通知表现层；致死同帧到达的下一发只能看到 0 HP。
    shot.consumed = true;
    const double applied = (std::min)(previous, damage);
    double remaining = previous;
    if (!m_Runtime.ApplyBaseAttribute(shot.hit.entity, healthId,
        VansAttributeBaseOperation::Add, -applied, remaining))
        return reject("Damage Health update failed");
    const bool lethal = remaining <= 0;
    const auto vector = [](const glm::vec3& v) {
        return V::Object({{"x", V::Float(v.x)}, {"y", V::Float(v.y)}, {"z", V::Float(v.z)}});
    };
    V result = V::Object({{"applied", V::Bool(true)}, {"shotId", V::Int(id)},
        {"damage", V::Float(damage)}, {"appliedDamage", V::Float(applied)},
        {"oldHealth", V::Float(previous)}, {"newHealth", V::Float(remaining)}, {"lethal", V::Bool(lethal)},
        {"hitRegion", V::String(shot.hit.region)}, {"hitComponentGuid", V::String(shot.hit.componentGuid)},
        {"hit", VansEncodeTargetData(VansTargetData{{shot.hit}})},
        {"position", V::Array({V::Float(shot.hit.position[0]), V::Float(shot.hit.position[1]), V::Float(shot.hit.position[2])})},
        {"direction", vector(shot.direction)}, {"distance", V::Float(shot.hit.distance)},
        {"deathImpulse", V::Float(profile->deathImpulse)}, {"weapon", V::String(profile->stableName)}});
    for (const char* name : {"Combat.DamageReceived", "Combat.Died"})
    {
        if (!lethal && std::string_view(name) == "Combat.Died") continue;
        VansActionEvent event;
        event.stableName = name; event.type = VansMakeStableId<VansActionFieldIdTag>(name);
        event.source = owner; event.target = shot.hit.entity; event.payload = result;
        m_Runtime.PublishGameplayEvent(shot.hit.entity, std::move(event));
    }
    VANS_LOG("[GAF Damage] shot=" << id << " target=" << shot.hit.entity.index << " region=" << shot.hit.region
        << " damage=" << damage << " health=" << previous << "->" << remaining << " lethal=" << lethal);
    return {VansActionError::None, {}, std::move(result), {}};
}

bool VansCombatActionService::ConfirmHit(VansActionHandle action, VansEntityHandle owner,
	VansEntityHandle instigator, const std::string& responseAction, const std::string& hitName,
	std::string_view hitType, const VansTargetHitResult& hit)
{
	// 命中确认与受击表现共用一条通路，保留实际肢体和所属角色的双重身份。
	if (m_Runtime.HasHost(owner))
	{
		VansActionEvent event;
		event.stableName = "Combat.Hit";
		event.type = VansMakeStableId<VansActionFieldIdTag>(event.stableName);
		event.source = owner;
		event.target = hit.entity;
		event.payload = VansSerializedValue::Object({
			{ "hitWindow", VansSerializedValue::String(hitName) },
			{ "hitType", VansSerializedValue::String(std::string(hitType)) },
			{ "hitRegion", VansSerializedValue::String(hit.region) },
			{ "targetData", VansEncodeTargetData(VansTargetData{ { hit } }) }
		});
		std::string error;
		if (!m_Runtime.EnqueueActionEvent(owner, action, std::move(event), error))
			VANS_LOG_WARN("[GAF Combat] Could not emit hit event: " << error);
	}
	const VansEntityHandle target = hit.entity;
	if (responseAction.empty()) return true;
	const auto response = m_Assets.ResolveAction(responseAction);
	if (!m_Runtime.HasHost(target) || !response) return false;
	VansActionContext context;
	context.SetEntity(VansActionContextSlots::Owner, target);
	context.SetEntity(VansActionContextSlots::Instigator, instigator);
	context.SetEntity(VansActionContextSlots::Source, owner);
	context.SetEntity(VansActionContextSlots::PrimaryTarget, owner);
	context.SetSerialized(VansActionContextSlots::Payload, VansSerializedValue::Object({
		{ "hitWindow", VansSerializedValue::String(hitName) },
		{ "hitType", VansSerializedValue::String(std::string(hitType)) },
		{ "hitRegion", VansSerializedValue::String(hit.region) },
		{ "hitComponentGuid", VansSerializedValue::String(hit.componentGuid) },
		{ "hit", VansEncodeTargetData(VansTargetData{ { hit } }) },
		{ "sourceEntity", VansSerializedValue::Object({
			{ "index", VansSerializedValue::Int(owner.index) },
			{ "generation", VansSerializedValue::Int(owner.generation) }
		}) }
	}));
	const VansActionResult result = m_Runtime.ActivateAction(
		target, response->id, std::move(context), VansTargetData{ { hit } });
	if (!result)
	{
		VANS_LOG_WARN("[GAF Combat] Hit response activation failed target=" << target.index
			<< " action='" << responseAction << "': " << result.message);
		return false;
	}
	VANS_LOG("[GAF Combat] Validated " << hitType << " hit source=" << owner.index
		<< " target=" << target.index << " window='" << hitName
		<< "' region='" << hit.region
		<< "' response='" << responseAction << "'");
	return true;
}

VansEntityHandle VansCombatActionService::ResolveHitTarget(VansEntityHandle entity) const
{
	// 部位实体挂在骨骼下，响应与去重归属最近的角色 ActionHost。
	while (m_World.IsAlive(entity))
	{
		if (m_Runtime.HasHost(entity)) return entity;
		const auto* record = m_World.Entities().Get(entity);
		entity = record ? record->parent : VansEntityHandle{};
	}
	return {};
}

bool VansCombatActionService::SampleWindow(MeleeWindow& window)
{
	const VansEntityHandle baseEntity = m_World.Entities().FindByGuid(window.baseEntityGuid);
	const VansEntityHandle tipEntity = m_World.Entities().FindByGuid(window.tipEntityGuid);
	Vans::VansTransform baseTransform;
	Vans::VansTransform tipTransform;
	Vans::VansTransform ownerTransform;
	if (!ResolveWorldTransform(m_World, baseEntity, baseTransform) ||
		!ResolveWorldTransform(m_World, tipEntity, tipTransform) ||
		!ResolveWorldTransform(m_World, window.owner, ownerTransform)) return false;
	const glm::vec3 currentBase = baseTransform.m_Position;
	const glm::vec3 currentTip = tipTransform.m_Position;
	if (!window.hasPrevious)
	{
		window.previousBase = currentBase;
		window.previousTip = currentTip;
		window.hasPrevious = true;
	}
	// 角色根 Transform 还包含模型导入所需的 pitch/roll/scale 修正。打击方向
	// 必须和 CCT、Root Motion 共用 locomotion yaw，否则 Survival 的 X=-90°
	// 会把局部 +Z 转成竖直方向，清除 Y 后扇形方向就会丢失。
	const glm::vec3 forward = LocomotionLocalToWorldPlanar(
		glm::vec3(0.0f, 0.0f, 1.0f), ownerTransform.m_Rotation.y);

	VansCombatDebugMeleeWindow debug;
	const VansEntityRecord* ownerRecord = m_World.Entities().Get(window.owner);
	debug.owner = ownerRecord ? ownerRecord->name : std::to_string(window.owner.index);
	debug.window = window.windowName;
	debug.active = window.sampleActive;
	debug.origin = ownerTransform.m_Position;
	debug.forward = forward;
	debug.previousBase = window.previousBase;
	debug.previousTip = window.previousTip;
	debug.currentBase = currentBase;
	debug.currentTip = currentTip;
	debug.range = window.range;
	debug.halfAngleDegrees = window.halfAngleDegrees;
	debug.sweepRadius = window.sweepRadius;
	debug.hitCount = window.hitTargets.size();
	const std::size_t debugWindowIndex = m_DebugSnapshot.windows.size();
	m_DebugSnapshot.windows.push_back(std::move(debug));

	auto* storage = FindStorage<VansRuntimePhysicsComponent>(
		m_World, VansRuntimeComponentType_Physics);
	std::vector<VansTargetHitResult> candidates;
	if (storage)
	{
		const auto& headers = storage->Headers();
		const auto& bodies = storage->DenseData();
		for (std::size_t index = 0; index < headers.size() && index < bodies.size(); ++index)
		{
			const VansComponentHeader& header = headers[index];
			const VansRuntimePhysicsComponent& body = bodies[index];
			if (!header.effectiveEnabled || header.owner == window.owner ||
				!body.physicsNode || !body.physicsNode->IsEnabled()) continue;
			const VansEngine::PhysicsNodeProperties& properties =
				body.physicsNode->GetProperties();
			if (!properties.isTrigger || properties.layerName != window.targetLayer) continue;
			HurtBodyGeometry geometry;
			if (!ResolveHurtBodyGeometry(*body.physicsNode, geometry)) continue;
			const VansEntityHandle target = ResolveHitTarget(header.owner);
			if (!target.IsValid() || target == window.owner) continue;
			if (!window.targetTag.empty())
			{
				const std::optional<VansGameplayTagId> tag =
					m_Assets.Tags().FindId(window.targetTag);
				if (!tag || !m_Runtime.HasTag(target, *tag)) continue;
			}
			const std::uint64_t key = EntityKey(target);
			if (window.sampleActive && window.hitTargets.size() < window.maximumHits &&
				window.hitTargets.count(key) == 0)
			{
				glm::vec3 axisPoint, weaponPoint;
				const bool sectorPassed = VansPointInsideMeleeSector(
					geometry.center, geometry.radius + geometry.halfHeight, ownerTransform.m_Position, forward,
					window.range, window.halfAngleDegrees, window.verticalTolerance);
				const bool pathPassed = sectorPassed && VansContinuousWeaponPathIntersectsCapsule(
					window.previousBase, window.previousTip, currentBase, currentTip,
					geometry.center - geometry.axis * geometry.halfHeight,
					geometry.center + geometry.axis * geometry.halfHeight,
					window.sweepRadius + geometry.radius, &axisPoint, &weaponPoint);
				if (!pathPassed) continue;
				glm::vec3 normal = weaponPoint - axisPoint;
				if (glm::length(normal) < kGeometryEpsilon)
				{
					normal = window.previousBase - axisPoint;
					normal -= geometry.axis * glm::dot(normal, geometry.axis);
					if (glm::length(normal) < kGeometryEpsilon)
						normal = glm::cross(geometry.axis, std::abs(geometry.axis.y) < 0.9f
							? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1));
				}
				normal = glm::normalize(normal);
				const glm::vec3 position = axisPoint + normal * geometry.radius;
				if (!VansPointInsideMeleeSector(position, window.sweepRadius,
					ownerTransform.m_Position, forward, window.range,
					window.halfAngleDegrees, window.verticalTolerance)) continue;
				VansTargetHitResult hit;
				hit.entity = target;
				hit.hitEntity = header.owner;
				hit.componentGuid = header.stableGuid;
				hit.region = properties.hitRegion;
				hit.position = { position.x, position.y, position.z };
				hit.normal = { normal.x, normal.y, normal.z };
				hit.distance = glm::length(position - window.previousBase);
				candidates.push_back(std::move(hit));
			}
		}
	}
	// 同一帧碰到多个部位时按距前一帧武器根部的接触距离排序，GUID 打破平局；
	// 选择与实体存储顺序无关，角色在同一个攻击窗口内仍只响应一次。
	std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b)
	{
		return a.distance == b.distance ? a.componentGuid < b.componentGuid : a.distance < b.distance;
	});
	for (const auto& hit : candidates)
	{
		if (window.hitTargets.size() >= window.maximumHits) break;
		const auto key = EntityKey(hit.entity);
		if (window.hitTargets.count(key)) continue;
		window.hitTargets.insert(key);
		ConfirmHit(window.action, window.owner, window.instigator,
			window.responseAction, window.windowName, "melee", hit);
		for (auto& body : m_DebugSnapshot.hurtBodies)
			if (body.componentGuid == hit.componentGuid) body.hit = true;
	}
	// 命中可能在本次采样中产生，Inspector 必须在同一帧看到更新后的累计值。
	// 否则轨迹与 HurtBody 已显示命中，而窗口计数仍会滞后一帧。
	m_DebugSnapshot.windows[debugWindowIndex].hitCount = window.hitTargets.size();
	window.previousBase = currentBase;
	window.previousTip = currentTip;
	return true;
}

void VansCombatActionService::Tick(double deltaSeconds)
{
	m_DebugSnapshot = {};
	m_DebugSnapshot.available = true;
	if (const auto* storage = FindStorage<VansRuntimePhysicsComponent>(
		m_World, VansRuntimeComponentType_Physics))
	{
		const auto& headers = storage->Headers();
		const auto& bodies = storage->DenseData();
		for (std::size_t i = 0; i < headers.size() && i < bodies.size(); ++i)
		{
			const auto* node = bodies[i].physicsNode;
			if (!headers[i].effectiveEnabled || !node || !node->IsEnabled() ||
				!node->GetProperties().isTrigger) continue;
			const auto target = ResolveHitTarget(headers[i].owner);
			if (!target.IsValid()) continue;
			HurtBodyGeometry geometry;
			if (!ResolveHurtBodyGeometry(*node, geometry)) continue;
			const auto* record = m_World.Entities().Get(target);
			m_DebugSnapshot.hurtBodies.push_back({ record->name, geometry.center,
				geometry.radius, geometry.halfHeight, false, geometry.axis,
				node->GetProperties().hitRegion, headers[i].stableGuid });
		}
	}
	const double dt = std::clamp(deltaSeconds, 0.0, 0.25);
	m_Windows.ForEach([&](VansGenerationHandle, MeleeWindow& window)
	{
		const double previousElapsed = window.elapsedSeconds;
		window.elapsedSeconds += dt;
		window.sampleActive = previousElapsed <= window.endSeconds &&
			window.elapsedSeconds + 1.0e-12 >= window.startSeconds;
		if (!window.windowOpen && window.sampleActive)
		{
			window.windowOpen = true;
			EmitWindowEvent(window, "Open");
		}
		SampleWindow(window);
		if (window.windowOpen && window.elapsedSeconds + 1.0e-12 >= window.endSeconds)
		{
			EmitWindowEvent(window, "Close");
			window.windowOpen = false;
		}
	});
}

bool VansCombatActionService::Release(VansGenerationHandle resource, std::string& error)
{
	MeleeWindow* window = m_Windows.Resolve(resource);
	if (!window)
	{
		error = "Combat melee window resource is stale";
		return false;
	}
	if (window->windowOpen)
	{
		EmitWindowEvent(*window, "Close");
		window->windowOpen = false;
	}
	return m_Windows.Release(resource);
}

VansCombatDebugSnapshot VansCombatActionService::CaptureDebugSnapshot() const
{
	return m_DebugSnapshot;
}
}
