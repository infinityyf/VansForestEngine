#include "VansRagdollProfileLegacyJsonCodec.h"

#include "../../Util/VansLog.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace VansEngine
{
namespace
{
glm::vec3 ReadVec3(const RagdollJson& source, const char* key, const glm::vec3& defaultValue)
{
    if (!source.contains(key) || !source[key].is_array() || source[key].size() < 3)
        return defaultValue;

    return glm::vec3(
        source[key][0].get<float>(),
        source[key][1].get<float>(),
        source[key][2].get<float>());
}
}

bool VansRagdollProfileLegacyJsonCodec::Decode(
    const RagdollJson& root,
    RagdollProfile& profile,
    std::string& error)
{
    if (!root.is_object())
    {
        error = "Ragdoll profile root must be an object";
        return false;
    }

    RagdollProfile decoded;
    decoded.name = root.value("name", "RagdollProfile");

    if (root.contains("bodies") && root["bodies"].is_array())
    {
        for (const auto& item : root["bodies"])
        {
            RagdollBodyConfig body;
            body.boneName = item.value("bone_name", item.value("boneName", ""));
            body.shapeType = item.value("shape_type", item.value("shapeType", "capsule"));
            body.capsuleRadius = item.value("capsule_radius", item.value("capsuleRadius", body.capsuleRadius));
            body.capsuleHalfHeight = item.value("capsule_half_height", item.value("capsuleHalfHeight", body.capsuleHalfHeight));
            body.boxExtents = ReadVec3(item, "box_extents", ReadVec3(item, "boxExtents", body.boxExtents));
            body.sphereRadius = item.value("sphere_radius", item.value("sphereRadius", body.sphereRadius));
            body.mass = item.value("mass", body.mass);
            body.staticFriction = item.value("static_friction", item.value("staticFriction", body.staticFriction));
            body.dynamicFriction = item.value("dynamic_friction", item.value("dynamicFriction", body.dynamicFriction));
            body.restitution = item.value("restitution", body.restitution);
            body.offsetPosition = ReadVec3(item, "offset_position", ReadVec3(item, "offsetPosition", body.offsetPosition));
            body.offsetRotation = ReadVec3(item, "offset_rotation", ReadVec3(item, "offsetRotation", body.offsetRotation));
            body.layerName = item.value("layer", item.value("layerName", body.layerName));

            if (body.boneName.empty())
            {
                VANS_LOG_WARN("[RagdollProfile] Skipped body without bone_name");
                continue;
            }
            decoded.bodies.push_back(std::move(body));
        }
    }

    if (root.contains("joints") && root["joints"].is_array())
    {
        for (const auto& item : root["joints"])
        {
            RagdollJointConfig joint;
            joint.childBoneName = item.value("child_bone_name", item.value("child_bone", item.value("childBoneName", "")));
            joint.swingYLimit = item.value("swing_y_limit", item.value("swingYLimit", joint.swingYLimit));
            joint.swingZLimit = item.value("swing_z_limit", item.value("swingZLimit", joint.swingZLimit));
            joint.twistLowLimit = item.value("twist_low_limit", item.value("twistLowLimit", joint.twistLowLimit));
            joint.twistHighLimit = item.value("twist_high_limit", item.value("twistHighLimit", joint.twistHighLimit));
            joint.limitStiffness = item.value("limit_stiffness", item.value("limitStiffness", joint.limitStiffness));
            joint.limitDamping = item.value("limit_damping", item.value("limitDamping", joint.limitDamping));
            joint.projectionTolerance = item.value("projection_tolerance", item.value("projectionTolerance", joint.projectionTolerance));
            joint.enableDrive = item.value("enable_drive", item.value("enableDrive", joint.enableDrive));
            joint.driveStiffness = item.value("drive_stiffness", item.value("driveStiffness", joint.driveStiffness));
            joint.driveDamping = item.value("drive_damping", item.value("driveDamping", joint.driveDamping));
            joint.driveForceLimit = item.value("drive_force_limit", item.value("driveForceLimit", joint.driveForceLimit));

            if (joint.childBoneName.empty())
            {
                VANS_LOG_WARN("[RagdollProfile] Skipped joint without child_bone");
                continue;
            }
            decoded.joints.push_back(std::move(joint));
        }
    }

    if (decoded.bodies.empty())
    {
        error = "Ragdoll profile '" + decoded.name + "' has no valid body";
        return false;
    }

    profile = std::move(decoded);
    return true;
}
}
