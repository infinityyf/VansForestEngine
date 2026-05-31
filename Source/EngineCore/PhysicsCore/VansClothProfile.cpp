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
    // 序列化
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

        json pinnedArr = json::array();
        for (const glm::vec3& pos : m_PinnedLocalPositions)
        {
            json entry;
            entry["localPosition"] = { pos.x, pos.y, pos.z };
            pinnedArr.push_back(entry);
        }
        j["pinnedVertices"] = pinnedArr;

        // 确保父目录存在
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
    // 反序列化
    // =========================================================================

    bool VansClothProfile::LoadFromFile(const std::string& filePath)
    {
        std::ifstream in(filePath);
        if (!in.is_open())
        {
            VANS_LOG_ERROR("[VansClothProfile] LoadFromFile: 无法打开文件 " << filePath);
            return false;
        }

        json j;
        try
        {
            j = json::parse(in);
        }
        catch (const json::exception& e)
        {
            VANS_LOG_ERROR("[VansClothProfile] LoadFromFile: JSON 解析失败 " << e.what());
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
            // gravity 兼容标量和三元素数组两种格式：
            // 标量 -9.81 → 直接读取；数组 [0,-9.8,0] → 取 Y 分量（索引 1）
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

        m_PinnedLocalPositions.clear();
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
            }
        }

        VANS_LOG("[VansClothProfile] 已加载: " << filePath
                 << "，固定点数=" << m_PinnedLocalPositions.size());
        return true;
    }

    // =========================================================================
    // 重置为默认值
    // =========================================================================

    void VansClothProfile::ResetToDefaults()
    {
        *this = VansClothProfile{};
    }

    // =========================================================================
    // 运行时辅助：局部坐标 → 原始顶点索引匹配
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
                // rawPosFloat4 实际布局：每个顶点 8 个 float（pos xyz pad + normal xyz pad）
                // 与 VansClothNode.cpp 中的 rawPos[v * 8 + 0] 保持一致
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
                VANS_LOG_WARN("[VansClothProfile] ResolveIndices: 固定点 ("
                              << pinnedPos.x << ", " << pinnedPos.y << ", " << pinnedPos.z
                              << ") 使用近邻容错匹配，顶点=" << bestIdx
                              << " 距离=" << bestDist
                              << " 配置容差=" << m_PinnedMatchTolerance
                              << " 容错上限=" << fallbackTolerance);
            }
            else
            {
                VANS_LOG_WARN("[VansClothProfile] ResolveIndices: 固定点 ("
                              << pinnedPos.x << ", " << pinnedPos.y << ", " << pinnedPos.z
                              << ") 未找到匹配顶点（容差=" << m_PinnedMatchTolerance
                              << "，最近距离=" << bestDist << "）");
            }
        }

        return result;
    }
}
