#include "VansTimer.h"

std::chrono::time_point<std::chrono::high_resolution_clock> VansGraphics::VansTimer::lastFrameTime;

void VansGraphics::VansTimer::Update()
{
    lastFrameTime = std::chrono::high_resolution_clock::now();
}

double VansGraphics::VansTimer::GetDeltaTime()
{
    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> deltaTime = currentTime - lastFrameTime;
    return deltaTime.count();
}