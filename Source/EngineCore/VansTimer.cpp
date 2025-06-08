#include "VansTimer.h"

std::chrono::time_point<std::chrono::high_resolution_clock> VansGraphics::VansTimer::lastFrameTime;

double VansGraphics::VansTimer::frameTime = 0;

void VansGraphics::VansTimer::Update()
{
    frameTime += GetDeltaTime();
    lastFrameTime = std::chrono::high_resolution_clock::now();
}

double VansGraphics::VansTimer::GetDeltaTime()
{
    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> deltaTime = currentTime - lastFrameTime;
    return deltaTime.count();
}

double VansGraphics::VansTimer::GetFrameTime()
{
    return frameTime;
}
