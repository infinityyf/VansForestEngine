#include "VansFramePhase.h"
#include "VansThreadContract.h"

#ifdef _DEBUG
thread_local VansFramePhase g_CurrentFramePhase = VansFramePhase::GameLogic;
thread_local VansThreadRole g_CurrentThreadRole = VansThreadRole::Unknown;
std::thread::id g_MainThreadId;
#endif
