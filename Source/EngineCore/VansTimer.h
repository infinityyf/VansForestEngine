#pragma once
#include <chrono>

namespace VansGraphics
{
    class VansTimer
    {
    public:
        static void Update();
        static void Reset();

        // Returns the cached per-frame delta. This value is stable for the whole frame.
        static double GetDeltaTime();

        // Alias for GetDeltaTime() kept for compatibility with existing call sites.
        static double GetLastFrameDelta();

        static double GetFrameTime();
        static double GetPhysicsDeltaTime();
        static void SetPhysicsDeltaTime(double deltaTime);

    private:
        using Clock = std::chrono::steady_clock;

        static std::chrono::time_point<Clock> m_LastFrameTime;

        static double m_FrameTime;

        static double m_LastFrameDelta;

        static double m_PhysicsDeltaTime;

        static bool m_IsInitialized;
    };
}
