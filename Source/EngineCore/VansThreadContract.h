#pragma once

#include <cassert>
#include <thread>

// 线程角色契约：仅 Debug 构建启用，用于防止加载/卸载等主线程流程被误调度。
#ifdef _DEBUG
extern std::thread::id g_MainThreadId;

#define VANS_INIT_MAIN_THREAD() (::g_MainThreadId = std::this_thread::get_id())
#define VANS_ASSERT_MAIN_THREAD() assert(std::this_thread::get_id() == ::g_MainThreadId)
#define VANS_ASSERT_NOT_MAIN_THREAD() assert(std::this_thread::get_id() != ::g_MainThreadId)
#else
#define VANS_INIT_MAIN_THREAD() ((void)0)
#define VANS_ASSERT_MAIN_THREAD() ((void)0)
#define VANS_ASSERT_NOT_MAIN_THREAD() ((void)0)
#endif