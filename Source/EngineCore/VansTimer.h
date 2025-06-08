#pragma once
#include <chrono>

namespace VansGraphics
{
    class VansTimer
    {
    public:
        static void Update();

        static double GetDeltaTime();
        
        static double GetFrameTime();

    private:

        static std::chrono::time_point<std::chrono::high_resolution_clock> lastFrameTime;

        static double frameTime;
    };
}
