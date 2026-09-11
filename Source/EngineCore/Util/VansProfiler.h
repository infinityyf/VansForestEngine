#pragma once

#ifndef VANS_PROFILER_ENABLED
  #define VANS_PROFILER_ENABLED 1
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <map>
#include <string>
#include <vector>

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
        uint32_t         trackId         = 0;
        ProfileTrackType type            = ProfileTrackType::CpuThread;
        uint64_t         threadId        = 0;
        char             name[48]        = {};
        uint32_t         color           = 0xffffffffu;
        uint32_t         firstEventIndex = 0;
        uint32_t         eventCount      = 0;
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

        uint64_t     frameIndex         = 0;
        double       frameDurationUs    = 0.0;
        double       timelineDurationUs = 0.0;
        double       gpuDurationUs      = 0.0;
        double       fps                = 0.0;
        uint32_t     trackCount         = 0;
        uint32_t     eventCount         = 0;
        uint32_t     droppedCpuEvents   = 0;
        uint32_t     droppedGpuEvents   = 0;
        bool         gpuExpected        = false;
        bool         gpuComplete        = false;
        bool         overflow           = false;
        ProfileTrack tracks[MAX_TRACKS] = {};
        ProfileEvent events[MAX_EVENTS] = {};
    };

    // 仅用于显式启用的自动性能验收；保存紧凑统计，不复制完整事件帧。
    class VansProfileCapture
    {
    public:
        struct Settings
        {
            uint32_t windowFrames = 120;
            uint32_t stableWindows = 3;
            uint32_t measurementFrames = 120;
            double minimumWindowSeconds = 5.0;
            double relativeDrift = 0.10;
            double maximumP95Spread = 0.25;
            double maximumSpikeRatio = 3.0;
        };
        enum class Phase { Settling, Measuring, Complete };
        struct Statistics
        {
            double minimum = 0.0, maximum = 0.0, mean = 0.0;
            double median = 0.0, p95 = 0.0, p99 = 0.0;
        };
        struct Sample
        {
            uint64_t frameIndex = 0;
            double observedSeconds = 0.0, cpuUs = 0.0, gpuUs = 0.0;
        };

        void Start(const Settings& settings, uint64_t firstFrameIndex);
        void Cancel();
        void Observe(const ProfileFrame& frame, double nowSeconds);
        bool IsActive() const { return m_Active; }
        Phase GetPhase() const { return m_Phase; }
        uint32_t GetStableWindows() const { return m_StableWindows; }
        uint64_t GetRestartCount() const { return m_Restarts; }
        uint64_t GetMissingFrames() const { return m_MissingFrames; }
        uint64_t GetNextFrameIndex() const { return m_HasLastFrame ? m_LastFrameIndex + 1 : m_FirstFrameIndex; }
        uint64_t GetDiscardedMeasurementSamples() const { return m_DiscardedMeasurementSamples; }
        const std::vector<Sample>& GetSamples() const { return m_Samples; }
        bool DumpJson(const char* outputDir) const;
        static Statistics Summarize(std::vector<double> values);

    private:
        void Restart();
        bool WindowStable(const Statistics& cpu, const Statistics& gpu) const;
        void CollectGpuScopes(const ProfileFrame& frame);
        void CollectCpuScopes(const ProfileFrame& frame);
        struct CpuScopeSamples
        {
            std::string trackName;
            bool wait = false;
            std::vector<double> inclusive, exclusive, calls;
        };
        Settings m_Settings;
        bool m_Active = false;
        Phase m_Phase = Phase::Settling;
        uint64_t m_FirstFrameIndex = 0, m_LastFrameIndex = 0;
        bool m_HasLastFrame = false;
        double m_LastObservedSeconds = 0.0;
        uint64_t m_ObservedFrames = 0, m_MissingFrames = 0, m_InvalidFrames = 0;
        uint64_t m_Restarts = 0, m_RejectedWindows = 0, m_DiscardedMeasurementSamples = 0;
        uint32_t m_StableWindows = 0;
        Statistics m_BaselineCpu, m_BaselineGpu;
        std::vector<Sample> m_Window, m_Samples;
        std::map<std::string, std::vector<double>> m_GpuScopes;
        std::map<std::pair<uint32_t, std::string>, CpuScopeSamples> m_CpuScopes;
    };

    class VansCpuProfiler
    {
    public:
        static constexpr uint64_t INVALID_FRAME_INDEX = std::numeric_limits<uint64_t>::max();

        static VansCpuProfiler& Get();

        bool BeginFrame(uint64_t frameIndex, int64_t frameStartNs);
        void EndFrame(uint64_t frameIndex, int64_t frameEndNs);
        void MarkFrameHasRender(uint64_t frameIndex);
        void BindCurrentThreadToFrame(uint64_t frameIndex);
        void EndRenderFrame(uint64_t frameIndex);

        bool PeekReadyFrame(uint64_t& frameIndex) const;
        bool CollectReadyFrame(uint64_t frameIndex, ProfileFrame& frame);

        uint32_t RegisterCurrentThread(const char* name, uint32_t color = 0xff62c96bu);
        bool Push(const char* name, ProfileCategory category, uint16_t flags = ProfileEventFlagNone);
        void Pop();

    private:
        static constexpr uint32_t MAX_DEPTH = 64;
        static constexpr uint32_t FRAME_BUFFER_COUNT = 8;
        static constexpr uint64_t INVALID_TOKEN = 0;

        struct StackEntry
        {
            char            name[96]      = {};
            ProfileCategory category      = ProfileCategory::Other;
            uint16_t        flags         = ProfileEventFlagNone;
            uint16_t        depth         = 0;
            uint32_t        eventId       = 0;
            uint32_t        parentEventId = 0;
            uint32_t        trackId       = 0;
            uint64_t        frameToken    = INVALID_TOKEN;
            int64_t         startNs       = 0;
        };

        struct ThreadContext
        {
            uint32_t   trackId    = 0;
            uint32_t   depth      = 0;
            uint64_t   boundToken = INVALID_TOKEN;
            StackEntry stack[MAX_DEPTH] = {};
        };

        struct FrameBuffer
        {
            std::atomic<uint64_t> generation{ INVALID_FRAME_INDEX };
            std::atomic<uint32_t> eventCount{ 0 };
            std::atomic<uint32_t> nextEventId{ 1 };
            std::atomic<uint32_t> activeScopes{ 0 };
            std::atomic_bool accepting{ false };
            std::atomic_bool closed{ false };
            std::atomic_bool renderPending{ false };
            std::atomic_bool expectsGpu{ false };
            std::atomic_bool overflow{ false };
            int64_t startNs = 0;
            int64_t endNs = 0;
            ProfileEvent events[ProfileFrame::MAX_EVENTS] = {};
        };

        static ThreadContext& GetThreadContext();
        static uint64_t GetCurrentThreadIdValue();
        static uint64_t MakeToken(uint32_t bufferIndex, uint64_t frameIndex);
        static uint32_t TokenBufferIndex(uint64_t token);
        static uint64_t TokenFrameIndex(uint64_t token);

        uint64_t FindFrameToken(uint64_t frameIndex) const;
        FrameBuffer* ValidateToken(uint64_t token);
        const FrameBuffer* ValidateToken(uint64_t token) const;
        uint32_t RegisterTrack(uint64_t threadId, const char* name, uint32_t color);
        static void CopyText(char* dst, uint32_t dstSize, const char* src);

        std::array<FrameBuffer, FRAME_BUFFER_COUNT> m_FrameBuffers{};
        std::atomic<uint64_t> m_ActiveToken{ INVALID_TOKEN };

        mutable std::mutex m_TrackMutex;
        ProfileTrack m_Tracks[ProfileFrame::MAX_TRACKS] = {};
        uint32_t m_TrackCount = 0;
    };

    struct VansCpuScopeTimer
    {
        explicit VansCpuScopeTimer(
            const char* name,
            ProfileCategory category = ProfileCategory::Other,
            uint16_t flags = ProfileEventFlagNone);
        ~VansCpuScopeTimer();

    private:
        bool m_Active = false;
    };

    enum class VansGpuQueueLane : uint8_t
    {
        Graphics = 0,
        Compute = 1
    };

    struct VansGpuResolvedFrame
    {
        static constexpr uint32_t MAX_EVENTS = 256;

        uint64_t frameIndex = VansCpuProfiler::INVALID_FRAME_INDEX;
        uint32_t eventCount = 0;
        uint32_t laneEventCount[2] = {};
        uint32_t droppedEvents = 0;
        double durationUs = 0.0;
        bool overflow = false;
        ProfileEvent events[MAX_EVENTS] = {};
    };

    class VansGpuProfiler
    {
    public:
        static VansGpuProfiler& Get();

        void Init(
            void* device,
            void* physDevice,
            uint32_t graphicsQueueFamily,
            uint32_t computeQueueFamily,
            bool hostQueryResetEnabled);
        void Destroy();

        void BeginFrame(uint64_t frameIndex);
        void BeginQueue(void* cmd, VansGpuQueueLane lane);
        bool Push(void* cmd, const char* name, VansGpuQueueLane lane = VansGpuQueueLane::Graphics);
        void Pop(void* cmd, VansGpuQueueLane lane = VansGpuQueueLane::Graphics);
        void EndFrame();

        bool IsInitialized() const;
        bool IsFrameCaptureActive() const;

    private:
        static constexpr uint32_t MAX_GPU_SCOPES = 128;
        static constexpr uint32_t QUERIES_PER_SCOPE = 2;
        static constexpr uint32_t QUERY_COUNT = MAX_GPU_SCOPES * QUERIES_PER_SCOPE;
        static constexpr uint32_t FRAME_SLOT_COUNT = 8;
        static constexpr uint32_t LANE_COUNT = 2;
        static constexpr uint32_t MAX_STACK_DEPTH = 64;
        static constexpr uint32_t INVALID_SLOT = std::numeric_limits<uint32_t>::max();

        enum class FrameSlotState : uint8_t
        {
            Free,
            Recording,
            Pending,
            Retired
        };

        struct ScopeSlot
        {
            char     name[96] = {};
            uint32_t beginQuery = 0;
            uint32_t endQuery = 0;
            uint32_t eventId = 0;
            uint32_t parentEventId = 0;
            uint16_t depth = 0;
            bool     closed = false;
        };

        struct FrameSlot
        {
            FrameSlotState state = FrameSlotState::Free;
            uint64_t frameIndex = VansCpuProfiler::INVALID_FRAME_INDEX;
            bool laneBegun[LANE_COUNT] = {};
            bool overflow = false;
            uint32_t droppedEvents = 0;
            uint32_t scopeCount[LANE_COUNT] = {};
            uint32_t nextQuery[LANE_COUNT] = {};
            uint32_t stackDepth[LANE_COUNT] = {};
            uint32_t scopeStack[LANE_COUNT][MAX_STACK_DEPTH] = {};
            ScopeSlot scopes[LANE_COUNT][MAX_GPU_SCOPES] = {};
        };

        struct QueryValue
        {
            uint64_t value = 0;
            uint64_t available = 0;
        };

        void PollPendingFrames();
        bool TryResolveFrame(uint32_t frameSlotIndex, VansGpuResolvedFrame& result);
        void SubmitDroppedFrame(uint64_t frameIndex, uint32_t droppedEvents = 0);
        static void CopyText(char* dst, uint32_t dstSize, const char* src);
        static int64_t TimestampDelta(uint64_t value, uint64_t reference, uint32_t validBits);

        void* m_Device = nullptr;
        double m_TimestampPeriodUs = 0.0;
        uint32_t m_TimestampValidBits[LANE_COUNT] = {};
        bool m_LaneSupported[LANE_COUNT] = {};
        void* m_Pools[LANE_COUNT][FRAME_SLOT_COUNT] = {};
        FrameSlot m_FrameSlots[FRAME_SLOT_COUNT] = {};
        QueryValue m_QueryResults[LANE_COUNT][QUERY_COUNT] = {};
        uint32_t m_ActiveFrameSlot = INVALID_SLOT;
    };

    struct VansGpuScopeQuery
    {
        void* m_Cmd = nullptr;
        bool  m_Active = false;
        VansGpuQueueLane m_Lane = VansGpuQueueLane::Graphics;

        explicit VansGpuScopeQuery(
            void* cmd,
            const char* name,
            VansGpuQueueLane lane = VansGpuQueueLane::Graphics);
        ~VansGpuScopeQuery();
    };

    class VansProfiler
    {
    public:
        static constexpr uint64_t INVALID_FRAME_INDEX = VansCpuProfiler::INVALID_FRAME_INDEX;

        static VansProfiler& Get();

        void BeginFrame();
        void EndFrame();
        void MarkCurrentFrameHasRender();
        uint64_t GetActiveFrameIndex() const;

        void BeginRenderFrame(uint64_t frameIndex);
        void EndRenderFrame(uint64_t frameIndex);
        void SubmitGpuFrame(const VansGpuResolvedFrame& frame);

        void SetCaptureEnabled(bool enabled);
        bool IsCaptureEnabled() const;
        void SetPaused(bool paused);
        bool IsPaused() const;

        void RegisterCurrentThread(const char* name, uint32_t color = 0xff62c96bu);
        const ProfileFrame& GetTimeline() const;
        void PrintTimeline() const;
        void DumpFrameJson(const char* outputDir = "LOG") const;
        void StartStableCapture(const VansProfileCapture::Settings& settings);
        void CancelStableCapture();
        const VansProfileCapture& GetStableCapture() const { return m_StableCapture; }

    private:
        static constexpr uint32_t ASSEMBLY_COUNT = 8;
        static constexpr uint32_t GPU_RESULT_QUEUE_COUNT = 16;

        struct FrameAssembly
        {
            bool occupied = false;
            bool hasCpu = false;
            bool hasGpu = false;
            bool composed = false;
            uint64_t frameIndex = INVALID_FRAME_INDEX;
            ProfileFrame frame = {};
            VansGpuResolvedFrame gpu = {};
        };

        void ProcessCompletedFrames();
        FrameAssembly& AcquireAssembly(uint64_t frameIndex);
        void ComposeAssembly(FrameAssembly& assembly);
        bool TryPopGpuFrame(VansGpuResolvedFrame& frame);
        void PublishLatestReadyAssembly();
        static void BuildTrackEventRanges(ProfileFrame& frame);

        std::array<FrameAssembly, ASSEMBLY_COUNT> m_Assemblies{};
        int32_t m_PublishedAssembly = -1;
        uint64_t m_LastPublishedFrameIndex = INVALID_FRAME_INDEX;
        ProfileFrame m_EmptyFrame = {};

        mutable std::mutex m_GpuResultMutex;
        std::array<VansGpuResolvedFrame, GPU_RESULT_QUEUE_COUNT> m_GpuResultQueue{};
        uint32_t m_GpuResultHead = 0;
        uint32_t m_GpuResultCount = 0;

        uint64_t m_NextFrameIndex = 0;
        std::atomic<uint64_t> m_ActiveFrameIndex{ INVALID_FRAME_INDEX };
        std::atomic_bool m_CaptureRequested{ false };
        std::atomic_bool m_Paused{ false };
        VansProfileCapture m_StableCapture;
    };

    class VansProfilerFrameScope
    {
    public:
        explicit VansProfilerFrameScope(bool frameActive);
        ~VansProfilerFrameScope();

        VansProfilerFrameScope(const VansProfilerFrameScope&) = delete;
        VansProfilerFrameScope& operator=(const VansProfilerFrameScope&) = delete;

    private:
        bool m_FrameActive = false;
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
  #define VANS_GPU_SCOPE_LANE(cmd, name, lane) Vans::VansGpuScopeQuery VANS_PROFILE_CONCAT(_vans_gpu_scope_, __LINE__)((void*)(cmd), name, lane)
  #define VANS_PROFILER_PRINT()           Vans::VansProfiler::Get().PrintTimeline()
  #define VANS_PROFILER_DUMP_JSON()       Vans::VansProfiler::Get().DumpFrameJson()
#else
  #define VANS_PROFILE_THREAD(name)       /* no-op */
  #define VANS_PROFILE_SCOPE(name, cat)   /* no-op */
  #define VANS_PROFILE_WAIT(name)         /* no-op */
  #define VANS_CPU_SCOPE(name)            /* no-op */
  #define VANS_GPU_SCOPE(cmd, name)       /* no-op */
  #define VANS_GPU_SCOPE_LANE(cmd, name, lane) /* no-op */
  #define VANS_PROFILER_PRINT()           /* no-op */
  #define VANS_PROFILER_DUMP_JSON()       /* no-op */
#endif
