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
        // 只改变显式配置的随机分布，默认 Uniform 保持原求值和随机流。
        if ((m_Mode == FloatCurveMode::RandomBetween || m_Mode == FloatCurveMode::RandomBetweenCurves)
            && m_RandomDistribution != VansParticleRandomDistribution::Uniform)
        {
            r = std::clamp(r, 0.f, 1.f);
            if (m_RandomDistribution == VansParticleRandomDistribution::StandardBias)
            {
                const float b = std::clamp((m_RandomBias + 1.f) * .5f, 0.f, 1.f);
                r = b <= 0 ? 0.f : b >= 1 ? 1.f : r / ((1.f-r)*(1.f/b-2.f)+1.f);
            }
            else
            {
                const float exponent = m_RandomBias < 0
                    ? 20.f - 19.f * std::clamp(m_RandomBias+1.f, 0.f, 1.f)
                    : 1.f - std::clamp(m_RandomBias, 0.f, 1.f);
                r = r <= 0 ? 0.f : r >= 1 ? 1.f : exponent <= 0 ? 1.f : std::pow(r, exponent);
            }
        }
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

} // namespace VansGraphics
