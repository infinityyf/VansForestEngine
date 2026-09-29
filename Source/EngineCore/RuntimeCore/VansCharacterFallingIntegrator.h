#pragma once

#include <limits>

namespace Vans
{
    // 通用世界 Y 轴重力积分配置；数值子步不等同于碰撞子步。
    struct VansCharacterFallingDynamics
    {
        float terminalSpeed = (std::numeric_limits<float>::max)();
        float maxTimeStep = 1.0f / 60.0f;
        int maxIterations = 8;
        int maxApexAttempts = 2;
        bool splitAtApex = true;
    };

    struct VansCharacterFallingResult
    {
        float velocity = 0.0f;
        float displacement = 0.0f;
        float simulatedTime = 0.0f;
        float apexTime = -1.0f;
        int steps = 0;
    };

    struct VansCharacterFallingStep
    {
        float velocity = 0.0f;
        float displacement = 0.0f;
        float deltaTime = 0.0f;
        bool apex = false;
    };

    float CharacterSimulationTimeStep(float remaining, int iteration,
        const VansCharacterFallingDynamics& dynamics);
    VansCharacterFallingStep IntegrateCharacterFallingStep(float initialVelocity,
        float gravity, float deltaTime, const VansCharacterFallingDynamics& dynamics, bool allowApex);

    VansCharacterFallingResult IntegrateCharacterFalling(float initialVelocity,
        float gravity, float deltaTime, const VansCharacterFallingDynamics& dynamics);
}
