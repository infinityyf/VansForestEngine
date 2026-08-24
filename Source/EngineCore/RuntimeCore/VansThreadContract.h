#pragma once

#include <cassert>
#include <thread>

enum class VansThreadRole
{
    Unknown,
    Main,
    Render,
    RenderWorker
};

// ???????? Debug ?????????????????????????
#ifdef _DEBUG
extern std::thread::id g_MainThreadId;
extern thread_local VansThreadRole g_CurrentThreadRole;

#define VANS_INIT_MAIN_THREAD() \
    do \
    { \
        ::g_MainThreadId = std::this_thread::get_id(); \
        ::g_CurrentThreadRole = VansThreadRole::Main; \
    } while (false)
#define VANS_INIT_RENDER_THREAD() (::g_CurrentThreadRole = VansThreadRole::Render)
#define VANS_INIT_RENDER_WORKER_THREAD() (::g_CurrentThreadRole = VansThreadRole::RenderWorker)
#define VANS_CLEAR_THREAD_ROLE() (::g_CurrentThreadRole = VansThreadRole::Unknown)

#define VANS_ASSERT_MAIN_THREAD() \
    do \
    { \
        assert(std::this_thread::get_id() == ::g_MainThreadId); \
        assert(::g_CurrentThreadRole == VansThreadRole::Main); \
    } while (false)
#define VANS_ASSERT_NOT_MAIN_THREAD() assert(std::this_thread::get_id() != ::g_MainThreadId)
#define VANS_ASSERT_RENDER_THREAD() assert(::g_CurrentThreadRole == VansThreadRole::Render)
#define VANS_ASSERT_RENDER_WORKER_THREAD() assert(::g_CurrentThreadRole == VansThreadRole::RenderWorker)
#else
#define VANS_INIT_MAIN_THREAD() ((void)0)
#define VANS_INIT_RENDER_THREAD() ((void)0)
#define VANS_INIT_RENDER_WORKER_THREAD() ((void)0)
#define VANS_CLEAR_THREAD_ROLE() ((void)0)
#define VANS_ASSERT_MAIN_THREAD() ((void)0)
#define VANS_ASSERT_NOT_MAIN_THREAD() ((void)0)
#define VANS_ASSERT_RENDER_THREAD() ((void)0)
#define VANS_ASSERT_RENDER_WORKER_THREAD() ((void)0)
#endif
