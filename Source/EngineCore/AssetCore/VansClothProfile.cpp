
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "VansClothProfile.h"
#include "../Util/VansLog.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <limits>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace VansEngine
{
    // =========================================================================
    // 序列化保存
    // =========================================================================

    bool VansClothProfile::SaveToFile(const std::string& filePath) const
    {
        json j;
        j["version"]     = PROFILE_VERSION;
        j["name"]        = m_Name;
        j["description"] = m_Description;
        j["modelPath"]   = m_ModelPath;

        j["simulation"]["stiffness"]     = m_Stiffness;
        j["simulation"]["damping"]       = m_Damping;
        j["simulation"]["friction"]      = m_Friction;
        j["simulation"]["gravity"]       = m_Gravity;
        j["simulation"]["selfCollision"] = m_SelfCollision;
        j["simulation"]["weldTolerance"] = m_WeldTolerance;

        j["pinnedMatchTolerance"] = m_PinnedMatchTolerance;


        j["followBones"]           = m_FollowBones;
        j["referenceSkeletonPath"] = m_ReferenceSkeletonPath;
        j["skeletonOffset"]["position"] = { m_SkeletonOffset.m_Position.x, m_SkeletonOffset.m_Position.y, m_SkeletonOffset.m_Position.z };
        j["skeletonOffset"]["rotation"] = { m_SkeletonOffset.m_Rotation.x, m_SkeletonOffset.m_Rotation.y, m_SkeletonOffset.m_Rotation.z };
        j["skeletonOffset"]["scale"]    = { m_SkeletonOffset.m_Scale.x,    m_SkeletonOffset.m_Scale.y,    m_SkeletonOffset.m_Scale.z    };


        json pinnedArr = json::array();
        for (size_t i = 0; i < m_PinnedLocalPositions.size(); ++i)
        {
            const glm::vec3& pos = m_PinnedLocalPositions[i];
            json entry;
            entry["localPosition"] = { pos.x, pos.y, pos.z };

            if (i < m_PinnedBoneBindings.size())
            {
                const auto& bd = m_PinnedBoneBindings[i];
                if (!bd.m_BoneNames.empty())
                {
                    json bindArr = json::array();
                    for (size_t b = 0; b < bd.m_BoneNames.size(); ++b)
                    {
                        float w = (b < bd.m_Weights.size()) ? bd.m_Weights[b] : 0.0f;
                        bindArr.push_back({ {"boneName", bd.m_BoneNames[b]}, {"weight", w} });
                    }
                    entry["boneBindings"] = bindArr;
                }
            }
            pinnedArr.push_back(entry);
        }
        j["pinnedVertices"] = pinnedArr;


        fs::path outPath(filePath);
        if (outPath.has_parent_path())
            fs::create_directories(outPath.parent_path());

        std::ofstream out(filePath);
        if (!out.is_open())
        {
            VANS_LOG_ERROR("[VansClothProfile] SaveToFile: 无法写入文件 " << filePath);
            return false;
        }
        out << j.dump(4);
        return true;
    }

    // =========================================================================
    // 反序列化加载
    // =========================================================================

    bool VansClothProfile::LoadFromFile(const std::string& filePath)
    {
        std::ifstream in(filePath);
        if (!in.is_open())
        {
            VANS_LOG_ERROR("[VansClothProfile] LoadFromFile: ?????? " << filePath);
            return false;
        }

        json j;
        try
        {
            j = json::parse(in);
        }
        catch (const json::exception& e)
        {
            VANS_LOG_ERROR("[VansClothProfile] LoadFromFile: JSON 瑙ｆ瀽澶辫触 " << e.what());
            return false;
        }

        m_Name        = j.value("name",        "");
        m_Description = j.value("description", "");
        m_ModelPath   = j.value("modelPath",    "");

        if (j.contains("simulation"))
        {
            const auto& sim = j["simulation"];
            m_Stiffness     = sim.value("stiffness",     0.8f);
            m_Damping       = sim.value("damping",       0.1f);
            m_Friction      = sim.value("friction",      0.0f);


            if (sim.contains("gravity"))
            {
                const auto& grav = sim["gravity"];
                if (grav.is_array() && grav.size() >= 2)
                    m_Gravity = grav[1].get<float>();
                else if (grav.is_number())
                    m_Gravity = grav.get<float>();
            }
            m_SelfCollision = sim.value("selfCollision", false);
            m_WeldTolerance = sim.value("weldTolerance", 1e-5f);
        }

        m_PinnedMatchTolerance = j.value("pinnedMatchTolerance", 0.01f);


        m_FollowBones           = j.value("followBones", false);
        m_ReferenceSkeletonPath = j.value("referenceSkeletonPath", "");

        if (j.contains("skeletonOffset"))
        {
            const auto& off = j["skeletonOffset"];
            if (off.contains("position") && off["position"].is_array() && off["position"].size() >= 3)
            {
                m_SkeletonOffset.m_Position.x = off["position"][0].get<float>();
                m_SkeletonOffset.m_Position.y = off["position"][1].get<float>();
                m_SkeletonOffset.m_Position.z = off["position"][2].get<float>();
            }
            if (off.contains("rotation") && off["rotation"].is_array() && off["rotation"].size() >= 3)
            {
                m_SkeletonOffset.m_Rotation.x = off["rotation"][0].get<float>();
                m_SkeletonOffset.m_Rotation.y = off["rotation"][1].get<float>();
                m_SkeletonOffset.m_Rotation.z = off["rotation"][2].get<float>();
            }
            if (off.contains("scale") && off["scale"].is_array() && off["scale"].size() >= 3)
            {
                m_SkeletonOffset.m_Scale.x = off["scale"][0].get<float>();
                m_SkeletonOffset.m_Scale.y = off["scale"][1].get<float>();
                m_SkeletonOffset.m_Scale.z = off["scale"][2].get<float>();
            }
        }

        m_PinnedLocalPositions.clear();
        m_PinnedBoneBindings.clear();
        if (j.contains("pinnedVertices") && j["pinnedVertices"].is_array())
        {
            for (const auto& entry : j["pinnedVertices"])
            {
                if (entry.contains("localPosition") && entry["localPosition"].is_array()
                    && entry["localPosition"].size() >= 3)
                {
                    glm::vec3 pos;
                    pos.x = entry["localPosition"][0].get<float>();
                    pos.y = entry["localPosition"][1].get<float>();
                    pos.z = entry["localPosition"][2].get<float>();
                    m_PinnedLocalPositions.push_back(pos);
                }


                PinBoneBinding binding;
                if (entry.contains("boneBindings") && entry["boneBindings"].is_array())
                {
                    for (const auto& bd : entry["boneBindings"])
                    {
                        std::string bn = bd.value("boneName", "");
                        float       w  = bd.value("weight", 0.0f);
                        if (!bn.empty())
                        {
                            binding.m_BoneNames.push_back(bn);
                            binding.m_Weights.push_back(w);
                        }
                    }
                }
                m_PinnedBoneBindings.push_back(binding);
            }
        }

        VANS_LOG("[VansClothProfile] Loaded: " << filePath
                 << ", pinnedCount=" << m_PinnedLocalPositions.size()
                 << ", followBones=" << (m_FollowBones ? "enabled" : "disabled"));
        return true;
    }

    // =========================================================================

    // =========================================================================

    void VansClothProfile::ResetToDefaults()
    {
        *this = VansClothProfile{};
    }

    // =========================================================================

    // =========================================================================

    std::vector<uint32_t> VansClothProfile::ResolveIndices(
        const std::vector<float>& rawPosFloat4,
        int vertexCount) const
    {
        std::vector<uint32_t> result;
        result.reserve(m_PinnedLocalPositions.size());
        const float fallbackTolerance = std::max(m_PinnedMatchTolerance, 0.5f);

        for (const glm::vec3& pinnedPos : m_PinnedLocalPositions)
        {
            float    bestDist = std::numeric_limits<float>::max();
            int      bestIdx  = -1;

            for (int v = 0; v < vertexCount; ++v)
            {


                float vx = rawPosFloat4[v * 8 + 0];
                float vy = rawPosFloat4[v * 8 + 1];
                float vz = rawPosFloat4[v * 8 + 2];

                float dx   = vx - pinnedPos.x;
                float dy   = vy - pinnedPos.y;
                float dz   = vz - pinnedPos.z;
                float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestIdx  = v;
                }
            }

            if (bestIdx >= 0 && bestDist <= m_PinnedMatchTolerance)
            {
                result.push_back(static_cast<uint32_t>(bestIdx));
            }
            else if (bestIdx >= 0 && bestDist <= fallbackTolerance)
            {
                result.push_back(static_cast<uint32_t>(bestIdx));
                VANS_LOG_WARN("[VansClothProfile] ResolveIndices: ????????????("
                              << pinnedPos.x << ", " << pinnedPos.y << ", " << pinnedPos.z
                              << ") ??????? " << bestIdx
                              << "???=" << bestDist
                              << "?????=" << m_PinnedMatchTolerance
                              << "?????=" << fallbackTolerance);
            }
            else
            {
                VANS_LOG_WARN("[VansClothProfile] ResolveIndices: pinned point ("
                              << pinnedPos.x << ", " << pinnedPos.y << ", " << pinnedPos.z
                              << ") has no matching vertex. tolerance=" << m_PinnedMatchTolerance
                              << ", bestDistance=" << bestDist);
            }
        }

        return result;
    }

    glm::mat4 VansClothProfile::GetSkeletonOffsetMatrix() const
    {
        glm::mat4 T = glm::translate(glm::mat4(1.0f), m_SkeletonOffset.m_Position);
        glm::mat4 R = glm::mat4(1.0f);
        R = glm::rotate(R, glm::radians(m_SkeletonOffset.m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        R = glm::rotate(R, glm::radians(m_SkeletonOffset.m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        R = glm::rotate(R, glm::radians(m_SkeletonOffset.m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        glm::mat4 S = glm::scale(glm::mat4(1.0f), m_SkeletonOffset.m_Scale);
        return T * R * S;
    }
}

