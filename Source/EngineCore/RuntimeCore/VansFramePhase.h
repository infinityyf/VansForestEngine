#pragma once

#include <cassert>

// ?????????? Debug ????????????? Release ???
enum class VansFramePhase
{
    GameLogic,
    RenderPrep,
    RenderPacketBuild,
    RenderThreadConsume,
    GPURecord,
    ParallelGPURecord,
    QueueSubmit,
    Present
};

#ifdef _DEBUG
extern thread_local VansFramePhase g_CurrentFramePhase;

#define VANS_SET_FRAME_PHASE(phase) (::g_CurrentFramePhase = (phase))
#define VANS_ASSERT_FRAME_PHASE(expected) assert(::g_CurrentFramePhase == (expected))
#else
#define VANS_SET_FRAME_PHASE(phase) ((void)0)
#define VANS_ASSERT_FRAME_PHASE(expected) ((void)0)
#endif
