#include "VansProfiler.h"
#include "VansLog.h"

#if VANS_PROFILER_ENABLED

// Vulkan API — only needed in the .cpp, not the public header
#include "../../Graphics/Vulkan/VansVKFunctions.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>
#include <filesystem>

#ifdef _WIN32
  #include <Windows.h>   // QueryPerformanceCounter
#endif

using json = nlohmann::json;

// -----------------------------------------------------------------------
// Platform high-resolution clock
// -----------------------------------------------------------------------
namespace
{

#ifdef _WIN32
    static int64_t s_QpcFrequency = 0;

    inline void EnsureQpcFreq()
    {
        if (s_QpcFrequency == 0)
        {
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            s_QpcFrequency = freq.QuadPart;
        }
    }

    inline int64_t NowNs()
    {
        EnsureQpcFreq();
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        // Convert to nanoseconds: ticks * 1e9 / freq
        return static_cast<int64_t>(
            static_cast<double>(now.QuadPart) * 1e9 / static_cast<double>(s_QpcFrequency));
    }
#else
    inline int64_t NowNs()
    {
        auto tp = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
    }
#endif

    inline double NsToMs(int64_t ns) { return static_cast<double>(ns) * 1e-6; }

} // anonymous namespace

// =======================================================================
//  CpuProfiler
// =======================================================================

Vans::VansCpuProfiler& Vans::VansCpuProfiler::Get()
{
    static VansCpuProfiler instance;
    return instance;
}

void Vans::VansCpuProfiler::BeginFrame()
{
    m_StackTop    = 0;
    m_RecordCount = 0;
    m_FrameStartNs = NowNs();
}

void Vans::VansCpuProfiler::Push(const char* name)
{
    if (m_StackTop >= MAX_DEPTH) return;

    Entry& e  = m_Stack[m_StackTop];
    e.name    = name;
    e.startNs = NowNs();
    e.depth   = static_cast<uint8_t>(m_StackTop);
    m_StackTop++;
}

void Vans::VansCpuProfiler::Pop()
{
    if (m_StackTop <= 0) return;
    m_StackTop--;

    const Entry& e = m_Stack[m_StackTop];
    int64_t endNs = NowNs();

    if (m_RecordCount < MAX_RECORDS)
    {
        ProfileRecord& r = m_Records[m_RecordCount++];
        r.name       = e.name;
        r.startMs    = NsToMs(e.startNs - m_FrameStartNs);
        r.durationMs = NsToMs(endNs - e.startNs);
        r.depth      = e.depth;
        r.isGpu      = false;
    }
}

void Vans::VansCpuProfiler::CollectInto(ProfileFrame& frame) const
{
    for (int i = 0; i < m_RecordCount && frame.recordCount < ProfileFrame::MAX_RECORDS; i++)
    {
        frame.records[frame.recordCount++] = m_Records[i];
    }
}

// =======================================================================
//  GpuProfiler
// =======================================================================

Vans::VansGpuProfiler& Vans::VansGpuProfiler::Get()
{
    static VansGpuProfiler instance;
    return instance;
}

void Vans::VansGpuProfiler::Init(void* device, void* physDevice, uint32_t queueFamily)
{
    if (m_Pools[0] != nullptr) return;  // already initialized

    VkDevice         vkDev  = static_cast<VkDevice>(device);
    VkPhysicalDevice vkPhys = static_cast<VkPhysicalDevice>(physDevice);

    m_Device = device;

    // Read timestamp period
    VkPhysicalDeviceProperties props;
    VansGraphics::vkGetPhysicalDeviceProperties(vkPhys, &props);
    m_TimestampPeriodMs = static_cast<double>(props.limits.timestampPeriod) * 1e-6; // ns -> ms

    if (props.limits.timestampComputeAndGraphics == VK_FALSE)
    {
        VANS_LOG_WARN("[VansProfiler] timestampComputeAndGraphics is FALSE — GPU timestamps may be inaccurate");
    }

    // Create double-buffered query pools
    VkQueryPoolCreateInfo ci{};
    ci.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    ci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    ci.queryCount = MAX_GPU_QUERIES * 2;

    for (int i = 0; i < POOL_COUNT; i++)
    {
        VkQueryPool pool = VK_NULL_HANDLE;
        VkResult res = VansGraphics::vkCreateQueryPool(vkDev, &ci, nullptr, &pool);
        if (res != VK_SUCCESS)
        {
            VANS_LOG_ERROR("[VansProfiler] Failed to create GPU query pool " << i << ": " << res);
            m_Pools[i] = nullptr;
        }
        else
        {
            m_Pools[i] = pool;
        }
    }

    m_WriteIdx         = 0;
    m_HasPreviousFrame = false;
    VANS_LOG("[VansProfiler] GPU query pools created (2x" << MAX_GPU_QUERIES << " slots, double-buffered)");
}

void Vans::VansGpuProfiler::Destroy()
{
    if (m_Device == nullptr) return;
    VkDevice vkDev = static_cast<VkDevice>(m_Device);
    for (int i = 0; i < POOL_COUNT; i++)
    {
        if (m_Pools[i] != nullptr)
        {
            VansGraphics::vkDestroyQueryPool(vkDev,
                               static_cast<VkQueryPool>(m_Pools[i]), nullptr);
            m_Pools[i] = nullptr;
        }
    }
    VANS_LOG("[VansProfiler] GPU query pools destroyed");
}

void Vans::VansGpuProfiler::BeginFrame(void* cmd)
{
    uint32_t cur = m_WriteIdx;
    m_SlotCount[cur]  = 0;
    m_NextQuery[cur]  = 0;
    m_StackDepth      = 0;

    if (m_Pools[cur] != nullptr)
    {
        VansGraphics::vkCmdResetQueryPool(static_cast<VkCommandBuffer>(cmd),
                            static_cast<VkQueryPool>(m_Pools[cur]), 0, MAX_GPU_QUERIES * 2);
    }
}

void Vans::VansGpuProfiler::Push(void* cmd, const char* name)
{
    uint32_t cur = m_WriteIdx;
    if (m_Pools[cur] == nullptr) return;
    if (m_SlotCount[cur] >= MAX_GPU_QUERIES) return;

    uint32_t beginIdx = m_NextQuery[cur]++;
    uint32_t endIdx   = m_NextQuery[cur]++;   // reserve end slot

    Slot& s     = m_Slots[cur][m_SlotCount[cur]++];
    s.name      = name;
    s.beginSlot = beginIdx;
    s.endSlot   = endIdx;
    s.depth     = static_cast<uint8_t>(m_StackDepth);

    m_StackDepth++;

    // Insert Vulkan debug label so the region is visible in Nsight / RenderDoc
#ifdef _DEBUG
    if (VansGraphics::vkCmdBeginDebugUtilsLabelEXT)
    {
        VkDebugUtilsLabelEXT labelInfo = {};
        labelInfo.sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        labelInfo.pLabelName = name;
        labelInfo.color[0] = 0.2f; labelInfo.color[1] = 0.6f;
        labelInfo.color[2] = 1.0f; labelInfo.color[3] = 1.0f;
        VansGraphics::vkCmdBeginDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(cmd), &labelInfo);
    }
#endif

    VansGraphics::vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(cmd),
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        static_cast<VkQueryPool>(m_Pools[cur]), beginIdx);
}

void Vans::VansGpuProfiler::Pop(void* cmd)
{
    uint32_t cur = m_WriteIdx;
    if (m_Pools[cur] == nullptr) return;
    if (m_StackDepth <= 0) return;
    m_StackDepth--;

    // Find the most recent slot at this depth and fill its end timestamp
    for (int i = m_SlotCount[cur] - 1; i >= 0; i--)
    {
        if (m_Slots[cur][i].depth == static_cast<uint8_t>(m_StackDepth) && m_Slots[cur][i].endSlot < MAX_GPU_QUERIES * 2)
        {
            VansGraphics::vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(cmd),
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                static_cast<VkQueryPool>(m_Pools[cur]), m_Slots[cur][i].endSlot);

            // Close matching Vulkan debug label for Nsight / RenderDoc
#ifdef _DEBUG
            if (VansGraphics::vkCmdEndDebugUtilsLabelEXT)
            {
                VansGraphics::vkCmdEndDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(cmd));
            }
#endif
            break;
        }
    }
}

void Vans::VansGpuProfiler::EndFrame()
{
    // Placeholder for optional pipeline barrier before readback
}

void Vans::VansGpuProfiler::Resolve(void* device)
{
    // ── Read back the PREVIOUS frame's pool (GPU has finished it) ──
    uint32_t prev = m_WriteIdx ^ 1;
    m_RecordCount = 0;

    if (!m_HasPreviousFrame || m_Pools[prev] == nullptr || m_SlotCount[prev] == 0)
    {
        // First frame or nothing recorded — just flip and return
        m_HasPreviousFrame = true;
        m_WriteIdx ^= 1;
        return;
    }

    VkDevice    vkDev  = static_cast<VkDevice>(device);
    VkQueryPool vkPool = static_cast<VkQueryPool>(m_Pools[prev]);

    uint32_t queryCount = m_NextQuery[prev];
    if (queryCount == 0) { m_WriteIdx ^= 1; return; }

    // No WAIT_BIT — previous frame should be done; if not, skip gracefully
    VkResult res = VansGraphics::vkGetQueryPoolResults(
        vkDev, vkPool,
        0, queryCount,
        queryCount * sizeof(uint64_t), m_RawResults,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT
    );

    if (res != VK_SUCCESS && res != VK_NOT_READY)
    {
        VANS_LOG_WARN("[VansProfiler] vkGetQueryPoolResults returned " << res);
        m_WriteIdx ^= 1;
        return;
    }

    if (res == VK_NOT_READY)
    {
        // GPU not done yet — skip this readback
        m_WriteIdx ^= 1;
        return;
    }

    // Find the minimum timestamp to use as a GPU frame origin
    uint64_t gpuOrigin = UINT64_MAX;
    for (int i = 0; i < m_SlotCount[prev]; i++)
    {
        uint64_t t = m_RawResults[m_Slots[prev][i].beginSlot];
        if (t < gpuOrigin) gpuOrigin = t;
    }

    m_RecordCount = 0;
    for (int i = 0; i < m_SlotCount[prev]; i++)
    {
        const Slot& s = m_Slots[prev][i];
        uint64_t rawBegin = m_RawResults[s.beginSlot];
        uint64_t rawEnd   = m_RawResults[s.endSlot];

        ProfileRecord& r = m_Records[m_RecordCount++];
        r.name       = s.name;
        r.startMs    = static_cast<double>(rawBegin - gpuOrigin) * m_TimestampPeriodMs;
        r.durationMs = static_cast<double>(rawEnd - rawBegin)    * m_TimestampPeriodMs;
        r.depth      = s.depth;
        r.isGpu      = true;
    }

    // ── Flip: next frame writes to the other pool ──
    m_WriteIdx ^= 1;
}

void Vans::VansGpuProfiler::CollectInto(ProfileFrame& frame) const
{
    for (int i = 0; i < m_RecordCount && frame.recordCount < ProfileFrame::MAX_RECORDS; i++)
    {
        frame.records[frame.recordCount++] = m_Records[i];
    }
}

// =======================================================================
//  Profiler Facade
// =======================================================================

Vans::VansProfiler& Vans::VansProfiler::Get()
{
    static VansProfiler instance;
    return instance;
}

void Vans::VansProfiler::BeginFrame()
{
    m_FrameStartNs = NowNs();
    VansCpuProfiler::Get().BeginFrame();
}

void Vans::VansProfiler::EndFrame(void* device)
{
    // 1. Resolve GPU timestamps
    VansGpuProfiler::Get().Resolve(device);

    // 2. Reset frame
    m_Frame = {};
    m_Frame.frameIndex = m_FrameIndex;

    // 3. Collect records
    VansCpuProfiler::Get().CollectInto(m_Frame);
    VansGpuProfiler::Get().CollectInto(m_Frame);

    // 4. Compute total frame time
    int64_t frameEndNs   = NowNs();
    m_Frame.frameDurationMs = NsToMs(frameEndNs - m_FrameStartNs);
    m_Frame.fps = (m_Frame.frameDurationMs > 0.0) ? (1000.0 / m_Frame.frameDurationMs) : 0.0;

    // 5. Sort by start time (CPU records first, then GPU)
    std::sort(m_Frame.records, m_Frame.records + m_Frame.recordCount,
        [](const ProfileRecord& a, const ProfileRecord& b)
        {
            if (a.isGpu != b.isGpu) return !a.isGpu; // CPU first
            return a.startMs < b.startMs;
        });

    m_FrameIndex++;
}

void Vans::VansProfiler::PrintTimeline() const
{
    const ProfileFrame& f = m_Frame;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "--- Frame " << f.frameIndex
        << "  total: " << f.frameDurationMs << " ms"
        << "  FPS: " << f.fps << " ---\n";

    for (uint32_t i = 0; i < f.recordCount; i++)
    {
        const ProfileRecord& r = f.records[i];
        // Indentation
        for (uint8_t d = 0; d < r.depth; d++) oss << "  ";

        oss << (r.isGpu ? "[GPU] " : "[CPU] ")
            << r.name
            << "  " << r.startMs << " ms  + " << r.durationMs << " ms\n";
    }
    oss << "---";

    VANS_LOG(oss.str());
}

void Vans::VansProfiler::DumpFrameJson(const char* outputDir) const
{
    const ProfileFrame& f = m_Frame;

    // Build JSON
    json jFrame;
    jFrame["frameIndex"]      = f.frameIndex;
    jFrame["frameDurationMs"] = f.frameDurationMs;
    jFrame["fps"]             = f.fps;

    json jRecords = json::array();
    for (uint32_t i = 0; i < f.recordCount; i++)
    {
        const ProfileRecord& r = f.records[i];
        json jr;
        jr["name"]       = r.name ? r.name : "unknown";
        jr["startMs"]    = r.startMs;
        jr["durationMs"] = r.durationMs;
        jr["depth"]      = r.depth;
        jr["isGpu"]      = r.isGpu;
        jRecords.push_back(jr);
    }
    jFrame["records"] = jRecords;

    // Ensure directory exists
    try
    {
        std::filesystem::create_directories(outputDir);
    }
    catch (...) {}

    // Write file
    std::ostringstream filename;
    filename << outputDir << "/profiler_frame_"
             << std::setfill('0') << std::setw(6) << f.frameIndex
             << ".json";

    std::ofstream out(filename.str());
    if (out.is_open())
    {
        out << jFrame.dump(2);
        out.close();
    }
}

#endif // VANS_PROFILER_ENABLED
