#pragma once
#include <chrono>

namespace VansGraphics
{
    class VansTimer
    {
    public:
        static void Update();

        // Returns the live time since lastFrameTime (use sparingly).
        static double GetDeltaTime();

        // Returns the cached frame-to-frame delta computed during Update().
        // This is the correct value for per-frame updates (animation, physics, etc.).
        static double GetLastFrameDelta();
        
        static double GetFrameTime();

    private:

        static std::chrono::time_point<std::chrono::high_resolution_clock> lastFrameTime;

        static double frameTime;

        // Cached delta from the last Update() call — the real frame-to-frame interval.
        static double lastFrameDelta;
    };
}
