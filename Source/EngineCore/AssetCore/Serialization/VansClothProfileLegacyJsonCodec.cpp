#include "VansClothProfileLegacyJsonCodec.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <exception>
#include <utility>

namespace VansEngine
{
ClothProfileJson VansClothProfileLegacyJsonCodec::Encode(const VansClothProfile& profile)
{
    const VansClothProfile& p = profile;
    ClothProfileJson root;
    root["version"] = VansClothProfile::PROFILE_VERSION;
    root["name"] = p.m_Name;
    root["description"] = p.m_Description;
    root["modelPath"] = p.m_ModelPath;

    root["simulation"]["stiffness"] = p.m_Stiffness;
    root["simulation"]["damping"] = p.m_Damping;
    root["simulation"]["friction"] = p.m_Friction;
    root["simulation"]["gravity"] = p.m_Gravity;
    root["simulation"]["selfCollision"] = p.m_SelfCollision;
    root["simulation"]["weldTolerance"] = p.m_WeldTolerance;

    root["pinnedMatchTolerance"] = p.m_PinnedMatchTolerance;
    root["followBones"] = p.m_FollowBones;
    root["referenceSkeletonPath"] = p.m_ReferenceSkeletonPath;
    root["skeletonOffset"]["position"] = {
        p.m_SkeletonOffset.m_Position.x,
        p.m_SkeletonOffset.m_Position.y,
        p.m_SkeletonOffset.m_Position.z
    };
    root["skeletonOffset"]["rotation"] = {
        p.m_SkeletonOffset.m_Rotation.x,
        p.m_SkeletonOffset.m_Rotation.y,
        p.m_SkeletonOffset.m_Rotation.z
    };
    root["skeletonOffset"]["scale"] = {
        p.m_SkeletonOffset.m_Scale.x,
        p.m_SkeletonOffset.m_Scale.y,
        p.m_SkeletonOffset.m_Scale.z
    };

    ClothProfileJson pinnedVertices = ClothProfileJson::array();
    for (std::size_t index = 0; index < p.m_PinnedLocalPositions.size(); ++index)
    {
        const glm::vec3& position = p.m_PinnedLocalPositions[index];
        ClothProfileJson entry;
        entry["localPosition"] = { position.x, position.y, position.z };

        if (index < p.m_PinnedBoneBindings.size())
        {
            const auto& binding = p.m_PinnedBoneBindings[index];
            if (!binding.m_BoneNames.empty())
            {
                ClothProfileJson bindings = ClothProfileJson::array();
                for (std::size_t boneIndex = 0; boneIndex < binding.m_BoneNames.size(); ++boneIndex)
                {
                    const float weight = boneIndex < binding.m_Weights.size()
                        ? binding.m_Weights[boneIndex]
                        : 0.0f;
                    bindings.push_back({
                        { "boneName", binding.m_BoneNames[boneIndex] },
                        { "weight", weight }
                    });
                }
                entry["boneBindings"] = std::move(bindings);
            }
        }
        pinnedVertices.push_back(std::move(entry));
    }
    root["pinnedVertices"] = std::move(pinnedVertices);
    return root;
}

bool VansClothProfileLegacyJsonCodec::Decode(
    const ClothProfileJson& root,
    const std::filesystem::path& filePath,
    VansClothProfile& profile,
    std::string& error)
{
    try
    {
        VansClothProfile decoded;
        decoded.m_Name = root.value("name", "");
        decoded.m_Description = root.value("description", "");
        decoded.m_ModelPath = root.value("modelPath", "");

        if (root.contains("simulation"))
        {
            const auto& simulation = root["simulation"];
            decoded.m_Stiffness = simulation.value("stiffness", 0.8f);
            decoded.m_Damping = simulation.value("damping", 0.1f);
            decoded.m_Friction = simulation.value("friction", 0.0f);

            if (simulation.contains("gravity"))
            {
                const auto& gravity = simulation["gravity"];
                if (gravity.is_array() && gravity.size() >= 2)
                    decoded.m_Gravity = gravity[1].get<float>();
                else if (gravity.is_number())
                    decoded.m_Gravity = gravity.get<float>();
            }
            decoded.m_SelfCollision = simulation.value("selfCollision", false);
            decoded.m_WeldTolerance = simulation.value("weldTolerance", 1e-5f);
        }

        decoded.m_PinnedMatchTolerance = root.value("pinnedMatchTolerance", 0.01f);
        decoded.m_FollowBones = root.value("followBones", false);
        decoded.m_ReferenceSkeletonPath = root.value("referenceSkeletonPath", "");

        if (root.contains("skeletonOffset"))
        {
            const auto& offset = root["skeletonOffset"];
            if (offset.contains("position") && offset["position"].is_array() && offset["position"].size() >= 3)
            {
                decoded.m_SkeletonOffset.m_Position.x = offset["position"][0].get<float>();
                decoded.m_SkeletonOffset.m_Position.y = offset["position"][1].get<float>();
                decoded.m_SkeletonOffset.m_Position.z = offset["position"][2].get<float>();
            }
            if (offset.contains("rotation") && offset["rotation"].is_array() && offset["rotation"].size() >= 3)
            {
                decoded.m_SkeletonOffset.m_Rotation.x = offset["rotation"][0].get<float>();
                decoded.m_SkeletonOffset.m_Rotation.y = offset["rotation"][1].get<float>();
                decoded.m_SkeletonOffset.m_Rotation.z = offset["rotation"][2].get<float>();
            }
            if (offset.contains("scale") && offset["scale"].is_array() && offset["scale"].size() >= 3)
            {
                decoded.m_SkeletonOffset.m_Scale.x = offset["scale"][0].get<float>();
                decoded.m_SkeletonOffset.m_Scale.y = offset["scale"][1].get<float>();
                decoded.m_SkeletonOffset.m_Scale.z = offset["scale"][2].get<float>();
            }
        }

        if (root.contains("pinnedVertices") && root["pinnedVertices"].is_array())
        {
            for (const auto& entry : root["pinnedVertices"])
            {
                if (entry.contains("localPosition") &&
                    entry["localPosition"].is_array() &&
                    entry["localPosition"].size() >= 3)
                {
                    glm::vec3 position;
                    position.x = entry["localPosition"][0].get<float>();
                    position.y = entry["localPosition"][1].get<float>();
                    position.z = entry["localPosition"][2].get<float>();
                    decoded.m_PinnedLocalPositions.push_back(position);
                }

                VansClothProfile::PinBoneBinding binding;
                if (entry.contains("boneBindings") && entry["boneBindings"].is_array())
                {
                    for (const auto& bindingJson : entry["boneBindings"])
                    {
                        std::string boneName = bindingJson.value("boneName", "");
                        const float weight = bindingJson.value("weight", 0.0f);
                        if (!boneName.empty())
                        {
                            binding.m_BoneNames.push_back(std::move(boneName));
                            binding.m_Weights.push_back(weight);
                        }
                    }
                }
                decoded.m_PinnedBoneBindings.push_back(std::move(binding));
            }
        }

        profile = std::move(decoded);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = "Invalid cloth profile JSON " + filePath.string() + ": " + exception.what();
        return false;
    }
}
}
