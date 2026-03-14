#include "VansTimer.h"

std::chrono::time_point<std::chrono::high_resolution_clock> VansGraphics::VansTimer::lastFrameTime;

double VansGraphics::VansTimer::frameTime = 0;

double VansGraphics::VansTimer::lastFrameDelta = 0;

void VansGraphics::VansTimer::Update()
{
    // Compute the real frame-to-frame delta and cache it before resetting the timer.
    lastFrameDelta = GetDeltaTime();
    frameTime += lastFrameDelta;
    lastFrameTime = std::chrono::high_resolution_clock::now();
}

double VansGraphics::VansTimer::GetDeltaTime()
{
    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> deltaTime = currentTime - lastFrameTime;
    return deltaTime.count();
}

double VansGraphics::VansTimer::GetLastFrameDelta()
{
    return lastFrameDelta;
}

double VansGraphics::VansTimer::GetFrameTime()
{
    return frameTime;
}
