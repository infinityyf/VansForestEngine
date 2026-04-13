#include "VansTimer.h"
#include "Util/VansLog.h"

std::chrono::time_point<VansGraphics::VansTimer::Clock> VansGraphics::VansTimer::m_LastFrameTime = VansGraphics::VansTimer::Clock::now();

// 编辑器专用时间戳，始终推进（不受 m_IsPaused 影响）
std::chrono::time_point<VansGraphics::VansTimer::Clock> VansGraphics::VansTimer::m_EditorLastFrameTime = VansGraphics::VansTimer::Clock::now();
double VansGraphics::VansTimer::m_EditorDeltaTime = 0.0;

double VansGraphics::VansTimer::m_FrameTime = 0.0;

double VansGraphics::VansTimer::m_LastFrameDelta = 0.0;

double VansGraphics::VansTimer::m_PhysicsDeltaTime = 1.0 / 60.0;

bool VansGraphics::VansTimer::m_IsInitialized = false;

// 默认冻结：场景加载完成后处于编辑模式，时间不推进
bool VansGraphics::VansTimer::m_IsPaused = true;

void VansGraphics::VansTimer::Update()
{
    const auto currentTime = Clock::now();

    // 编辑器 delta 始终推进，不受暂停状态影响（用于编辑器相机等工具逻辑）
    {
        const std::chrono::duration<double> editorDelta = currentTime - m_EditorLastFrameTime;
        m_EditorDeltaTime    = editorDelta.count();
        m_EditorLastFrameTime = currentTime;
    }

    // 时间被冻结时（编辑/暂停状态），delta 归零并刷新基准时间点
    // 刷新基准点是为了防止恢复后出现一个巨大的跳帧 delta
    if (m_IsPaused)
    {
        m_LastFrameDelta = 0.0;
        m_LastFrameTime  = currentTime;
        return;
    }

    if (!m_IsInitialized)
    {
        m_LastFrameTime = currentTime;
        m_LastFrameDelta = 0.0;
        m_FrameTime = 0.0;
        m_IsInitialized = true;
        return;
    }

    const std::chrono::duration<double> deltaTime = currentTime - m_LastFrameTime;
    m_LastFrameDelta = deltaTime.count();
    m_FrameTime += m_LastFrameDelta;
    m_LastFrameTime = currentTime;
}

void VansGraphics::VansTimer::Reset()
{
    m_LastFrameTime = Clock::now();
    m_FrameTime = 0.0;
    m_LastFrameDelta = 0.0;
    m_IsInitialized = false;
}

double VansGraphics::VansTimer::GetDeltaTime()
{
    return m_LastFrameDelta;
}

double VansGraphics::VansTimer::GetLastFrameDelta()
{
    return m_LastFrameDelta;
}

double VansGraphics::VansTimer::GetEditorDeltaTime()
{
    return m_EditorDeltaTime;
}

double VansGraphics::VansTimer::GetFrameTime()
{
    return m_FrameTime;
}

double VansGraphics::VansTimer::GetPhysicsDeltaTime()
{
    return m_PhysicsDeltaTime;
}

void VansGraphics::VansTimer::SetTimePaused(bool paused)
{
    m_IsPaused = paused;
}

bool VansGraphics::VansTimer::IsTimePaused()
{
    return m_IsPaused;
}

void VansGraphics::VansTimer::SetPhysicsDeltaTime(double deltaTime)
{
    if (deltaTime <= 0.0)
    {
        VANS_LOG_WARN("[VansTimer] Ignore invalid physics delta time: " << deltaTime);
        return;
    }

    m_PhysicsDeltaTime = deltaTime;
}
