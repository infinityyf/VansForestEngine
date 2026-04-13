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

		// 编辑器专用 delta time：无论时间是否冻结均持续推进
		// 用于编辑器相机输入，保证编辑/暂停状态下相机仍可移动
		static double GetEditorDeltaTime();
        static double GetFrameTime();
        static double GetPhysicsDeltaTime();
        static void SetPhysicsDeltaTime(double deltaTime);

        // 编辑器运行控制：冻结/恢复逻辑时间推进
        static void SetTimePaused(bool paused);
        static bool IsTimePaused();

    private:
        using Clock = std::chrono::steady_clock;

        static std::chrono::time_point<Clock> m_LastFrameTime;

		// 编辑器时间戳（不受 m_IsPaused 影响）
		static std::chrono::time_point<Clock> m_EditorLastFrameTime;
		static double m_EditorDeltaTime;
        static double m_FrameTime;

        static double m_LastFrameDelta;

        static double m_PhysicsDeltaTime;

        static bool m_IsInitialized;

        // 时间是否被冻结（编辑器暂停/编辑状态下为 true）
        static bool m_IsPaused;
    };
}
