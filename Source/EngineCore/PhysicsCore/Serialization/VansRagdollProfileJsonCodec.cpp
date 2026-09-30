#include "VansRagdollProfileJsonCodec.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <utility>
#include <set>

namespace VansEngine
{
namespace
{
const char* MotionName(RagdollJointMotion motion)
{
    switch (motion)
    {
    case RagdollJointMotion::Locked: return "locked";
    case RagdollJointMotion::Limited: return "limited";
    case RagdollJointMotion::Free: return "free";
    }
    return "";
}

bool ReadMotion(const RagdollJson& value, RagdollJointMotion& motion, std::string& error)
{
    if (value.is_string())
    {
        const auto name = value.get<std::string>();
        for (auto candidate : {RagdollJointMotion::Locked, RagdollJointMotion::Limited, RagdollJointMotion::Free})
            if (name == MotionName(candidate)) { motion = candidate; return true; }
    }
    error = "Ragdoll joint motion must be locked, limited or free";
    return false;
}

bool ReadDriveFlags(const RagdollJson& item, const char* key, std::array<bool,3>& flags,
    std::string& error)
{
    if (!item.contains(key)) return true;
    const auto& values = item[key];
    if (!values.is_array() || values.size() != 3 ||
        !values[0].is_boolean() || !values[1].is_boolean() || !values[2].is_boolean())
    { error = std::string("Ragdoll field '") + key + "' must contain three booleans"; return false; }
    for (int i=0; i<3; ++i) flags[i] = values[i].get<bool>();
    return true;
}

bool ReadVec3(
    const RagdollJson& source,
    const char* key,
    const glm::vec3& defaultValue,
    glm::vec3& value,
    std::string& error)
{
    if (!source.contains(key))
    {
        value = defaultValue;
        return true;
    }
    if (!source[key].is_array() || source[key].size() != 3 ||
        !source[key][0].is_number() || !source[key][1].is_number() ||
        !source[key][2].is_number())
    {
        error = std::string("Ragdoll field '") + key + "' must be a numeric vec3";
        return false;
    }

    value = glm::vec3(
        source[key][0].get<float>(),
        source[key][1].get<float>(),
        source[key][2].get<float>());
    return true;
}

bool DecodeShape(const RagdollJson& item, RagdollShapeConfig& shape, std::string& error)
{
    if (!item.is_object() || !item.contains("shape_type") || !item["shape_type"].is_string())
    {
        error = "Ragdoll shape requires string shape_type";
        return false;
    }
    shape.shapeType = item["shape_type"].get<std::string>();
    shape.capsuleRadius = item.value("capsule_radius", shape.capsuleRadius);
    shape.capsuleHalfHeight = item.value("capsule_half_height", shape.capsuleHalfHeight);
    shape.sphereRadius = item.value("sphere_radius", shape.sphereRadius);
    if (!ReadVec3(item, "box_extents", shape.boxExtents, shape.boxExtents, error) ||
        !ReadVec3(item, "offset_position", shape.offsetPosition, shape.offsetPosition, error) ||
        !ReadVec3(item, "offset_rotation", shape.offsetRotation, shape.offsetRotation, error))
        return false;
    if (item.contains("convex_vertices"))
    {
        if (!item["convex_vertices"].is_array())
        {
            error = "Ragdoll convex_vertices must be an array";
            return false;
        }
        for (const auto& vertex : item["convex_vertices"])
        {
            glm::vec3 value;
            const RagdollJson wrapped = {{"vertex", vertex}};
            if (!ReadVec3(wrapped, "vertex", {}, value, error)) return false;
            shape.convexVertices.push_back(value);
        }
    }
    const auto finite = [](const glm::vec3& v)
    { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    if (!finite(shape.offsetPosition) || !finite(shape.offsetRotation) ||
        (shape.shapeType != "capsule" && shape.shapeType != "box" &&
         shape.shapeType != "sphere" && shape.shapeType != "convex") ||
        (shape.shapeType == "capsule" && (!std::isfinite(shape.capsuleRadius) ||
          !std::isfinite(shape.capsuleHalfHeight) || shape.capsuleRadius <= 0 ||
          shape.capsuleHalfHeight <= 0)) ||
        (shape.shapeType == "sphere" && (!std::isfinite(shape.sphereRadius) || shape.sphereRadius <= 0)) ||
        (shape.shapeType == "box" && (!finite(shape.boxExtents) ||
          shape.boxExtents.x <= 0 || shape.boxExtents.y <= 0 || shape.boxExtents.z <= 0)) ||
        (shape.shapeType == "convex" && (shape.convexVertices.size() < 4 ||
          shape.convexVertices.size() > 255)) ||
        (shape.shapeType != "convex" && !shape.convexVertices.empty()))
    {
        error = "Ragdoll shape geometry or offset is invalid";
        return false;
    }
    for (const auto& vertex : shape.convexVertices)
        if (!finite(vertex)) { error = "Ragdoll convex vertex must be finite"; return false; }
    return true;
}

RagdollJson EncodeShape(const RagdollShapeConfig& shape)
{
    RagdollJson result = {
        { "shape_type", shape.shapeType },
        { "capsule_radius", shape.capsuleRadius },
        { "capsule_half_height", shape.capsuleHalfHeight },
        { "box_extents", { shape.boxExtents.x, shape.boxExtents.y, shape.boxExtents.z } },
        { "sphere_radius", shape.sphereRadius },
        { "offset_position", { shape.offsetPosition.x, shape.offsetPosition.y, shape.offsetPosition.z } },
        { "offset_rotation", { shape.offsetRotation.x, shape.offsetRotation.y, shape.offsetRotation.z } }
    };
    if (!shape.convexVertices.empty())
    {
        result["convex_vertices"] = RagdollJson::array();
        for (const auto& vertex : shape.convexVertices)
            result["convex_vertices"].push_back({vertex.x, vertex.y, vertex.z});
    }
    return result;
}
}

bool VansRagdollProfileJsonCodec::Decode(
    const RagdollJson& root,
    RagdollProfile& profile,
    std::string& error)
{
    if (!root.is_object())
    {
        error = "Ragdoll profile root must be an object";
        return false;
    }

    if (!root.contains("name") || !root["name"].is_string() ||
        root["name"].get<std::string>().empty() ||
        !root.contains("bodies") || !root["bodies"].is_array() ||
        !root.contains("joints") || !root["joints"].is_array())
    {
        error = "Ragdoll profile requires name, bodies, and joints";
        return false;
    }

    RagdollProfile decoded;
    decoded.name = root["name"].get<std::string>();
    decoded.selfCollision = root.value("self_collision", false);
    std::unordered_set<std::string> bodyBones;

    for (const auto& item : root["bodies"])
    {
        if (!item.is_object() || !item.contains("bone_name") ||
            !item["bone_name"].is_string() || !item.contains("shape_type") ||
            !item["shape_type"].is_string())
        {
            error = "Ragdoll body requires string bone_name and shape_type";
            return false;
        }
        RagdollBodyConfig body;
        body.boneName = item["bone_name"].get<std::string>();
        if (!DecodeShape(item, body, error)) return false;
        body.mass = item.value("mass", body.mass);
        if (item.contains("linear_damping") && !item["linear_damping"].is_number())
        { error = "Ragdoll linear_damping must be numeric"; return false; }
        body.linearDamping = item.value("linear_damping", body.linearDamping);
        if (item.contains("angular_damping"))
        {
            if (!item["angular_damping"].is_number())
            { error = "Ragdoll angular_damping must be numeric"; return false; }
            body.angularDamping = item["angular_damping"].get<float>();
        }
        body.inertiaScale = item.value("inertia_scale", body.inertiaScale);
        body.staticFriction = item.value("static_friction", body.staticFriction);
        body.dynamicFriction = item.value("dynamic_friction", body.dynamicFriction);
        body.restitution = item.value("restitution", body.restitution);
        if ((item.contains("simulate_in_physics_mode") && !item["simulate_in_physics_mode"].is_boolean()) ||
            (item.contains("collision_enabled") && !item["collision_enabled"].is_boolean()))
        { error = "Ragdoll body simulation and collision flags must be boolean"; return false; }
        body.simulateInPhysicsMode = item.value("simulate_in_physics_mode", body.simulateInPhysicsMode);
        body.collisionEnabled = item.value("collision_enabled", body.collisionEnabled);
        if (!ReadVec3(item, "stationary_angular_velocity", body.stationaryAngularVelocity, body.stationaryAngularVelocity, error) ||
            !ReadVec3(item, "stationary_impulse", body.stationaryImpulse, body.stationaryImpulse, error))
            return false;
        if (item.contains("additional_shapes"))
        {
            if (!item["additional_shapes"].is_array())
            { error = "Ragdoll additional_shapes must be an array"; return false; }
            for (const auto& shapeItem : item["additional_shapes"])
            {
                RagdollShapeConfig shape;
                if (!DecodeShape(shapeItem, shape, error)) return false;
                body.additionalShapes.push_back(std::move(shape));
            }
        }
        body.layerName = item.value("layer", body.layerName);
        if (body.boneName.empty() || !bodyBones.insert(body.boneName).second ||
            !std::isfinite(body.mass) || body.mass <= 0.0f ||
            !std::isfinite(body.linearDamping) || body.linearDamping < 0.0f ||
            (body.angularDamping && (!std::isfinite(*body.angularDamping) || *body.angularDamping < 0.0f)) ||
            !std::isfinite(body.inertiaScale) || body.inertiaScale <= 0.0f)
        {
            error = "Ragdoll bodies require unique bones, a canonical shape_type, and positive mass/inertia scale";
            return false;
        }
        decoded.bodies.push_back(std::move(body));
    }

    std::unordered_set<std::string> jointBones;
    for (const auto& item : root["joints"])
    {
        if (!item.is_object() || !item.contains("child_bone_name") ||
            !item["child_bone_name"].is_string())
        {
            error = "Ragdoll joint requires string child_bone_name";
            return false;
        }
        RagdollJointConfig joint;
        joint.childBoneName = item["child_bone_name"].get<std::string>();
        int frames = 0;
        for (const char* key : {"parent_frame_position", "parent_frame_rotation", "child_frame_position", "child_frame_rotation"})
            frames += item.contains(key) ? 1 : 0;
        if (frames != 0 && frames != 4) { error = "Ragdoll joint requires complete local frames"; return false; }
        joint.hasLocalFrames = frames == 4;
        if (!ReadVec3(item,"parent_frame_position",{},joint.parentFramePosition,error) ||
            !ReadVec3(item,"parent_frame_rotation",{},joint.parentFrameRotation,error) ||
            !ReadVec3(item,"child_frame_position",{},joint.childFramePosition,error) ||
            !ReadVec3(item,"child_frame_rotation",{},joint.childFrameRotation,error)) return false;
        if (item.contains("linear_motion"))
        {
            const auto& values = item["linear_motion"];
            if (!values.is_array() || values.size() != 3)
            { error = "Ragdoll linear_motion requires three axis modes"; return false; }
            for (int i=0; i<3; ++i)
                if (!ReadMotion(values[i],joint.linearMotion[i],error)) return false;
        }
        joint.linearLimit = item.value("linear_limit",joint.linearLimit);
        for (const auto& entry : {std::make_pair("swing_y_motion", &joint.swingYMotion),
            std::make_pair("swing_z_motion", &joint.swingZMotion),
            std::make_pair("twist_motion", &joint.twistMotion)})
            if (item.contains(entry.first))
            {
                RagdollJointMotion motion;
                if (!ReadMotion(item[entry.first],motion,error)) return false;
                *entry.second = motion;
            }
        if (!std::isfinite(joint.linearLimit) || joint.linearLimit < 0 ||
            (joint.linearLimit == 0 && std::find(joint.linearMotion.begin(),joint.linearMotion.end(),
                RagdollJointMotion::Limited) != joint.linearMotion.end()))
        { error = "Ragdoll limited linear motion requires a positive linear_limit"; return false; }
        joint.swingYLimit = item.value("swing_y_limit", joint.swingYLimit);
        joint.swingZLimit = item.value("swing_z_limit", joint.swingZLimit);
        joint.twistLowLimit = item.value("twist_low_limit", joint.twistLowLimit);
        joint.twistHighLimit = item.value("twist_high_limit", joint.twistHighLimit);
        joint.limitStiffness = item.value("limit_stiffness", joint.limitStiffness);
        joint.limitDamping = item.value("limit_damping", joint.limitDamping);
        joint.projectionTolerance = item.value("projection_tolerance", joint.projectionTolerance);
        joint.enableDrive = item.value("enable_drive", joint.enableDrive);
        joint.driveStiffness = item.value("drive_stiffness", joint.driveStiffness);
        joint.driveDamping = item.value("drive_damping", joint.driveDamping);
        joint.driveForceLimit = item.value("drive_force_limit", joint.driveForceLimit);
        const auto driveMode = item.value("angular_drive_mode",std::string("slerp"));
        if (driveMode == "slerp") joint.angularDriveMode = RagdollAngularDriveMode::Slerp;
        else if (driveMode == "swing_twist") joint.angularDriveMode = RagdollAngularDriveMode::SwingTwist;
        else { error = "Ragdoll angular_drive_mode must be slerp or swing_twist"; return false; }
        joint.driveAcceleration = item.value("drive_acceleration",false);
        if (!ReadDriveFlags(item,"drive_position_enabled",joint.drivePositionEnabled,error) ||
            !ReadDriveFlags(item,"drive_velocity_enabled",joint.driveVelocityEnabled,error)) return false;
        if (!std::isfinite(joint.swingYLimit) || !std::isfinite(joint.swingZLimit) ||
            joint.swingYLimit < 0 || joint.swingYLimit >= 180 || joint.swingZLimit < 0 || joint.swingZLimit >= 180 ||
            !std::isfinite(joint.twistLowLimit) || !std::isfinite(joint.twistHighLimit) ||
            (joint.twistLowLimit >= joint.twistHighLimit && !(joint.twistLowLimit == 0 && joint.twistHighLimit == 0)) ||
            joint.twistLowLimit <= -180 || joint.twistHighLimit >= 180 ||
            !std::isfinite(joint.limitStiffness) || joint.limitStiffness < 0 || !std::isfinite(joint.limitDamping) || joint.limitDamping < 0)
        { error = "Ragdoll joint angular limits or springs are invalid"; return false; }
        if (joint.childBoneName.empty() || !jointBones.insert(joint.childBoneName).second ||
            bodyBones.find(joint.childBoneName) == bodyBones.end())
        {
            error = "Ragdoll joints require a unique child_bone_name resolving to a body";
            return false;
        }
        decoded.joints.push_back(std::move(joint));
    }

    if (decoded.bodies.empty())
    {
        error = "Ragdoll profile '" + decoded.name + "' has no valid body";
        return false;
    }

    if (root.contains("disabled_collision_pairs"))
    {
        const auto& pairs = root["disabled_collision_pairs"];
        if (!pairs.is_array()) { error = "Ragdoll disabled_collision_pairs must be an array"; return false; }
        std::set<std::array<std::string,2>> uniquePairs;
        for (const auto& pair : pairs)
        {
            if (!pair.is_array() || pair.size()!=2 || !pair[0].is_string() || !pair[1].is_string())
            { error = "Ragdoll disabled collision pair requires two body names"; return false; }
            std::array<std::string,2> names{pair[0].get<std::string>(),pair[1].get<std::string>()};
            if (names[0]==names[1] || !bodyBones.count(names[0]) || !bodyBones.count(names[1]))
            { error = "Ragdoll disabled collision pair must resolve two distinct bodies"; return false; }
            std::sort(names.begin(),names.end());
            if (!uniquePairs.insert(names).second)
            { error = "Ragdoll disabled collision pair is duplicated"; return false; }
            decoded.disabledCollisionPairs.push_back(std::move(names));
        }
    }
    profile = std::move(decoded);
    error.clear();
    return true;
}

bool VansRagdollProfileJsonCodec::Encode(
    const RagdollProfile& profile,
    RagdollJson& root,
    std::string& error)
{
    root = {
        { "name", profile.name },
        { "self_collision", profile.selfCollision },
        { "bodies", RagdollJson::array() },
        { "joints", RagdollJson::array() }
    };
    if (!profile.disabledCollisionPairs.empty())
        root["disabled_collision_pairs"] = profile.disabledCollisionPairs;
    for (const RagdollBodyConfig& body : profile.bodies)
    {
        RagdollJson encoded = EncodeShape(body);
        encoded.update(RagdollJson{
            { "bone_name", body.boneName },
            { "mass", body.mass },
            { "linear_damping", body.linearDamping },
            { "inertia_scale", body.inertiaScale },
            { "static_friction", body.staticFriction },
            { "dynamic_friction", body.dynamicFriction },
            { "restitution", body.restitution },
            { "layer", body.layerName },
            { "stationary_angular_velocity", { body.stationaryAngularVelocity.x, body.stationaryAngularVelocity.y, body.stationaryAngularVelocity.z } },
            { "stationary_impulse", { body.stationaryImpulse.x, body.stationaryImpulse.y, body.stationaryImpulse.z } }
        });
        if (body.angularDamping) encoded["angular_damping"] = *body.angularDamping;
        if (!body.simulateInPhysicsMode) encoded["simulate_in_physics_mode"] = false;
        if (!body.collisionEnabled) encoded["collision_enabled"] = false;
        if (!body.additionalShapes.empty())
        {
            encoded["additional_shapes"] = RagdollJson::array();
            for (const auto& shape : body.additionalShapes)
                encoded["additional_shapes"].push_back(EncodeShape(shape));
        }
        root["bodies"].push_back(std::move(encoded));
    }
    for (const RagdollJointConfig& joint : profile.joints)
    {
        root["joints"].push_back({
            { "child_bone_name", joint.childBoneName },
            { "swing_y_limit", joint.swingYLimit },
            { "swing_z_limit", joint.swingZLimit },
            { "twist_low_limit", joint.twistLowLimit },
            { "twist_high_limit", joint.twistHighLimit },
            { "limit_stiffness", joint.limitStiffness },
            { "limit_damping", joint.limitDamping },
            { "projection_tolerance", joint.projectionTolerance },
            { "enable_drive", joint.enableDrive },
            { "drive_stiffness", joint.driveStiffness },
            { "drive_damping", joint.driveDamping },
            { "drive_force_limit", joint.driveForceLimit }
        });
        auto& encoded = root["joints"].back();
        if (joint.linearMotion != std::array<RagdollJointMotion,3>{RagdollJointMotion::Locked,
            RagdollJointMotion::Locked,RagdollJointMotion::Locked})
            encoded["linear_motion"] = {MotionName(joint.linearMotion[0]),
                MotionName(joint.linearMotion[1]),MotionName(joint.linearMotion[2])};
        if (joint.linearLimit != 0) encoded["linear_limit"] = joint.linearLimit;
        if (joint.swingYMotion) encoded["swing_y_motion"] = MotionName(*joint.swingYMotion);
        if (joint.swingZMotion) encoded["swing_z_motion"] = MotionName(*joint.swingZMotion);
        if (joint.twistMotion) encoded["twist_motion"] = MotionName(*joint.twistMotion);
        if (joint.angularDriveMode == RagdollAngularDriveMode::SwingTwist)
            encoded["angular_drive_mode"] = "swing_twist";
        if (joint.driveAcceleration) encoded["drive_acceleration"] = true;
        if (joint.drivePositionEnabled != std::array<bool,3>{true,true,true})
            encoded["drive_position_enabled"] = joint.drivePositionEnabled;
        if (joint.driveVelocityEnabled != std::array<bool,3>{true,true,true})
            encoded["drive_velocity_enabled"] = joint.driveVelocityEnabled;
        if (joint.hasLocalFrames)
        {
            auto& item = root["joints"].back();
            const auto vec = [](const glm::vec3& v) { return RagdollJson::array({v.x,v.y,v.z}); };
            item["parent_frame_position"] = vec(joint.parentFramePosition);
            item["parent_frame_rotation"] = vec(joint.parentFrameRotation);
            item["child_frame_position"] = vec(joint.childFramePosition);
            item["child_frame_rotation"] = vec(joint.childFrameRotation);
        }
    }
    RagdollProfile verified;
    return Decode(root, verified, error);
}
}
