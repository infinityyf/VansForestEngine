#include "VansParticleModule.h"
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
    // ──────────────────────────────────────────────────────────────────────
    // VansFloatCurve::Evaluate
    // ──────────────────────────────────────────────────────────────────────

    // 从 Keys 中根据 t 线性插值（内部辅助函数）
    static float EvalKeys(const std::vector<CurveKey>& keys, float t)
    {
        if (keys.empty()) return 0.f;
        if (keys.size() == 1) return keys[0].value;
        if (t <= keys.front().t) return keys.front().value;
        if (t >= keys.back().t)  return keys.back().value;

        for (size_t i = 0; i + 1 < keys.size(); ++i)
        {
            const CurveKey& a = keys[i];
            const CurveKey& b = keys[i + 1];
            if (t >= a.t && t <= b.t)
            {
                float span = b.t - a.t;
                if (span < 1e-6f) return a.value;
                float alpha = (t - a.t) / span;
                return a.value + (b.value - a.value) * alpha;
            }
        }
        return keys.back().value;
    }

    float VansFloatCurve::Evaluate(float normalizedT, float r) const
    {
        switch (m_Mode)
        {
        case FloatCurveMode::Constant:
            return m_Value;
        case FloatCurveMode::RandomBetween:
            return m_Min + r * (m_Max - m_Min);
        case FloatCurveMode::Curve:
            return EvalKeys(m_Keys, normalizedT);
        case FloatCurveMode::RandomBetweenCurves:
        {
            float lo = EvalKeys(m_MinKeys, normalizedT);
            float hi = EvalKeys(m_MaxKeys, normalizedT);
            return lo + r * (hi - lo);
        }
        default:
            return m_Value;
        }
    }

    nlohmann::json VansFloatCurve::Serialize() const
    {
        nlohmann::json j;
        switch (m_Mode)
        {
        case FloatCurveMode::Constant:
            j["mode"] = "Constant";
            j["value"] = m_Value;
            break;
        case FloatCurveMode::RandomBetween:
            j["mode"] = "RandomBetween";
            j["min"]  = m_Min;
            j["max"]  = m_Max;
            break;
        case FloatCurveMode::Curve:
        {
            j["mode"] = "Curve";
            auto keys = nlohmann::json::array();
            for (auto& k : m_Keys)
                keys.push_back({ {"t", k.t}, {"value", k.value} });
            j["keys"] = keys;
            break;
        }
        case FloatCurveMode::RandomBetweenCurves:
        {
            j["mode"] = "RandomBetweenCurves";
            auto minKeys = nlohmann::json::array();
            auto maxKeys = nlohmann::json::array();
            for (auto& k : m_MinKeys) minKeys.push_back({ {"t", k.t}, {"value", k.value} });
            for (auto& k : m_MaxKeys) maxKeys.push_back({ {"t", k.t}, {"value", k.value} });
            j["minKeys"] = minKeys;
            j["maxKeys"] = maxKeys;
            break;
        }
        }
        return j;
    }

    void VansFloatCurve::Deserialize(const nlohmann::json& j)
    {
        std::string mode = j.value("mode", "Constant");
        if (mode == "Constant")
        {
            m_Mode  = FloatCurveMode::Constant;
            m_Value = j.value("value", 1.f);
        }
        else if (mode == "RandomBetween")
        {
            m_Mode = FloatCurveMode::RandomBetween;
            m_Min  = j.value("min", 0.f);
            m_Max  = j.value("max", 1.f);
        }
        else if (mode == "Curve")
        {
            m_Mode = FloatCurveMode::Curve;
            m_Keys.clear();
            if (j.contains("keys"))
            {
                for (auto& k : j["keys"])
                    m_Keys.push_back({ k.value("t", 0.f), k.value("value", 0.f) });
            }
        }
        else if (mode == "RandomBetweenCurves")
        {
            m_Mode = FloatCurveMode::RandomBetweenCurves;
            m_MinKeys.clear();
            m_MaxKeys.clear();
            if (j.contains("minKeys"))
            {
                for (auto& k : j["minKeys"])
                    m_MinKeys.push_back({ k.value("t", 0.f), k.value("value", 0.f) });
            }
            if (j.contains("maxKeys"))
            {
                for (auto& k : j["maxKeys"])
                    m_MaxKeys.push_back({ k.value("t", 0.f), k.value("value", 0.f) });
            }
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansColorGradient::Evaluate
    // ──────────────────────────────────────────────────────────────────────

    glm::vec4 VansColorGradient::Evaluate(float normalizedT) const
    {
        if (m_Stops.empty()) return glm::vec4(1.f);
        if (m_Stops.size() == 1) return m_Stops[0].color;
        if (normalizedT <= m_Stops.front().t) return m_Stops.front().color;
        if (normalizedT >= m_Stops.back().t)  return m_Stops.back().color;

        for (size_t i = 0; i + 1 < m_Stops.size(); ++i)
        {
            const ColorGradientStop& a = m_Stops[i];
            const ColorGradientStop& b = m_Stops[i + 1];
            if (normalizedT >= a.t && normalizedT <= b.t)
            {
                float span = b.t - a.t;
                if (span < 1e-6f) return a.color;
                float alpha = (normalizedT - a.t) / span;
                return glm::mix(a.color, b.color, alpha);
            }
        }
        return m_Stops.back().color;
    }

    nlohmann::json VansColorGradient::Serialize() const
    {
        auto stops = nlohmann::json::array();
        for (auto& s : m_Stops)
        {
            stops.push_back({
                {"t", s.t},
                {"color", { s.color.r, s.color.g, s.color.b, s.color.a }}
            });
        }
        return nlohmann::json{ {"stops", stops} };
    }

    void VansColorGradient::Deserialize(const nlohmann::json& j)
    {
        m_Stops.clear();
        if (j.contains("stops"))
        {
            for (auto& s : j["stops"])
            {
                ColorGradientStop stop;
                stop.t = s.value("t", 0.f);
                if (s.contains("color") && s["color"].is_array() && s["color"].size() >= 4)
                {
                    stop.color = glm::vec4(
                        s["color"][0].get<float>(),
                        s["color"][1].get<float>(),
                        s["color"][2].get<float>(),
                        s["color"][3].get<float>()
                    );
                }
                m_Stops.push_back(stop);
            }
        }
    }

} // namespace VansGraphics
