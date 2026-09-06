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
        body.staticFriction = item.value("static_friction", body.staticFriction);
        body.dynamicFriction = item.value("dynamic_friction", body.dynamicFriction);
        body.restitution = item.value("restitution", body.restitution);
        if (!ReadVec3(item, "offset_position", body.offsetPosition, body.offsetPosition, error) ||
            !ReadVec3(item, "offset_rotation", body.offsetRotation, body.offsetRotation, error))
            return false;
        body.layerName = item.value("layer", body.layerName);
        if (body.boneName.empty() || !bodyBones.insert(body.boneName).second ||
            (body.shapeType != "capsule" && body.shapeType != "box" && body.shapeType != "sphere") ||
            !std::isfinite(body.mass) || body.mass <= 0.0f)
        {
            error = "Ragdoll bodies require unique bones, a canonical shape_type, and positive mass";
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
            { "static_friction", body.staticFriction },
            { "dynamic_friction", body.dynamicFriction },
            { "restitution", body.restitution },
            { "offset_position", { body.offsetPosition.x, body.offsetPosition.y, body.offsetPosition.z } },
            { "offset_rotation", { body.offsetRotation.x, body.offsetRotation.y, body.offsetRotation.z } },
            { "layer", body.layerName }
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
    }
    RagdollProfile verified;
    return Decode(root, verified, error);
}
}
