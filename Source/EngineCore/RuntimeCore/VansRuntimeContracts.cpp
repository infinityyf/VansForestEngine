#include "VansFramePhase.h"
#include "VansThreadContract.h"

#ifdef _DEBUG
VansFramePhase g_CurrentFramePhase = VansFramePhase::GameLogic;
std::thread::id g_MainThreadId;
#endif
