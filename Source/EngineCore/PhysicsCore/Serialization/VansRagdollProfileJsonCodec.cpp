#include "VansRagdollProfileJsonCodec.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <unordered_set>
#include <utility>

namespace VansEngine
{
namespace
{
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
        body.shapeType = item["shape_type"].get<std::string>();
        body.capsuleRadius = item.value("capsule_radius", body.capsuleRadius);
        body.capsuleHalfHeight = item.value("capsule_half_height", body.capsuleHalfHeight);
        if (!ReadVec3(item, "box_extents", body.boxExtents, body.boxExtents, error)) return false;
        body.sphereRadius = item.value("sphere_radius", body.sphereRadius);
        body.mass = item.value("mass", body.mass);
        body.inertiaScale = item.value("inertia_scale", body.inertiaScale);
        body.staticFriction = item.value("static_friction", body.staticFriction);
        body.dynamicFriction = item.value("dynamic_friction", body.dynamicFriction);
        body.restitution = item.value("restitution", body.restitution);
        if (!ReadVec3(item, "offset_position", body.offsetPosition, body.offsetPosition, error) ||
            !ReadVec3(item, "offset_rotation", body.offsetRotation, body.offsetRotation, error) ||
            !ReadVec3(item, "stationary_angular_velocity", body.stationaryAngularVelocity, body.stationaryAngularVelocity, error) ||
            !ReadVec3(item, "stationary_impulse", body.stationaryImpulse, body.stationaryImpulse, error))
            return false;
        body.layerName = item.value("layer", body.layerName);
        if (body.boneName.empty() || !bodyBones.insert(body.boneName).second ||
            (body.shapeType != "capsule" && body.shapeType != "box" && body.shapeType != "sphere") ||
            !std::isfinite(body.mass) || body.mass <= 0.0f ||
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
    for (const RagdollBodyConfig& body : profile.bodies)
    {
        root["bodies"].push_back({
            { "bone_name", body.boneName },
            { "shape_type", body.shapeType },
            { "capsule_radius", body.capsuleRadius },
            { "capsule_half_height", body.capsuleHalfHeight },
            { "box_extents", { body.boxExtents.x, body.boxExtents.y, body.boxExtents.z } },
            { "sphere_radius", body.sphereRadius },
            { "mass", body.mass },
            { "inertia_scale", body.inertiaScale },
            { "static_friction", body.staticFriction },
            { "dynamic_friction", body.dynamicFriction },
            { "restitution", body.restitution },
            { "offset_position", { body.offsetPosition.x, body.offsetPosition.y, body.offsetPosition.z } },
            { "offset_rotation", { body.offsetRotation.x, body.offsetRotation.y, body.offsetRotation.z } },
            { "layer", body.layerName },
            { "stationary_angular_velocity", { body.stationaryAngularVelocity.x, body.stationaryAngularVelocity.y, body.stationaryAngularVelocity.z } },
            { "stationary_impulse", { body.stationaryImpulse.x, body.stationaryImpulse.y, body.stationaryImpulse.z } }
        });
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
