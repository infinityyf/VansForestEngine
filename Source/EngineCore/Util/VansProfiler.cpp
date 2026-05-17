#include "VansProfiler.h"
#include "VansLog.h"

#if VANS_PROFILER_ENABLED

#include "../../Graphics/Vulkan/VansVKFunctions.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
  #include <Windows.h>
#endif

using json = nlohmann::json;

namespace
{
    std::mutex s_CpuProfilerMutex;

#ifdef _WIN32
    static int64_t s_QpcFrequency = 0;

    void EnsureQpcFrequency()
    {
        if (s_QpcFrequency == 0)
        {
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            s_QpcFrequency = freq.QuadPart;
        }
    }

    int64_t NowNs()
    {
        EnsureQpcFrequency();
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return static_cast<int64_t>(static_cast<double>(now.QuadPart) * 1e9 / static_cast<double>(s_QpcFrequency));
    }
#else
    int64_t NowNs()
    {
        auto tp = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
    }
#endif

    double NsToUs(int64_t ns)
    {
        return static_cast<double>(ns) * 1e-3;
    }

    const char* CategoryName(Vans::ProfileCategory category)
    {
        switch (category)
        {
        case Vans::ProfileCategory::Frame:         return "Frame";
        case Vans::ProfileCategory::Editor:        return "Editor";
        case Vans::ProfileCategory::Script:        return "Script";
        case Vans::ProfileCategory::Physics:       return "Physics";
        case Vans::ProfileCategory::Animation:     return "Animation";
        case Vans::ProfileCategory::Particles:     return "Particles";
        case Vans::ProfileCategory::Audio:         return "Audio";
        case Vans::ProfileCategory::Video:         return "Video";
        case Vans::ProfileCategory::RuntimeUI:     return "RuntimeUI";
        case Vans::ProfileCategory::RenderPrepare: return "RenderPrepare";
        case Vans::ProfileCategory::CommandRecord: return "CommandRecord";
        case Vans::ProfileCategory::VulkanSubmit:  return "VulkanSubmit";
        case Vans::ProfileCategory::GPU:           return "GPU";
        case Vans::ProfileCategory::JobSystem:     return "JobSystem";
        case Vans::ProfileCategory::Wait:          return "Wait";
        case Vans::ProfileCategory::IO:            return "IO";
        default:                                   return "Other";
        }
    }

    const char* TrackTypeName(Vans::ProfileTrackType type)
    {
        switch (type)
        {
        case Vans::ProfileTrackType::CpuThread: return "CpuThread";
        case Vans::ProfileTrackType::GpuQueue:  return "GpuQueue";
        case Vans::ProfileTrackType::Marker:    return "Marker";
        default:                                return "Unknown";
        }
    }
}

Vans::VansCpuProfiler& Vans::VansCpuProfiler::Get()
{
    static VansCpuProfiler instance;
    return instance;
}

Vans::VansCpuProfiler::ThreadContext& Vans::VansCpuProfiler::GetThreadContext()
{
    thread_local ThreadContext context;
    return context;
}

uint64_t Vans::VansCpuProfiler::GetCurrentThreadIdValue()
{
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

void Vans::VansCpuProfiler::CopyText(char* dst, uint32_t dstSize, const char* src) const
{
    if (!dst || dstSize == 0)
        return;

    const char* text = src ? src : "?";
    std::snprintf(dst, dstSize, "%s", text);
}

void Vans::VansCpuProfiler::BeginFrame(uint64_t frameIndex, int64_t frameStartNs)
{
    std::lock_guard<std::mutex> lock(s_CpuProfilerMutex);
    m_EventCount = 0;
    m_NextEventId = 1;
    m_FrameIndex = frameIndex;
    m_FrameStartNs = frameStartNs;
    m_FrameActive = true;
    m_Overflow = false;
}

void Vans::VansCpuProfiler::EndFrame(ProfileFrame& frame, double frameDurationUs)
{
    std::lock_guard<std::mutex> lock(s_CpuProfilerMutex);

    frame.frameIndex = m_FrameIndex;
    frame.frameDurationUs = frameDurationUs;
    frame.fps = frameDurationUs > 0.0 ? 1000000.0 / frameDurationUs : 0.0;
    frame.trackCount = std::min<uint32_t>(m_TrackCount, ProfileFrame::MAX_TRACKS);
    frame.eventCount = std::min<uint32_t>(m_EventCount, ProfileFrame::MAX_EVENTS);
    frame.overflow = m_Overflow;

    for (uint32_t i = 0; i < frame.trackCount; ++i)
        frame.tracks[i] = m_Tracks[i];

    for (uint32_t i = 0; i < frame.eventCount; ++i)
        frame.events[i] = m_Events[i];

    m_FrameActive = false;
}

uint32_t Vans::VansCpuProfiler::RegisterTrack(uint64_t threadId, const char* name, ProfileTrackType type, uint32_t color)
{
    std::lock_guard<std::mutex> lock(s_CpuProfilerMutex);

    for (uint32_t i = 0; i < m_TrackCount; ++i)
    {
        if (m_Tracks[i].threadId == threadId && m_Tracks[i].type == type)
        {
            CopyText(m_Tracks[i].name, sizeof(m_Tracks[i].name), name);
            if (color != 0)
                m_Tracks[i].color = color;
            return m_Tracks[i].trackId;
        }
    }

    if (m_TrackCount >= ProfileFrame::MAX_TRACKS)
    {
        m_Overflow = true;
        return 0;
    }

    ProfileTrack& track = m_Tracks[m_TrackCount];
    track.trackId = m_TrackCount + 1;
    track.type = type;
    track.threadId = threadId;
    track.color = color;
    CopyText(track.name, sizeof(track.name), name);
    ++m_TrackCount;
    return track.trackId;
}

uint32_t Vans::VansCpuProfiler::RegisterCurrentThread(const char* name, uint32_t color)
{
    ThreadContext& context = GetThreadContext();
    const uint64_t threadId = GetCurrentThreadIdValue();
    context.trackId = RegisterTrack(threadId, name, ProfileTrackType::CpuThread, color);
    return context.trackId;
}

uint32_t Vans::VansCpuProfiler::AllocateEventId()
{
    std::lock_guard<std::mutex> lock(s_CpuProfilerMutex);
    return m_NextEventId++;
}

void Vans::VansCpuProfiler::AddEvent(const ProfileEvent& event)
{
    std::lock_guard<std::mutex> lock(s_CpuProfilerMutex);
    if (m_EventCount >= ProfileFrame::MAX_EVENTS)
    {
        m_Overflow = true;
        return;
    }

    m_Events[m_EventCount++] = event;
}

bool Vans::VansCpuProfiler::Push(const char* name, ProfileCategory category, uint16_t flags)
{
    ThreadContext& context = GetThreadContext();
    if (context.trackId == 0)
        RegisterCurrentThread("Thread");

    if (context.depth >= MAX_DEPTH)
        return false;

    StackEntry& entry = context.stack[context.depth];
    CopyText(entry.name, sizeof(entry.name), name);
    entry.category = category;
    entry.flags = flags;
    entry.depth = static_cast<uint16_t>(context.depth);
    entry.parentEventId = context.depth > 0 ? context.stack[context.depth - 1].eventId : 0;
    entry.trackId = context.trackId;
    entry.startNs = NowNs();

    {
        std::lock_guard<std::mutex> lock(s_CpuProfilerMutex);
        if (!m_FrameActive || m_FrameStartNs == 0)
            return false;
        entry.eventId = m_NextEventId++;
        entry.frameIndex = m_FrameIndex;
    }

    ++context.depth;
    return true;
}

void Vans::VansCpuProfiler::Pop()
{
    ThreadContext& context = GetThreadContext();
    if (context.depth == 0)
        return;

    --context.depth;
    const StackEntry& entry = context.stack[context.depth];
    const int64_t endNs = NowNs();

    uint64_t frameIndex = 0;
    int64_t frameStartNs = 0;
    {
        std::lock_guard<std::mutex> lock(s_CpuProfilerMutex);
        if (!m_FrameActive)
            return;
        frameIndex = m_FrameIndex;
        frameStartNs = m_FrameStartNs;
    }

    if (entry.frameIndex != frameIndex || entry.startNs < frameStartNs)
        return;

    ProfileEvent event;
    event.eventId = entry.eventId;
    event.parentEventId = entry.parentEventId;
    event.trackId = entry.trackId;
    event.category = entry.category;
    CopyText(event.name, sizeof(event.name), entry.name);
    event.startUs = NsToUs(entry.startNs - frameStartNs);
    event.endUs = NsToUs(endNs - frameStartNs);
    event.depth = entry.depth;
    event.flags = entry.flags;
    AddEvent(event);
}

Vans::VansCpuScopeTimer::VansCpuScopeTimer(const char* name, ProfileCategory category, uint16_t flags)
{
    m_Active = VansCpuProfiler::Get().Push(name, category, flags);
}

Vans::VansCpuScopeTimer::~VansCpuScopeTimer()
{
    if (m_Active)
        VansCpuProfiler::Get().Pop();
}

Vans::VansGpuProfiler& Vans::VansGpuProfiler::Get()
{
    static VansGpuProfiler instance;
    return instance;
}

void Vans::VansGpuProfiler::CopyText(char* dst, uint32_t dstSize, const char* src) const
{
    if (!dst || dstSize == 0)
        return;

    const char* text = src ? src : "?";
    std::snprintf(dst, dstSize, "%s", text);
}

void Vans::VansGpuProfiler::Init(void* device, void* physDevice, uint32_t /*queueFamily*/)
{
    if (m_Pools[0] != nullptr)
        return;

    VkDevice vkDev = static_cast<VkDevice>(device);
    VkPhysicalDevice vkPhys = static_cast<VkPhysicalDevice>(physDevice);
    m_Device = device;

    VkPhysicalDeviceProperties props;
    VansGraphics::vkGetPhysicalDeviceProperties(vkPhys, &props);
    m_TimestampPeriodMs = static_cast<double>(props.limits.timestampPeriod) * 1e-6;

    VkQueryPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    ci.queryCount = MAX_GPU_QUERIES * 2;

    for (int i = 0; i < POOL_COUNT; ++i)
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

    m_WriteIdx = 0;
    m_HasPreviousFrame = false;
}

void Vans::VansGpuProfiler::Destroy()
{
    if (m_Device == nullptr)
        return;

    VkDevice vkDev = static_cast<VkDevice>(m_Device);
    for (int i = 0; i < POOL_COUNT; ++i)
    {
        if (m_Pools[i] != nullptr)
        {
            VansGraphics::vkDestroyQueryPool(vkDev, static_cast<VkQueryPool>(m_Pools[i]), nullptr);
            m_Pools[i] = nullptr;
        }
    }
}

void Vans::VansGpuProfiler::BeginFrame(void* cmd)
{
    if (cmd == nullptr || m_Pools[0] == nullptr || m_Pools[1] == nullptr)
        return;

    uint32_t cur = m_WriteIdx;
    m_SlotCount[cur] = 0;
    m_NextQuery[cur] = 0;
    m_StackDepth = 0;

    if (m_Pools[cur] != nullptr)
    {
        VansGraphics::vkCmdResetQueryPool(static_cast<VkCommandBuffer>(cmd), static_cast<VkQueryPool>(m_Pools[cur]), 0, MAX_GPU_QUERIES * 2);
    }
}

void Vans::VansGpuProfiler::Push(void* cmd, const char* name)
{
    if (cmd == nullptr || m_Pools[0] == nullptr || m_Pools[1] == nullptr)
        return;

    uint32_t cur = m_WriteIdx;
    if (m_Pools[cur] == nullptr || m_SlotCount[cur] >= MAX_GPU_QUERIES || m_NextQuery[cur] + 1 >= MAX_GPU_QUERIES * 2)
        return;

    uint32_t beginIdx = m_NextQuery[cur]++;
    uint32_t endIdx = m_NextQuery[cur]++;

    Slot& slot = m_Slots[cur][m_SlotCount[cur]++];
    CopyText(slot.name, sizeof(slot.name), name);
    slot.beginSlot = beginIdx;
    slot.endSlot = endIdx;
    slot.depth = static_cast<uint16_t>(m_StackDepth);
    ++m_StackDepth;

#ifdef _DEBUG
    if (VansGraphics::vkCmdBeginDebugUtilsLabelEXT)
    {
        VkDebugUtilsLabelEXT labelInfo = {};
        labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        labelInfo.pLabelName = name;
        labelInfo.color[0] = 0.2f;
        labelInfo.color[1] = 0.6f;
        labelInfo.color[2] = 1.0f;
        labelInfo.color[3] = 1.0f;
        VansGraphics::vkCmdBeginDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(cmd), &labelInfo);
    }
#endif

    VansGraphics::vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(cmd), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, static_cast<VkQueryPool>(m_Pools[cur]), beginIdx);
}

void Vans::VansGpuProfiler::Pop(void* cmd)
{
    if (cmd == nullptr || m_Pools[0] == nullptr || m_Pools[1] == nullptr)
        return;

    uint32_t cur = m_WriteIdx;
    if (m_Pools[cur] == nullptr || m_StackDepth <= 0)
        return;

    --m_StackDepth;
    for (int i = m_SlotCount[cur] - 1; i >= 0; --i)
    {
        if (m_Slots[cur][i].depth == static_cast<uint16_t>(m_StackDepth))
        {
            VansGraphics::vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(cmd), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<VkQueryPool>(m_Pools[cur]), m_Slots[cur][i].endSlot);
#ifdef _DEBUG
            if (VansGraphics::vkCmdEndDebugUtilsLabelEXT)
                VansGraphics::vkCmdEndDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(cmd));
#endif
            break;
        }
    }
}

void Vans::VansGpuProfiler::EndFrame()
{
}

void Vans::VansGpuProfiler::Resolve(void* device)
{
    uint32_t prev = m_WriteIdx ^ 1;
    m_EventCount = 0;

    if (!m_HasPreviousFrame || m_Pools[prev] == nullptr || m_SlotCount[prev] == 0)
    {
        m_HasPreviousFrame = true;
        m_WriteIdx ^= 1;
        return;
    }

    VkDevice vkDev = static_cast<VkDevice>(device);
    VkQueryPool vkPool = static_cast<VkQueryPool>(m_Pools[prev]);
    uint32_t queryCount = m_NextQuery[prev];
    if (queryCount == 0)
    {
        m_WriteIdx ^= 1;
        return;
    }

    VkResult res = VansGraphics::vkGetQueryPoolResults(
        vkDev, vkPool, 0, queryCount,
        queryCount * sizeof(uint64_t), m_RawResults, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

    if (res != VK_SUCCESS)
    {
        m_WriteIdx ^= 1;
        return;
    }

    uint64_t gpuOrigin = UINT64_MAX;
    for (int i = 0; i < m_SlotCount[prev]; ++i)
        gpuOrigin = std::min<uint64_t>(gpuOrigin, m_RawResults[m_Slots[prev][i].beginSlot]);

    for (int i = 0; i < m_SlotCount[prev] && m_EventCount < MAX_GPU_QUERIES; ++i)
    {
        const Slot& slot = m_Slots[prev][i];
        uint64_t rawBegin = m_RawResults[slot.beginSlot];
        uint64_t rawEnd = m_RawResults[slot.endSlot];

        ProfileEvent& event = m_Events[m_EventCount++];
        event.eventId = static_cast<uint32_t>(i + 1);
        event.parentEventId = 0;
        event.trackId = 0;
        event.category = ProfileCategory::GPU;
        CopyText(event.name, sizeof(event.name), slot.name);
        event.startUs = static_cast<double>(rawBegin - gpuOrigin) * m_TimestampPeriodMs * 1000.0;
        event.endUs = static_cast<double>(rawEnd - gpuOrigin) * m_TimestampPeriodMs * 1000.0;
        event.depth = slot.depth;
        event.flags = ProfileEventFlagGpu;
    }

    m_WriteIdx ^= 1;
}

void Vans::VansGpuProfiler::CollectInto(ProfileFrame& frame) const
{
    uint32_t gpuTrackId = 0;
    for (uint32_t i = 0; i < frame.trackCount; ++i)
    {
        if (frame.tracks[i].type == ProfileTrackType::GpuQueue)
        {
            gpuTrackId = frame.tracks[i].trackId;
            break;
        }
    }

    if (gpuTrackId == 0 && frame.trackCount < ProfileFrame::MAX_TRACKS)
    {
        ProfileTrack& track = frame.tracks[frame.trackCount];
        track.trackId = frame.trackCount + 1;
        track.type = ProfileTrackType::GpuQueue;
        track.threadId = 1;
        track.color = 0xff7a8cffu;
        std::snprintf(track.name, sizeof(track.name), "%s", "GPU Graphics Queue");
        gpuTrackId = track.trackId;
        ++frame.trackCount;
    }

    for (int i = 0; i < m_EventCount && frame.eventCount < ProfileFrame::MAX_EVENTS; ++i)
    {
        ProfileEvent event = m_Events[i];
        event.trackId = gpuTrackId;
        frame.events[frame.eventCount++] = event;
    }
}

Vans::VansGpuScopeQuery::VansGpuScopeQuery(void* cmd, const char* name)
    : m_Cmd(cmd)
{
    if (cmd != nullptr && VansGpuProfiler::Get().IsInitialized())
    {
        VansGpuProfiler::Get().Push(cmd, name);
        m_Active = true;
    }
}

Vans::VansGpuScopeQuery::~VansGpuScopeQuery()
{
    if (m_Active)
        VansGpuProfiler::Get().Pop(m_Cmd);
}

Vans::VansProfiler& Vans::VansProfiler::Get()
{
    static VansProfiler instance;
    return instance;
}

void Vans::VansProfiler::BeginFrame()
{
    m_FrameStartNs = NowNs();
    RegisterCurrentThread("Main Thread", 0xff62c96bu);
    VansCpuProfiler::Get().BeginFrame(m_FrameIndex, m_FrameStartNs);
}

void Vans::VansProfiler::EndFrame(void* device)
{
    VansGpuProfiler::Get().Resolve(device);

    int64_t frameEndNs = NowNs();
    double frameDurationUs = NsToUs(frameEndNs - m_FrameStartNs);

    std::memset(&m_Frame, 0, sizeof(m_Frame));
    VansCpuProfiler::Get().EndFrame(m_Frame, frameDurationUs);
    VansGpuProfiler::Get().CollectInto(m_Frame);

    std::sort(m_Frame.events, m_Frame.events + m_Frame.eventCount,
        [](const ProfileEvent& a, const ProfileEvent& b)
        {
            if (a.trackId != b.trackId)
                return a.trackId < b.trackId;
            if (a.startUs != b.startUs)
                return a.startUs < b.startUs;
            return a.depth < b.depth;
        });

    ++m_FrameIndex;
}

void Vans::VansProfiler::RegisterCurrentThread(const char* name, uint32_t color)
{
    VansCpuProfiler::Get().RegisterCurrentThread(name, color);
}

void Vans::VansProfiler::PrintTimeline() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "--- Frame " << m_Frame.frameIndex
        << " total: " << (m_Frame.frameDurationUs * 0.001) << " ms"
        << " FPS: " << m_Frame.fps << " ---\n";

    for (uint32_t i = 0; i < m_Frame.eventCount; ++i)
    {
        const ProfileEvent& event = m_Frame.events[i];
        for (uint16_t d = 0; d < event.depth; ++d)
            oss << "  ";
        oss << "[Track " << event.trackId << "] " << event.name
            << " start=" << event.startUs * 0.001
            << "ms dur=" << (event.endUs - event.startUs) * 0.001 << "ms\n";
    }
    VANS_LOG(oss.str());
}

void Vans::VansProfiler::DumpFrameJson(const char* outputDir) const
{
    json jFrame;
    jFrame["frameIndex"] = m_Frame.frameIndex;
    jFrame["frameDurationUs"] = m_Frame.frameDurationUs;
    jFrame["fps"] = m_Frame.fps;
    jFrame["overflow"] = m_Frame.overflow;

    json jTracks = json::array();
    for (uint32_t i = 0; i < m_Frame.trackCount; ++i)
    {
        const ProfileTrack& track = m_Frame.tracks[i];
        json jt;
        jt["trackId"] = track.trackId;
        jt["type"] = TrackTypeName(track.type);
        jt["threadId"] = track.threadId;
        jt["name"] = track.name;
        jt["color"] = track.color;
        jTracks.push_back(jt);
    }
    jFrame["tracks"] = jTracks;

    json jEvents = json::array();
    for (uint32_t i = 0; i < m_Frame.eventCount; ++i)
    {
        const ProfileEvent& event = m_Frame.events[i];
        json je;
        je["eventId"] = event.eventId;
        je["parentEventId"] = event.parentEventId;
        je["trackId"] = event.trackId;
        je["category"] = CategoryName(event.category);
        je["name"] = event.name;
        je["startUs"] = event.startUs;
        je["endUs"] = event.endUs;
        je["durationUs"] = event.endUs - event.startUs;
        je["depth"] = event.depth;
        je["flags"] = event.flags;
        jEvents.push_back(je);
    }
    jFrame["events"] = jEvents;

    try
    {
        std::filesystem::create_directories(outputDir);
    }
    catch (...)
    {
    }

    std::ostringstream filename;
    filename << outputDir << "/profiler_frame_"
             << std::setfill('0') << std::setw(6) << m_Frame.frameIndex << ".json";

    std::ofstream out(filename.str());
    if (out.is_open())
        out << jFrame.dump(2);
}

#endif
