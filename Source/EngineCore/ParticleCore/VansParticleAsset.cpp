#include "VansParticleAsset.h"
#include <fstream>
#include <stdexcept>

namespace VansGraphics
{
    bool VansParticleAsset::LoadFromFile(const std::string& filePath)
    {
        std::ifstream file(filePath);
        if (!file.is_open())
            return false;

        try
        {
            nlohmann::json j;
            file >> j;
            Deserialize(j);
            m_FilePath = filePath;
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool VansParticleAsset::SaveToFile(const std::string& filePath) const
    {
        std::ofstream file(filePath);
        if (!file.is_open())
            return false;

        try
        {
            file << Serialize().dump(4);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    nlohmann::json VansParticleAsset::Serialize() const
    {
        nlohmann::json j;
        j["version"]     = m_Version;
        j["name"]        = m_Name;

        nlohmann::json global;
        global["duration"]         = m_Duration;
        global["loop"]             = m_Loop;
        global["prewarm"]          = m_Prewarm;
        global["simulationSpace"]  = m_SimSpace;
        j["global"] = global;

        auto emitters = nlohmann::json::array();
        for (auto& e : m_Emitters)
            if (e) emitters.push_back(e->Serialize());
        j["emitters"] = emitters;

        return j;
    }

    void VansParticleAsset::Deserialize(const nlohmann::json& j)
    {
        m_Version = j.value("version", 1);
        m_Name    = j.value("name",    "");

        if (j.contains("global"))
        {
            auto& g   = j["global"];
            m_Duration = g.value("duration",        5.f);
            m_Loop     = g.value("loop",             true);
            m_Prewarm  = g.value("prewarm",          false);
            m_SimSpace = g.value("simulationSpace",  std::string("Local"));
        }

        m_Emitters.clear();
        if (j.contains("emitters") && j["emitters"].is_array())
        {
            for (auto& eJson : j["emitters"])
            {
                auto emitter = std::make_unique<VansParticleEmitter>();
                emitter->Deserialize(eJson);
                m_Emitters.push_back(std::move(emitter));
            }
        }
    }

} // namespace VansGraphics
