#pragma once

#include <cassert>

// 帧内三层分工：仅用于 Debug 构建验证主循环时序，不参与 Release 逻辑。
enum class VansFramePhase
{
    GameLogic,
    RenderPrep,
    GPURecord
};

#ifdef _DEBUG
extern VansFramePhase g_CurrentFramePhase;

#define VANS_SET_FRAME_PHASE(phase) (::g_CurrentFramePhase = (phase))
#define VANS_ASSERT_FRAME_PHASE(expected) assert(::g_CurrentFramePhase == (expected))
#else
#define VANS_SET_FRAME_PHASE(phase) ((void)0)
#define VANS_ASSERT_FRAME_PHASE(expected) ((void)0)
#endif