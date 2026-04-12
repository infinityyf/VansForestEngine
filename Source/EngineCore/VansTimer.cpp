#include "VansTimer.h"
#include "Util/VansLog.h"

std::chrono::time_point<VansGraphics::VansTimer::Clock> VansGraphics::VansTimer::m_LastFrameTime = VansGraphics::VansTimer::Clock::now();

double VansGraphics::VansTimer::m_FrameTime = 0.0;

double VansGraphics::VansTimer::m_LastFrameDelta = 0.0;

double VansGraphics::VansTimer::m_PhysicsDeltaTime = 1.0 / 60.0;

bool VansGraphics::VansTimer::m_IsInitialized = false;

void VansGraphics::VansTimer::Update()
{
    const auto currentTime = Clock::now();

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

double VansGraphics::VansTimer::GetFrameTime()
{
    return m_FrameTime;
}

double VansGraphics::VansTimer::GetPhysicsDeltaTime()
{
    return m_PhysicsDeltaTime;
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
