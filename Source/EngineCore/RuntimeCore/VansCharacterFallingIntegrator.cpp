#include "VansCharacterFallingIntegrator.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
    float CharacterSimulationTimeStep(float remaining, int iteration,
        const VansCharacterFallingDynamics& dynamics)
    {
        const float maxStep = (std::max)(1.e-6f, dynamics.maxTimeStep);
        return remaining > maxStep && iteration < std::clamp(dynamics.maxIterations, 1, 32)
            ? (std::min)(maxStep, remaining * 0.5f) : remaining;
    }

    VansCharacterFallingStep IntegrateCharacterFallingStep(float initialVelocity,
        float gravity, float deltaTime, const VansCharacterFallingDynamics& dynamics, bool allowApex)
    {
        VansCharacterFallingStep result;
        result.velocity = initialVelocity;
        if (!std::isfinite(deltaTime) || deltaTime < 1.e-6f) return result;
        result.deltaTime = deltaTime;
        const float acceleration = -(std::max)(0.0f, gravity);
        result.velocity += acceleration * deltaTime;
        if (acceleration < 0.0f)
            result.velocity = (std::max)(result.velocity, -(std::max)(0.0f, dynamics.terminalSpeed));
        if (allowApex && dynamics.splitAtApex && initialVelocity > 0.0f && result.velocity <= 0.0f)
        {
            const float derivedAcceleration = (result.velocity - initialVelocity) / deltaTime;
            if (std::abs(derivedAcceleration) > 1.e-10f)
            {
                const float timeToApex = -initialVelocity / derivedAcceleration;
                if (timeToApex >= 0.0001f && timeToApex < deltaTime)
                {
                    result.deltaTime = timeToApex;
                    result.velocity = 0.0f;
                    result.apex = true;
                }
            }
        }
        result.displacement = 0.5f * (initialVelocity + result.velocity) * result.deltaTime;
        return result;
    }

    VansCharacterFallingResult IntegrateCharacterFalling(float initialVelocity,
        float gravity, float deltaTime, const VansCharacterFallingDynamics& dynamics)
    {
        VansCharacterFallingResult result;
        result.velocity = initialVelocity;
        constexpr float minTick = 1.e-6f;
        if (!std::isfinite(deltaTime) || deltaTime < minTick) return result;
        const int maxIterations = std::clamp(dynamics.maxIterations, 1, 32);
        const int maxApexAttempts = std::clamp(dynamics.maxApexAttempts, 0, 8);
        float remaining = deltaTime;
        int iterations = 0, apexAttempts = 0;
        while (remaining >= minTick && iterations < maxIterations)
        {
            ++iterations;
            const auto step = IntegrateCharacterFallingStep(result.velocity, gravity,
                CharacterSimulationTimeStep(remaining, iterations, dynamics), dynamics,
                apexAttempts < maxApexAttempts);
            remaining -= step.deltaTime;
            if (step.apex)
            {
                --iterations;
                ++apexAttempts;
                result.apexTime = result.simulatedTime + step.deltaTime;
            }
            result.displacement += step.displacement;
            result.velocity = step.velocity;
            result.simulatedTime += step.deltaTime;
            ++result.steps;
        }
        return result;
    }
}
