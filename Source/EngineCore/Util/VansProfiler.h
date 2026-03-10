#pragma once
// -----------------------------------------------------------------------
// VansProfiler  —  Lightweight CPU + GPU profiling system.
//
// Platform-independent header — no Vulkan/graphics API includes.
// The GPU profiler accepts opaque void* handles; the renderer passes
// its native handles (VkDevice, VkCommandBuffer, etc.) and the .cpp
// casts them internally.
//
// Toggle:  VANS_PROFILER_ENABLED  (1 = on, 0 = compiled away)
//
// Usage:
//   VANS_CPU_SCOPE("Physics::Update");
//   VANS_GPU_SCOPE(cmd, "Shadow Pass");        // cmd = void* (VkCommandBuffer)
//   VANS_PROFILER_BEGIN_FRAME();
//   VANS_PROFILER_END_FRAME(device);            // device = void* (VkDevice)
//   VANS_PROFILER_PRINT();
//
// All storage is fixed-size — zero heap allocations per frame.
// -----------------------------------------------------------------------

#ifndef VANS_PROFILER_ENABLED
  #define VANS_PROFILER_ENABLED 1   // default ON — set 0 for release builds
#endif

#include <cstdint>
#include <cstring>

// -----------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------
namespace Vans
{

// ─── Shared record for both CPU and GPU ─────────────────────────────
struct ProfileRecord
{
    const char* name       = nullptr;
    double      startMs    = 0.0;
    double      durationMs = 0.0;
    uint8_t     depth      = 0;
    bool        isGpu      = false;
};

// ─── One assembled frame ────────────────────────────────────────────
struct ProfileFrame
{
    uint32_t      frameIndex      = 0;
    double        frameDurationMs = 0.0;
    double        fps             = 0.0;

    static constexpr uint32_t MAX_RECORDS = 256;
    ProfileRecord records[MAX_RECORDS];
    uint32_t      recordCount = 0;
};

// =====================================================================
//  CpuProfiler
// =====================================================================
class VansCpuProfiler
{
public:
    static VansCpuProfiler& Get();

    void BeginFrame();
    void Push(const char* name);
    void Pop();
    void CollectInto(ProfileFrame& frame) const;

private:
    struct Entry
    {
        const char* name    = nullptr;
        int64_t     startNs = 0;
        uint8_t     depth   = 0;
    };

    static constexpr int MAX_DEPTH   = 32;
    static constexpr int MAX_RECORDS = 128;

    Entry         m_Stack[MAX_DEPTH]     = {};
    int           m_StackTop             = 0;
    ProfileRecord m_Records[MAX_RECORDS] = {};
    int           m_RecordCount          = 0;
    int64_t       m_FrameStartNs         = 0;
};

// ─── RAII CPU scope ─────────────────────────────────────────────────
struct VansCpuScopeTimer
{
    explicit VansCpuScopeTimer(const char* name) { VansCpuProfiler::Get().Push(name); }
    ~VansCpuScopeTimer()                         { VansCpuProfiler::Get().Pop(); }
};

// =====================================================================
//  GpuProfiler  (platform-agnostic interface — void* handles)
//
//  The renderer passes native GPU handles as void*:
//    Init(VkDevice, VkPhysicalDevice, queueFamily)
//    BeginFrame / Push / Pop  take VkCommandBuffer as void*
//    Resolve takes VkDevice as void*
// =====================================================================
class VansGpuProfiler
{
public:
    static VansGpuProfiler& Get();

    /// Initialize with native GPU handles (e.g. VkDevice, VkPhysicalDevice).
    void Init(void* device, void* physDevice, uint32_t queueFamily);
    void Destroy();

    /// Call at start of command buffer recording.
    void BeginFrame(void* cmd);
    void Push(void* cmd, const char* name);
    void Pop(void* cmd);
    void EndFrame();                              // no-op placeholder
    /// Read back GPU timestamps (call after submit/wait).
    void Resolve(void* device);
    void CollectInto(ProfileFrame& frame) const;

    bool IsInitialized() const { return m_Pools[0] != nullptr; }

private:
    static constexpr int MAX_GPU_QUERIES = 64;
    static constexpr int POOL_COUNT      = 2;   // double-buffered

    void*         m_Pools[POOL_COUNT]    = {};   // native query pool handles
    void*         m_Device               = nullptr;
    double        m_TimestampPeriodMs    = 0.0;
    uint32_t      m_WriteIdx             = 0;    // toggles 0/1 each frame
    bool          m_HasPreviousFrame     = false; // skip first-frame readback

    struct Slot
    {
        const char* name      = nullptr;
        uint32_t    beginSlot = 0;
        uint32_t    endSlot   = 0;
        uint8_t     depth     = 0;
    };

    // Per-pool recording state
    Slot          m_Slots[POOL_COUNT][MAX_GPU_QUERIES] = {};
    int           m_SlotCount[POOL_COUNT]              = {};
    uint32_t      m_NextQuery[POOL_COUNT]              = {};
    int           m_StackDepth                         = 0;

    // Readback (from previous frame's pool)
    uint64_t      m_RawResults[MAX_GPU_QUERIES * 2]    = {};
    ProfileRecord m_Records[MAX_GPU_QUERIES]            = {};
    int           m_RecordCount                        = 0;
};

// ─── RAII GPU scope ─────────────────────────────────────────────────
struct VansGpuScopeQuery
{
    void* m_Cmd;
    explicit VansGpuScopeQuery(void* cmd, const char* name)
        : m_Cmd(cmd) { VansGpuProfiler::Get().Push(cmd, name); }
    ~VansGpuScopeQuery() { VansGpuProfiler::Get().Pop(m_Cmd); }
};

// =====================================================================
//  Profiler Facade
// =====================================================================
class VansProfiler
{
public:
    static VansProfiler& Get();

    void BeginFrame();
    /// End frame — pass native device handle (e.g. VkDevice) as void*.
    void EndFrame(void* device);

    const ProfileFrame& GetTimeline() const { return m_Frame; }

    /// Print indented timeline to log
    void PrintTimeline() const;

    /// Dump frame data to JSON file (LOG/profiler_frame_XXXX.json)
    void DumpFrameJson(const char* outputDir = "LOG") const;

private:
    ProfileFrame m_Frame      = {};
    uint32_t     m_FrameIndex = 0;
    int64_t      m_FrameStartNs = 0;
};

} // namespace Vans

// -----------------------------------------------------------------------
// Convenience macros
// -----------------------------------------------------------------------
#if VANS_PROFILER_ENABLED
  #define VANS_CPU_SCOPE(name)            Vans::VansCpuScopeTimer _vans_cpu_scope_##__LINE__(name)
  #define VANS_GPU_SCOPE(cmd, name)       Vans::VansGpuScopeQuery _vans_gpu_scope_##__LINE__((void*)(cmd), name)
  #define VANS_PROFILER_BEGIN_FRAME()     Vans::VansProfiler::Get().BeginFrame()
  #define VANS_PROFILER_END_FRAME(dev)    Vans::VansProfiler::Get().EndFrame((void*)(dev))
  #define VANS_PROFILER_PRINT()           Vans::VansProfiler::Get().PrintTimeline()
  #define VANS_PROFILER_DUMP_JSON()       Vans::VansProfiler::Get().DumpFrameJson()
#else
  #define VANS_CPU_SCOPE(name)            /* no-op */
  #define VANS_GPU_SCOPE(cmd, name)       /* no-op */
  #define VANS_PROFILER_BEGIN_FRAME()     /* no-op */
  #define VANS_PROFILER_END_FRAME(dev)    /* no-op */
  #define VANS_PROFILER_PRINT()           /* no-op */
  #define VANS_PROFILER_DUMP_JSON()       /* no-op */
#endif
