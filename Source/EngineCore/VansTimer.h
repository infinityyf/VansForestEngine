#pragma once
#include <chrono>

namespace VansGraphics
{
    class VansTimer
    {
    public:
        static void Update();

        static double GetDeltaTime();
        

    private:

        static std::chrono::time_point<std::chrono::high_resolution_clock> lastFrameTime;
    };
}
