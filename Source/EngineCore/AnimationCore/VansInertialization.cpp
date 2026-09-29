#include "VansInertialization.h"
#include <algorithm>
#include <cmath>
#include <../../GLM/gtc/constants.hpp>

namespace VansGraphics
{
	bool VansInertializationState::IsFinite() const
	{
		const auto vec = [](const auto& v) { for (int i=0;i<v.length();++i) if (!std::isfinite(v[i])) return false; return true; };
		const auto snapshot = [&](const auto& value)
		{
			if (!std::isfinite(value.deltaTime) || value.deltaTime < 0) return false;
			for (int i=0;i<4;++i) if (!vec(value.ownerWorld[i])) return false;
			for (const auto& bone : value.bones)
				if (!vec(bone.translation) || !vec(bone.rotation) || !vec(bone.scale) || glm::dot(bone.rotation,bone.rotation) < 1e-8f) return false;
			for (const auto& [id, curve] : value.curves) if (!std::isfinite(curve.value)) return false;
			return true;
		};
		if (!snapshot(previous) || !snapshot(current) || !std::isfinite(deltaTime) || deltaTime < 0
			|| !std::isfinite(elapsed) || elapsed < 0 || !std::isfinite(duration) || duration < 0
			|| !std::isfinite(deficit) || deficit < 0) return false;
		for (float request : requests) if (!std::isfinite(request) || request < 0) return false;
		for (const auto& diff : boneDiffs)
			if (!vec(diff.translationAxis) || !vec(diff.rotationAxis) || !vec(diff.scaleAxis)
				|| !std::isfinite(diff.translation) || !std::isfinite(diff.translationSpeed)
				|| !std::isfinite(diff.angle) || !std::isfinite(diff.angularSpeed)
				|| !std::isfinite(diff.scale) || !std::isfinite(diff.scaleSpeed)) return false;
		for (const auto& [id, diff] : curveDiffs) if (!std::isfinite(diff.delta) || !std::isfinite(diff.derivative)) return false;
		return !active || boneDiffs.size() <= current.bones.size();
	}

	float VansInertializationState::Decay(float x, float v, float t, float end)
	{
		if (!std::isfinite(x) || !std::isfinite(v) || !std::isfinite(t) || !std::isfinite(end) || end < 0) return 0;
		t = std::max(t, 0.0f);
		if (t >= end - 1e-7f) return 0;
		const float sign = x < 0 ? -1.0f : 1.0f;
		x *= sign; v = std::min(v * sign, 0.0f);
		if (v < -.0001f) end = std::min(end, -5 * x / v);
		if (t >= end - 1e-7f) return 0;
		const float e2 = end*end, e3 = e2*end, e4 = e3*end, e5 = e4*end;
		const float acceleration = std::max(0.0f, (-8*end*v - 20*x)/e2);
		const float a = -.5f*(acceleration*e2 + 6*end*v + 12*x)/e5;
		const float b = .5f*(3*acceleration*e2 + 16*end*v + 30*x)/e4;
		const float c = -.5f*(3*acceleration*e2 + 12*end*v + 20*x)/e3;
		return (((((a*t+b)*t+c)*t+.5f*acceleration)*t+v)*t+x)*sign;
	}

	void VansInertializationState::Evaluate(VansPosePayload& pose, const glm::mat4& world, float teleportDistance)
	{
		if (!pose.valid) { requests.clear(); deltaTime = 0; return; }
		bool pending = false;
		if (!requests.empty() && !current.bones.empty())
		{
			float appliedDeficit = 0;
			if (active)
			{
				const bool apply = deficit > 0;
				deficit = duration - elapsed;
				if (apply) appliedDeficit = deficit;
			}
			duration = std::max(0.0f, *std::min_element(requests.begin(), requests.end()) - appliedDeficit);
			elapsed = 0; active = pending = true;
		}
		requests.clear();
		if (active)
		{
			elapsed += deltaTime;
			if (elapsed >= duration) deficit = 0;
			else deficit -= std::min(deficit, deltaTime);
			if (elapsed >= duration) { active = pending = false; boneDiffs.clear(); curveDiffs.clear(); }
		}
		if (!current.bones.empty() && !pose.localPose.empty() && teleportDistance > 0)
		{
			const auto now = glm::vec3(world * glm::vec4(pose.localPose[0].translation,1));
			const auto then = glm::vec3(current.ownerWorld * glm::vec4(current.bones[0].translation,1));
			if (glm::length(now - then) > teleportDistance)
			{
				if (pending) { active = pending = false; boneDiffs.clear(); curveDiffs.clear(); deficit = 0; }
				deltaTime = 0;
			}
		}
		if (pending)
		{
			const auto& older = previous.bones.empty() ? current : previous;
			const auto rotation = [](const glm::mat4& transform)
			{
				glm::mat3 basis(transform);
				for (int axis = 0; axis < 3; ++axis) basis[axis] = glm::normalize(basis[axis]);
				return glm::normalize(glm::quat_cast(basis));
			};
			const auto worldInverse = glm::inverse(rotation(world));
			const auto currentWorldRotation = rotation(current.ownerWorld), olderWorldRotation = rotation(older.ownerWorld);
			const bool worldRotation = std::abs((currentWorldRotation * worldInverse).w) < .999f
				|| std::abs((olderWorldRotation * worldInverse).w) < .999f;
			const auto unwind = [](float angle) { return std::remainder(angle, glm::two_pi<float>()); };
			const auto vectorDiff = [&](const glm::vec3& target, const glm::vec3& last, const glm::vec3& before,
				glm::vec3& axis, float& magnitude, float& speed, float epsilon)
			{
				const auto offset = last - target;
				magnitude = glm::length(offset); axis = magnitude > epsilon ? offset / magnitude : glm::vec3(0);
				speed = current.deltaTime > .0001f && magnitude > epsilon
					? (magnitude - glm::dot(before - target, axis)) / current.deltaTime : 0;
			};
			boneDiffs.assign(std::min({pose.localPose.size(), current.bones.size(), older.bones.size()}), {});
			for (size_t i = 0; i < boneDiffs.size(); ++i)
			{
				const auto& target = pose.localPose[i]; auto last = current.bones[i], before = older.bones[i];
				if (i == 0 && worldRotation)
				{
					last.rotation = worldInverse * currentWorldRotation * last.rotation;
					before.rotation = worldInverse * olderWorldRotation * before.rotation;
				}
				auto& diff = boneDiffs[i];
				vectorDiff(target.translation,last.translation,before.translation,diff.translationAxis,diff.translation,diff.translationSpeed,1e-6f);
				vectorDiff(target.scale,last.scale,before.scale,diff.scaleAxis,diff.scale,diff.scaleSpeed,1e-4f);
				const auto q = glm::normalize(last.rotation * glm::inverse(target.rotation));
				const glm::vec3 vector(q.x,q.y,q.z); const float magnitude = glm::length(vector);
				diff.rotationAxis = magnitude > 1e-8f ? vector / magnitude : glm::vec3(1,0,0);
				diff.angle = unwind(2*std::acos(std::clamp(q.w,-1.0f,1.0f)));
				if (diff.angle < 0) { diff.angle = -diff.angle; diff.rotationAxis = -diff.rotationAxis; }
				if (current.deltaTime > .0001f && diff.angle > .0001f)
				{
					const auto qBefore = before.rotation * glm::inverse(target.rotation);
					const float twist = unwind(2*std::atan2(glm::dot(glm::vec3(qBefore.x,qBefore.y,qBefore.z),diff.rotationAxis),qBefore.w));
					diff.angularSpeed = unwind(diff.angle - twist) / current.deltaTime;
				}
			}
			curveDiffs.clear();
			std::map<std::uint64_t, float> destination;
			for (const auto& curve : pose.curves) if (curve.present)
			{
				destination[curve.id] = curve.value;
				curveDiffs[curve.id].name = curve.name;
			}
			for (const auto& [id, curve] : current.curves) curveDiffs[id].name = curve.name;
			for (const auto& [id, curve] : older.curves) curveDiffs[id].name = curve.name;
			for (auto& [id, diff] : curveDiffs)
			{
				const auto last = current.curves.find(id);
				const auto before = older.curves.find(id);
				const float target = destination[id], v1 = last == current.curves.end() ? 0 : last->second.value;
				diff.delta = v1 - target;
				// UE5.4此处使用Delta-Value，不能擅自改成常见的上一帧差分公式。
				diff.derivative = current.deltaTime > .0001f
					? (diff.delta - target - (before == older.curves.end() ? 0 : before->second.value)) / current.deltaTime : 0;
			}
		}
		if (active)
		{
			for (size_t i = 0; i < std::min(pose.localPose.size(), boneDiffs.size()); ++i)
			{
				auto& bone = pose.localPose[i]; const auto& diff = boneDiffs[i];
				bone.translation += diff.translationAxis * (Decay(diff.translation*100.0f,diff.translationSpeed*100.0f,elapsed,duration)*.01f);
				bone.rotation = glm::normalize(glm::angleAxis(Decay(diff.angle,diff.angularSpeed,elapsed,duration),diff.rotationAxis)*bone.rotation);
				bone.scale += diff.scaleAxis * Decay(diff.scale,diff.scaleSpeed,elapsed,duration);
			}
			for (const auto& [id, diff] : curveDiffs)
			{
				auto found = std::find_if(pose.curves.begin(),pose.curves.end(),[&](const auto& curve){return curve.id==id;});
				const float offset = Decay(diff.delta,diff.derivative,elapsed,duration);
				if (found == pose.curves.end()) pose.curves.push_back({id,diff.name,offset,true});
				else { found->value += offset; found->present = true; }
			}
		}
		previous = std::move(current);
		current.bones.assign(pose.localPose.begin(),pose.localPose.end()); current.curves.clear();
		for (const auto& curve : pose.curves) if (curve.present) current.curves[curve.id] = {std::string(curve.name),curve.value};
		current.ownerWorld = world; current.deltaTime = deltaTime; deltaTime = 0;
	}
}
