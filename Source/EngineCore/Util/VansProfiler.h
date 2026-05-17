#pragma once

#ifndef VANS_PROFILER_ENABLED
  #define VANS_PROFILER_ENABLED 1
#endif

#include <cstdint>

namespace Vans
{
    enum class ProfileTrackType : uint8_t
    {
        CpuThread,
        GpuQueue,
        Marker
    };

    enum class ProfileCategory : uint8_t
    {
        Frame,
        Editor,
        Script,
        Physics,
        Animation,
        Particles,
        Audio,
        Video,
        RuntimeUI,
        RenderPrepare,
        CommandRecord,
        VulkanSubmit,
        GPU,
        JobSystem,
        Wait,
        IO,
        Other
    };

    enum ProfileEventFlags : uint16_t
    {
        ProfileEventFlagNone     = 0,
        ProfileEventFlagWait     = 1 << 0,
        ProfileEventFlagGpu      = 1 << 1,
        ProfileEventFlagOverflow = 1 << 2
    };

    struct ProfileTrack
    {
        uint32_t         trackId  = 0;
        ProfileTrackType type     = ProfileTrackType::CpuThread;
        uint64_t         threadId = 0;
        char             name[48] = {};
        uint32_t         color    = 0xffffffffu;
    };

    struct ProfileEvent
    {
        uint32_t        eventId       = 0;
        uint32_t        parentEventId = 0;
        uint32_t        trackId       = 0;
        ProfileCategory category      = ProfileCategory::Other;
        char            name[96]      = {};
        double          startUs       = 0.0;
        double          endUs         = 0.0;
        uint16_t        depth         = 0;
        uint16_t        flags         = ProfileEventFlagNone;
    };

    struct ProfileFrame
    {
        static constexpr uint32_t MAX_TRACKS = 64;
        static constexpr uint32_t MAX_EVENTS = 8192;

        uint64_t     frameIndex      = 0;
        double       frameDurationUs = 0.0;
        double       fps             = 0.0;
        uint32_t     trackCount      = 0;
        uint32_t     eventCount      = 0;
        bool         overflow        = false;
        ProfileTrack tracks[MAX_TRACKS] = {};
        ProfileEvent events[MAX_EVENTS] = {};
    };

    class VansCpuProfiler
    {
    public:
        static VansCpuProfiler& Get();

        void BeginFrame(uint64_t frameIndex, int64_t frameStartNs);
        void EndFrame(ProfileFrame& frame, double frameDurationUs);
        uint32_t RegisterCurrentThread(const char* name, uint32_t color = 0xff62c96bu);
        bool Push(const char* name, ProfileCategory category, uint16_t flags = ProfileEventFlagNone);
        void Pop();

    private:
        static constexpr uint32_t MAX_DEPTH = 64;

        struct StackEntry
        {
            char            name[96]      = {};
            ProfileCategory category      = ProfileCategory::Other;
            uint16_t        flags         = ProfileEventFlagNone;
            uint16_t        depth         = 0;
            uint32_t        eventId       = 0;
            uint32_t        parentEventId = 0;
            uint32_t        trackId       = 0;
            uint64_t        frameIndex    = 0;
            int64_t         startNs       = 0;
        };

        struct ThreadContext
        {
            uint32_t   trackId = 0;
            uint32_t   depth   = 0;
            StackEntry stack[MAX_DEPTH] = {};
        };

        static ThreadContext& GetThreadContext();
        static uint64_t GetCurrentThreadIdValue();

        uint32_t RegisterTrack(uint64_t threadId, const char* name, ProfileTrackType type, uint32_t color);
        uint32_t AllocateEventId();
        void AddEvent(const ProfileEvent& event);
        void CopyText(char* dst, uint32_t dstSize, const char* src) const;

    private:
        ProfileTrack m_Tracks[ProfileFrame::MAX_TRACKS] = {};
        uint32_t     m_TrackCount = 0;
        ProfileEvent m_Events[ProfileFrame::MAX_EVENTS] = {};
        uint32_t     m_EventCount = 0;
        uint32_t     m_NextEventId = 1;
        uint64_t     m_FrameIndex = 0;
        int64_t      m_FrameStartNs = 0;
        bool         m_FrameActive = false;
        bool         m_Overflow = false;
    };

    struct VansCpuScopeTimer
    {
        explicit VansCpuScopeTimer(const char* name, ProfileCategory category = ProfileCategory::Other, uint16_t flags = ProfileEventFlagNone);
        ~VansCpuScopeTimer();

    private:
        bool m_Active = false;
    };

    class VansGpuProfiler
    {
    public:
        static VansGpuProfiler& Get();

        void Init(void* device, void* physDevice, uint32_t queueFamily);
        void Destroy();
        void BeginFrame(void* cmd);
        void Push(void* cmd, const char* name);
        void Pop(void* cmd);
        void EndFrame();
        void Resolve(void* device);
        void CollectInto(ProfileFrame& frame) const;

        bool IsInitialized() const { return m_Pools[0] != nullptr; }

    private:
        static constexpr int MAX_GPU_QUERIES = 64;
        static constexpr int POOL_COUNT      = 2;

        struct Slot
        {
            char     name[96]  = {};
            uint32_t beginSlot = 0;
            uint32_t endSlot   = 0;
            uint16_t depth     = 0;
        };

        void CopyText(char* dst, uint32_t dstSize, const char* src) const;

    private:
        void*        m_Pools[POOL_COUNT] = {};
        void*        m_Device = nullptr;
        double       m_TimestampPeriodMs = 0.0;
        uint32_t     m_WriteIdx = 0;
        bool         m_HasPreviousFrame = false;
        Slot         m_Slots[POOL_COUNT][MAX_GPU_QUERIES] = {};
        int          m_SlotCount[POOL_COUNT] = {};
        uint32_t     m_NextQuery[POOL_COUNT] = {};
        int          m_StackDepth = 0;
        uint64_t     m_RawResults[MAX_GPU_QUERIES * 2] = {};
        ProfileEvent m_Events[MAX_GPU_QUERIES] = {};
        int          m_EventCount = 0;
    };

    struct VansGpuScopeQuery
    {
        void* m_Cmd = nullptr;
        bool  m_Active = false;
        explicit VansGpuScopeQuery(void* cmd, const char* name);
        ~VansGpuScopeQuery();
    };

    class VansProfiler
    {
    public:
        static VansProfiler& Get();

        void BeginFrame();
        void EndFrame(void* device);
        void RegisterCurrentThread(const char* name, uint32_t color = 0xff62c96bu);
        const ProfileFrame& GetTimeline() const { return m_Frame; }
        void PrintTimeline() const;
        void DumpFrameJson(const char* outputDir = "LOG") const;

    private:
        ProfileFrame m_Frame = {};
        uint64_t     m_FrameIndex = 0;
        int64_t      m_FrameStartNs = 0;
    };
}

#if VANS_PROFILER_ENABLED
  #define VANS_PROFILE_CONCAT_IMPL(a, b) a##b
  #define VANS_PROFILE_CONCAT(a, b) VANS_PROFILE_CONCAT_IMPL(a, b)
  #define VANS_PROFILE_THREAD(name)       Vans::VansProfiler::Get().RegisterCurrentThread(name)
  #define VANS_PROFILE_SCOPE(name, cat)   Vans::VansCpuScopeTimer VANS_PROFILE_CONCAT(_vans_cpu_scope_, __LINE__)(name, cat)
  #define VANS_PROFILE_WAIT(name)         Vans::VansCpuScopeTimer VANS_PROFILE_CONCAT(_vans_cpu_wait_, __LINE__)(name, Vans::ProfileCategory::Wait, Vans::ProfileEventFlagWait)
  #define VANS_CPU_SCOPE(name)            Vans::VansCpuScopeTimer VANS_PROFILE_CONCAT(_vans_cpu_scope_, __LINE__)(name, Vans::ProfileCategory::Other)
  #define VANS_GPU_SCOPE(cmd, name)       Vans::VansGpuScopeQuery VANS_PROFILE_CONCAT(_vans_gpu_scope_, __LINE__)((void*)(cmd), name)
  #define VANS_PROFILER_BEGIN_FRAME()     Vans::VansProfiler::Get().BeginFrame()
  #define VANS_PROFILER_END_FRAME(dev)    Vans::VansProfiler::Get().EndFrame((void*)(dev))
  #define VANS_PROFILER_PRINT()           Vans::VansProfiler::Get().PrintTimeline()
  #define VANS_PROFILER_DUMP_JSON()       Vans::VansProfiler::Get().DumpFrameJson()
#else
  #define VANS_PROFILE_THREAD(name)       /* no-op */
  #define VANS_PROFILE_SCOPE(name, cat)   /* no-op */
  #define VANS_PROFILE_WAIT(name)         /* no-op */
  #define VANS_CPU_SCOPE(name)            /* no-op */
  #define VANS_GPU_SCOPE(cmd, name)       /* no-op */
  #define VANS_PROFILER_BEGIN_FRAME()     /* no-op */
  #define VANS_PROFILER_END_FRAME(dev)    /* no-op */
  #define VANS_PROFILER_PRINT()           /* no-op */
  #define VANS_PROFILER_DUMP_JSON()       /* no-op */
#endif
