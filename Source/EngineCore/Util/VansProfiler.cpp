#include "VansProfiler.h"
#include "VansLog.h"

#if VANS_PROFILER_ENABLED

#include "../RenderCore/VulkanCore/VansVKDevice.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
  #include <Windows.h>
#endif

using json = nlohmann::json;

namespace
{
#ifdef _WIN32
    int64_t NowNs()
    {
        static const int64_t frequency = []
        {
            LARGE_INTEGER value{};
            QueryPerformanceFrequency(&value);
            return value.QuadPart;
        }();

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return static_cast<int64_t>(
            static_cast<double>(now.QuadPart) * 1.0e9 /
            static_cast<double>(frequency));
    }
#else
    int64_t NowNs()
    {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
    }
#endif

    double NsToUs(int64_t nanoseconds)
    {
        return static_cast<double>(nanoseconds) * 1.0e-3;
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
    static VansCpuProfiler profiler;
    return profiler;
}

Vans::VansCpuProfiler::ThreadContext& Vans::VansCpuProfiler::GetThreadContext()
{
    thread_local ThreadContext context;
    return context;
}

uint64_t Vans::VansCpuProfiler::GetCurrentThreadIdValue()
{
    return static_cast<uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

uint64_t Vans::VansCpuProfiler::MakeToken(uint32_t bufferIndex, uint64_t frameIndex)
{
    return ((frameIndex + 1u) << 8u) | static_cast<uint64_t>(bufferIndex + 1u);
}

uint32_t Vans::VansCpuProfiler::TokenBufferIndex(uint64_t token)
{
    const uint32_t encoded = static_cast<uint32_t>(token & 0xffu);
    return encoded == 0u ? FRAME_BUFFER_COUNT : encoded - 1u;
}

uint64_t Vans::VansCpuProfiler::TokenFrameIndex(uint64_t token)
{
    return token == INVALID_TOKEN ? INVALID_FRAME_INDEX : (token >> 8u) - 1u;
}

void Vans::VansCpuProfiler::CopyText(
    char* destination,
    uint32_t destinationSize,
    const char* source)
{
    if (destination == nullptr || destinationSize == 0u)
        return;
    std::snprintf(destination, destinationSize, "%s", source != nullptr ? source : "?");
}

uint64_t Vans::VansCpuProfiler::FindFrameToken(uint64_t frameIndex) const
{
    if (frameIndex == INVALID_FRAME_INDEX)
        return INVALID_TOKEN;

    for (uint32_t bufferIndex = 0; bufferIndex < FRAME_BUFFER_COUNT; ++bufferIndex)
    {
        if (m_FrameBuffers[bufferIndex].generation.load(std::memory_order_acquire) == frameIndex)
            return MakeToken(bufferIndex, frameIndex);
    }
    return INVALID_TOKEN;
}

Vans::VansCpuProfiler::FrameBuffer* Vans::VansCpuProfiler::ValidateToken(uint64_t token)
{
    const uint32_t bufferIndex = TokenBufferIndex(token);
    if (bufferIndex >= FRAME_BUFFER_COUNT)
        return nullptr;
    FrameBuffer& buffer = m_FrameBuffers[bufferIndex];
    return buffer.generation.load(std::memory_order_acquire) == TokenFrameIndex(token)
        ? &buffer
        : nullptr;
}

const Vans::VansCpuProfiler::FrameBuffer* Vans::VansCpuProfiler::ValidateToken(uint64_t token) const
{
    const uint32_t bufferIndex = TokenBufferIndex(token);
    if (bufferIndex >= FRAME_BUFFER_COUNT)
        return nullptr;
    const FrameBuffer& buffer = m_FrameBuffers[bufferIndex];
    return buffer.generation.load(std::memory_order_acquire) == TokenFrameIndex(token)
        ? &buffer
        : nullptr;
}

bool Vans::VansCpuProfiler::BeginFrame(uint64_t frameIndex, int64_t frameStartNs)
{
    m_ActiveToken.store(INVALID_TOKEN, std::memory_order_release);

    uint32_t selectedBuffer = FRAME_BUFFER_COUNT;
    const uint32_t preferred = static_cast<uint32_t>(frameIndex % FRAME_BUFFER_COUNT);
    for (uint32_t offset = 0; offset < FRAME_BUFFER_COUNT; ++offset)
    {
        const uint32_t bufferIndex = (preferred + offset) % FRAME_BUFFER_COUNT;
        FrameBuffer& buffer = m_FrameBuffers[bufferIndex];
        if (buffer.generation.load(std::memory_order_acquire) == INVALID_FRAME_INDEX &&
            buffer.activeScopes.load(std::memory_order_acquire) == 0u)
        {
            selectedBuffer = bufferIndex;
            break;
        }
    }

    if (selectedBuffer >= FRAME_BUFFER_COUNT)
        return false;

    FrameBuffer& buffer = m_FrameBuffers[selectedBuffer];
    buffer.eventCount.store(0u, std::memory_order_relaxed);
    buffer.nextEventId.store(1u, std::memory_order_relaxed);
    buffer.activeScopes.store(0u, std::memory_order_relaxed);
    buffer.closed.store(false, std::memory_order_relaxed);
    buffer.renderPending.store(false, std::memory_order_relaxed);
    buffer.expectsGpu.store(false, std::memory_order_relaxed);
    buffer.overflow.store(false, std::memory_order_relaxed);
    buffer.startNs = frameStartNs;
    buffer.endNs = frameStartNs;
    buffer.generation.store(frameIndex, std::memory_order_release);
    buffer.accepting.store(true, std::memory_order_release);

    m_ActiveToken.store(MakeToken(selectedBuffer, frameIndex), std::memory_order_release);
    return true;
}

void Vans::VansCpuProfiler::EndFrame(uint64_t frameIndex, int64_t frameEndNs)
{
    const uint64_t token = FindFrameToken(frameIndex);
    FrameBuffer* buffer = ValidateToken(token);
    if (buffer == nullptr)
        return;

    buffer->endNs = frameEndNs;
    buffer->accepting.store(false, std::memory_order_release);
    buffer->closed.store(true, std::memory_order_release);

    uint64_t expected = token;
    m_ActiveToken.compare_exchange_strong(
        expected,
        INVALID_TOKEN,
        std::memory_order_release,
        std::memory_order_relaxed);
}

void Vans::VansCpuProfiler::MarkFrameHasRender(uint64_t frameIndex)
{
    FrameBuffer* buffer = ValidateToken(FindFrameToken(frameIndex));
    if (buffer == nullptr)
        return;
    buffer->expectsGpu.store(true, std::memory_order_release);
    buffer->renderPending.store(true, std::memory_order_release);
}

void Vans::VansCpuProfiler::BindCurrentThreadToFrame(uint64_t frameIndex)
{
    GetThreadContext().boundToken = FindFrameToken(frameIndex);
}

void Vans::VansCpuProfiler::EndRenderFrame(uint64_t frameIndex)
{
    ThreadContext& context = GetThreadContext();
    const uint64_t token = context.boundToken;
    context.boundToken = INVALID_TOKEN;

    FrameBuffer* buffer = ValidateToken(token);
    if (buffer != nullptr && TokenFrameIndex(token) == frameIndex)
        buffer->renderPending.store(false, std::memory_order_release);
}

bool Vans::VansCpuProfiler::PeekReadyFrame(uint64_t& frameIndex) const
{
    uint64_t oldestFrame = INVALID_FRAME_INDEX;
    for (const FrameBuffer& buffer : m_FrameBuffers)
    {
        const uint64_t generation = buffer.generation.load(std::memory_order_acquire);
        if (generation == INVALID_FRAME_INDEX ||
            !buffer.closed.load(std::memory_order_acquire) ||
            buffer.renderPending.load(std::memory_order_acquire) ||
            buffer.activeScopes.load(std::memory_order_acquire) != 0u)
        {
            continue;
        }
        oldestFrame = std::min(oldestFrame, generation);
    }

    if (oldestFrame == INVALID_FRAME_INDEX)
        return false;
    frameIndex = oldestFrame;
    return true;
}

bool Vans::VansCpuProfiler::CollectReadyFrame(uint64_t frameIndex, ProfileFrame& frame)
{
    const uint64_t token = FindFrameToken(frameIndex);
    FrameBuffer* buffer = ValidateToken(token);
    if (buffer == nullptr ||
        !buffer->closed.load(std::memory_order_acquire) ||
        buffer->renderPending.load(std::memory_order_acquire) ||
        buffer->activeScopes.load(std::memory_order_acquire) != 0u)
    {
        return false;
    }

    std::memset(&frame, 0, sizeof(frame));
    frame.frameIndex = frameIndex;
    frame.frameDurationUs = NsToUs(std::max<int64_t>(0, buffer->endNs - buffer->startNs));
    frame.timelineDurationUs = frame.frameDurationUs;
    frame.fps = frame.frameDurationUs > 0.0 ? 1000000.0 / frame.frameDurationUs : 0.0;
    frame.gpuExpected = buffer->expectsGpu.load(std::memory_order_acquire);

    {
        std::lock_guard<std::mutex> lock(m_TrackMutex);
        frame.trackCount = std::min<uint32_t>(m_TrackCount, ProfileFrame::MAX_TRACKS);
        for (uint32_t trackIndex = 0; trackIndex < frame.trackCount; ++trackIndex)
            frame.tracks[trackIndex] = m_Tracks[trackIndex];
    }

    const uint32_t writtenEventCount = buffer->eventCount.load(std::memory_order_acquire);
    frame.eventCount = std::min<uint32_t>(writtenEventCount, ProfileFrame::MAX_EVENTS);
    frame.droppedCpuEvents = writtenEventCount > ProfileFrame::MAX_EVENTS
        ? writtenEventCount - ProfileFrame::MAX_EVENTS
        : 0u;
    frame.overflow = buffer->overflow.load(std::memory_order_acquire) || frame.droppedCpuEvents != 0u;

    for (uint32_t eventIndex = 0; eventIndex < frame.eventCount; ++eventIndex)
    {
        frame.events[eventIndex] = buffer->events[eventIndex];
        frame.timelineDurationUs = std::max(
            frame.timelineDurationUs,
            frame.events[eventIndex].endUs);
    }

    buffer->accepting.store(false, std::memory_order_relaxed);
    buffer->closed.store(false, std::memory_order_relaxed);
    buffer->expectsGpu.store(false, std::memory_order_relaxed);
    buffer->eventCount.store(0u, std::memory_order_relaxed);
    buffer->generation.store(INVALID_FRAME_INDEX, std::memory_order_release);
    return true;
}

uint32_t Vans::VansCpuProfiler::RegisterTrack(
    uint64_t threadId,
    const char* name,
    uint32_t color)
{
    std::lock_guard<std::mutex> lock(m_TrackMutex);
    for (uint32_t trackIndex = 0; trackIndex < m_TrackCount; ++trackIndex)
    {
        ProfileTrack& track = m_Tracks[trackIndex];
        if (track.threadId == threadId && track.type == ProfileTrackType::CpuThread)
        {
            CopyText(track.name, sizeof(track.name), name);
            track.color = color;
            return track.trackId;
        }
    }

    if (m_TrackCount >= ProfileFrame::MAX_TRACKS)
        return 0u;

    ProfileTrack& track = m_Tracks[m_TrackCount];
    track.trackId = m_TrackCount + 1u;
    track.type = ProfileTrackType::CpuThread;
    track.threadId = threadId;
    track.color = color;
    CopyText(track.name, sizeof(track.name), name);
    ++m_TrackCount;
    return track.trackId;
}

uint32_t Vans::VansCpuProfiler::RegisterCurrentThread(const char* name, uint32_t color)
{
    ThreadContext& context = GetThreadContext();
    context.trackId = RegisterTrack(GetCurrentThreadIdValue(), name, color);
    return context.trackId;
}

bool Vans::VansCpuProfiler::Push(
    const char* name,
    ProfileCategory category,
    uint16_t flags)
{
    ThreadContext& context = GetThreadContext();
    if (context.trackId == 0u)
        context.trackId = RegisterCurrentThread("Thread");
    if (context.trackId == 0u || context.depth >= MAX_DEPTH)
        return false;

    const bool boundToRenderFrame = context.boundToken != INVALID_TOKEN;
    const uint64_t token = boundToRenderFrame
        ? context.boundToken
        : m_ActiveToken.load(std::memory_order_acquire);
    FrameBuffer* buffer = ValidateToken(token);
    if (buffer == nullptr)
        return false;

    buffer->activeScopes.fetch_add(1u, std::memory_order_acq_rel);
    const bool tokenStillValid = ValidateToken(token) == buffer;
    const bool frameAcceptsScope = boundToRenderFrame
        ? buffer->renderPending.load(std::memory_order_acquire)
        : buffer->accepting.load(std::memory_order_acquire);
    if (!tokenStillValid || !frameAcceptsScope)
    {
        buffer->activeScopes.fetch_sub(1u, std::memory_order_release);
        return false;
    }

    StackEntry& entry = context.stack[context.depth];
    CopyText(entry.name, sizeof(entry.name), name);
    entry.category = category;
    entry.flags = flags;
    entry.depth = static_cast<uint16_t>(context.depth);
    entry.trackId = context.trackId;
    entry.frameToken = token;
    entry.startNs = NowNs();
    entry.eventId = buffer->nextEventId.fetch_add(1u, std::memory_order_relaxed);
    entry.parentEventId = context.depth > 0u &&
        context.stack[context.depth - 1u].frameToken == token
        ? context.stack[context.depth - 1u].eventId
        : 0u;
    ++context.depth;
    return true;
}

void Vans::VansCpuProfiler::Pop()
{
    ThreadContext& context = GetThreadContext();
    if (context.depth == 0u)
        return;

    --context.depth;
    const StackEntry& entry = context.stack[context.depth];
    FrameBuffer* buffer = ValidateToken(entry.frameToken);
    if (buffer == nullptr)
        return;

    const int64_t endNs = NowNs();
    const uint32_t eventIndex = buffer->eventCount.fetch_add(1u, std::memory_order_relaxed);
    if (eventIndex < ProfileFrame::MAX_EVENTS)
    {
        ProfileEvent& event = buffer->events[eventIndex];
        event.eventId = entry.eventId;
        event.parentEventId = entry.parentEventId;
        event.trackId = entry.trackId;
        event.category = entry.category;
        CopyText(event.name, sizeof(event.name), entry.name);
        event.startUs = NsToUs(entry.startNs - buffer->startNs);
        event.endUs = NsToUs(endNs - buffer->startNs);
        event.depth = entry.depth;
        event.flags = entry.flags;
    }
    else
    {
        buffer->overflow.store(true, std::memory_order_relaxed);
    }

    buffer->activeScopes.fetch_sub(1u, std::memory_order_release);
}

Vans::VansCpuScopeTimer::VansCpuScopeTimer(
    const char* name,
    ProfileCategory category,
    uint16_t flags)
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
    static VansGpuProfiler profiler;
    return profiler;
}

void Vans::VansGpuProfiler::CopyText(
    char* destination,
    uint32_t destinationSize,
    const char* source)
{
    if (destination == nullptr || destinationSize == 0u)
        return;
    std::snprintf(destination, destinationSize, "%s", source != nullptr ? source : "?");
}

void Vans::VansGpuProfiler::Init(
    void* device,
    void* physDevice,
    uint32_t graphicsQueueFamily,
    uint32_t computeQueueFamily)
{
    Destroy();

    const VkDevice vkDevice = static_cast<VkDevice>(device);
    const VkPhysicalDevice vkPhysicalDevice = static_cast<VkPhysicalDevice>(physDevice);
    if (vkDevice == VK_NULL_HANDLE || vkPhysicalDevice == VK_NULL_HANDLE)
        return;

    m_Device = device;
    m_TimestampPeriodUs =
        VansGraphics::VansVKDevice::GetTimestampPeriodMs(vkPhysicalDevice) * 1000.0;

    const uint32_t queueFamilies[LANE_COUNT] = {
        graphicsQueueFamily,
        computeQueueFamily == VK_QUEUE_FAMILY_IGNORED
            ? graphicsQueueFamily
            : computeQueueFamily
    };

    VkQueryPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    createInfo.queryCount = QUERY_COUNT;

    for (uint32_t lane = 0u; lane < LANE_COUNT; ++lane)
    {
        m_TimestampValidBits[lane] =
            VansGraphics::VansVKDevice::GetQueueFamilyTimestampValidBits(
                vkPhysicalDevice,
                queueFamilies[lane]);
        m_LaneSupported[lane] = m_TimestampValidBits[lane] != 0u;
        if (!m_LaneSupported[lane])
            continue;

        for (uint32_t frameSlot = 0u; frameSlot < FRAME_SLOT_COUNT; ++frameSlot)
        {
            VkQueryPool pool = VK_NULL_HANDLE;
            if (!VansGraphics::VansVKDevice::CreateQueryPool(vkDevice, createInfo, pool))
            {
                VANS_LOG_WARN("[Profiler] Failed to create GPU timestamp query pool.");
                Destroy();
                return;
            }
            m_Pools[lane][frameSlot] = pool;
        }
    }
}

void Vans::VansGpuProfiler::Destroy()
{
    if (m_Device != nullptr)
    {
        const VkDevice vkDevice = static_cast<VkDevice>(m_Device);
        for (uint32_t lane = 0u; lane < LANE_COUNT; ++lane)
        {
            for (uint32_t frameSlot = 0u; frameSlot < FRAME_SLOT_COUNT; ++frameSlot)
            {
                VkQueryPool pool = static_cast<VkQueryPool>(m_Pools[lane][frameSlot]);
                if (pool != VK_NULL_HANDLE)
                    VansGraphics::VansVKDevice::DestroyQueryPool(vkDevice, pool);
                m_Pools[lane][frameSlot] = nullptr;
            }
        }
    }

    m_Device = nullptr;
    m_TimestampPeriodUs = 0.0;
    m_ActiveFrameSlot = INVALID_SLOT;
    std::memset(m_TimestampValidBits, 0, sizeof(m_TimestampValidBits));
    std::memset(m_LaneSupported, 0, sizeof(m_LaneSupported));
    for (FrameSlot& slot : m_FrameSlots)
        slot = FrameSlot{};
}

bool Vans::VansGpuProfiler::IsInitialized() const
{
    return m_Device != nullptr &&
        (m_Pools[0][0] != nullptr || m_Pools[1][0] != nullptr);
}

bool Vans::VansGpuProfiler::IsFrameCaptureActive() const
{
    return m_ActiveFrameSlot < FRAME_SLOT_COUNT;
}

void Vans::VansGpuProfiler::SubmitDroppedFrame(uint64_t frameIndex, uint32_t droppedEvents)
{
    VansGpuResolvedFrame result{};
    result.frameIndex = frameIndex;
    result.droppedEvents = std::max<uint32_t>(1u, droppedEvents);
    result.overflow = true;
    VansProfiler::Get().SubmitGpuFrame(result);
}

void Vans::VansGpuProfiler::BeginFrame(uint64_t frameIndex)
{
    m_ActiveFrameSlot = INVALID_SLOT;
    if (!IsInitialized() || frameIndex == VansCpuProfiler::INVALID_FRAME_INDEX)
        return;

    for (uint32_t frameSlot = 0u; frameSlot < FRAME_SLOT_COUNT; ++frameSlot)
    {
        if (m_FrameSlots[frameSlot].state != FrameSlotState::Free)
            continue;

        m_FrameSlots[frameSlot] = FrameSlot{};
        m_FrameSlots[frameSlot].state = FrameSlotState::Recording;
        m_FrameSlots[frameSlot].frameIndex = frameIndex;
        m_ActiveFrameSlot = frameSlot;
        return;
    }

    SubmitDroppedFrame(frameIndex);
}

void Vans::VansGpuProfiler::BeginQueue(void* cmd, VansGpuQueueLane lane)
{
    if (cmd == nullptr || m_ActiveFrameSlot >= FRAME_SLOT_COUNT)
        return;

    const uint32_t laneIndex = static_cast<uint32_t>(lane);
    if (laneIndex >= LANE_COUNT || !m_LaneSupported[laneIndex])
        return;

    FrameSlot& frame = m_FrameSlots[m_ActiveFrameSlot];
    if (frame.state != FrameSlotState::Recording || frame.laneResetRecorded[laneIndex])
        return;

    const VkQueryPool pool = static_cast<VkQueryPool>(m_Pools[laneIndex][m_ActiveFrameSlot]);
    if (pool == VK_NULL_HANDLE)
        return;

    VansGraphics::VansVKDevice::CmdResetQueryPool(
        static_cast<VkCommandBuffer>(cmd),
        pool,
        0u,
        QUERY_COUNT);
    frame.laneResetRecorded[laneIndex] = true;
}

bool Vans::VansGpuProfiler::Push(void* cmd, const char* name, VansGpuQueueLane lane)
{
    if (cmd == nullptr || m_ActiveFrameSlot >= FRAME_SLOT_COUNT)
        return false;

    const uint32_t laneIndex = static_cast<uint32_t>(lane);
    if (laneIndex >= LANE_COUNT || !m_LaneSupported[laneIndex])
        return false;

    FrameSlot& frame = m_FrameSlots[m_ActiveFrameSlot];
    if (frame.state != FrameSlotState::Recording || !frame.laneResetRecorded[laneIndex])
        return false;

    const uint32_t scopeIndex = frame.scopeCount[laneIndex];
    const uint32_t stackDepth = frame.stackDepth[laneIndex];
    if (scopeIndex >= MAX_GPU_SCOPES ||
        stackDepth >= MAX_STACK_DEPTH ||
        frame.nextQuery[laneIndex] + 1u >= QUERY_COUNT)
    {
        frame.overflow = true;
        ++frame.droppedEvents;
        return false;
    }

    ScopeSlot& scope = frame.scopes[laneIndex][scopeIndex];
    CopyText(scope.name, sizeof(scope.name), name);
    scope.beginQuery = frame.nextQuery[laneIndex]++;
    scope.endQuery = frame.nextQuery[laneIndex]++;
    scope.eventId = laneIndex * MAX_GPU_SCOPES + scopeIndex + 1u;
    scope.parentEventId = stackDepth > 0u
        ? frame.scopes[laneIndex][frame.scopeStack[laneIndex][stackDepth - 1u]].eventId
        : 0u;
    scope.depth = static_cast<uint16_t>(stackDepth);
    scope.closed = false;

    frame.scopeStack[laneIndex][stackDepth] = scopeIndex;
    frame.stackDepth[laneIndex] = stackDepth + 1u;
    frame.scopeCount[laneIndex] = scopeIndex + 1u;

    VansGraphics::VansVKDevice::CmdWriteTimestamp(
        static_cast<VkCommandBuffer>(cmd),
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        static_cast<VkQueryPool>(m_Pools[laneIndex][m_ActiveFrameSlot]),
        scope.beginQuery);
    return true;
}

void Vans::VansGpuProfiler::Pop(void* cmd, VansGpuQueueLane lane)
{
    if (cmd == nullptr || m_ActiveFrameSlot >= FRAME_SLOT_COUNT)
        return;

    const uint32_t laneIndex = static_cast<uint32_t>(lane);
    if (laneIndex >= LANE_COUNT)
        return;

    FrameSlot& frame = m_FrameSlots[m_ActiveFrameSlot];
    if (frame.state != FrameSlotState::Recording || frame.stackDepth[laneIndex] == 0u)
        return;

    const uint32_t newDepth = frame.stackDepth[laneIndex] - 1u;
    frame.stackDepth[laneIndex] = newDepth;
    const uint32_t scopeIndex = frame.scopeStack[laneIndex][newDepth];
    ScopeSlot& scope = frame.scopes[laneIndex][scopeIndex];
    scope.closed = true;

    VansGraphics::VansVKDevice::CmdWriteTimestamp(
        static_cast<VkCommandBuffer>(cmd),
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        static_cast<VkQueryPool>(m_Pools[laneIndex][m_ActiveFrameSlot]),
        scope.endQuery);
}

int64_t Vans::VansGpuProfiler::TimestampDelta(
    uint64_t value,
    uint64_t reference,
    uint32_t validBits)
{
    if (validBits == 0u)
        return 0;
    if (validBits >= 64u)
        return static_cast<int64_t>(value - reference);

    const uint64_t mask = (uint64_t{ 1 } << validBits) - 1u;
    const uint64_t signBit = uint64_t{ 1 } << (validBits - 1u);
    uint64_t difference = (value - reference) & mask;
    if ((difference & signBit) != 0u)
        difference |= ~mask;
    return static_cast<int64_t>(difference);
}

bool Vans::VansGpuProfiler::TryResolveFrame(
    uint32_t frameSlotIndex,
    void* device,
    VansGpuResolvedFrame& result)
{
    FrameSlot& frame = m_FrameSlots[frameSlotIndex];
    if (frame.state != FrameSlotState::Pending)
        return false;

    const VkDevice vkDevice = static_cast<VkDevice>(device);
    bool hasQueries = false;
    uint32_t commonValidBits = 64u;
    uint64_t referenceTimestamp = 0u;
    bool hasReference = false;

    for (uint32_t lane = 0u; lane < LANE_COUNT; ++lane)
    {
        const uint32_t queryCount = frame.nextQuery[lane];
        if (queryCount == 0u)
            continue;

        hasQueries = true;
        commonValidBits = std::min(commonValidBits, m_TimestampValidBits[lane]);
        const VkResult queryResult = VansGraphics::VansVKDevice::GetQueryPoolResults(
            vkDevice,
            static_cast<VkQueryPool>(m_Pools[lane][frameSlotIndex]),
            0u,
            queryCount,
            sizeof(QueryValue) * queryCount,
            m_QueryResults[lane],
            sizeof(QueryValue),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (queryResult != VK_SUCCESS && queryResult != VK_NOT_READY)
        {
            result = VansGpuResolvedFrame{};
            result.frameIndex = frame.frameIndex;
            result.droppedEvents = std::max<uint32_t>(1u, frame.droppedEvents);
            result.overflow = true;
            return true;
        }

        for (uint32_t queryIndex = 0u; queryIndex < queryCount; ++queryIndex)
        {
            if (m_QueryResults[lane][queryIndex].available == 0u)
                return false;
        }

        if (!hasReference && frame.scopeCount[lane] > 0u)
        {
            referenceTimestamp =
                m_QueryResults[lane][frame.scopes[lane][0].beginQuery].value;
            hasReference = true;
        }
    }

    result = VansGpuResolvedFrame{};
    result.frameIndex = frame.frameIndex;
    result.droppedEvents = frame.droppedEvents;
    result.overflow = frame.overflow;
    if (!hasQueries || !hasReference)
        return true;

    int64_t earliestTick = 0;
    bool hasEarliestTick = false;
    for (uint32_t lane = 0u; lane < LANE_COUNT; ++lane)
    {
        for (uint32_t scopeIndex = 0u; scopeIndex < frame.scopeCount[lane]; ++scopeIndex)
        {
            const ScopeSlot& scope = frame.scopes[lane][scopeIndex];
            if (!scope.closed)
                continue;
            const int64_t beginTick = TimestampDelta(
                m_QueryResults[lane][scope.beginQuery].value,
                referenceTimestamp,
                commonValidBits);
            if (!hasEarliestTick || beginTick < earliestTick)
            {
                earliestTick = beginTick;
                hasEarliestTick = true;
            }
        }
    }

    for (uint32_t lane = 0u; lane < LANE_COUNT; ++lane)
    {
        for (uint32_t scopeIndex = 0u; scopeIndex < frame.scopeCount[lane]; ++scopeIndex)
        {
            const ScopeSlot& scope = frame.scopes[lane][scopeIndex];
            if (!scope.closed)
            {
                ++result.droppedEvents;
                result.overflow = true;
                continue;
            }
            if (result.eventCount >= VansGpuResolvedFrame::MAX_EVENTS)
            {
                ++result.droppedEvents;
                result.overflow = true;
                continue;
            }

            ProfileEvent& event = result.events[result.eventCount++];
            event.eventId = scope.eventId;
            event.parentEventId = scope.parentEventId;
            event.trackId = lane + 1u;
            event.category = ProfileCategory::GPU;
            CopyText(event.name, sizeof(event.name), scope.name);
            const int64_t beginTick = TimestampDelta(
                m_QueryResults[lane][scope.beginQuery].value,
                referenceTimestamp,
                commonValidBits);
            const int64_t endTick = TimestampDelta(
                m_QueryResults[lane][scope.endQuery].value,
                referenceTimestamp,
                commonValidBits);
            event.startUs = static_cast<double>(beginTick - earliestTick) * m_TimestampPeriodUs;
            event.endUs = static_cast<double>(endTick - earliestTick) * m_TimestampPeriodUs;
            event.depth = scope.depth;
            event.flags = ProfileEventFlagGpu;
            result.durationUs = std::max(result.durationUs, event.endUs);
            ++result.laneEventCount[lane];
        }
    }
    return true;
}

void Vans::VansGpuProfiler::PollPendingFrames(void* device)
{
    if (device == nullptr || !IsInitialized())
        return;

    for (uint32_t frameSlot = 0u; frameSlot < FRAME_SLOT_COUNT; ++frameSlot)
    {
        if (m_FrameSlots[frameSlot].state != FrameSlotState::Pending)
            continue;

        VansGpuResolvedFrame result{};
        if (!TryResolveFrame(frameSlot, device, result))
            continue;

        VansProfiler::Get().SubmitGpuFrame(result);
        m_FrameSlots[frameSlot] = FrameSlot{};
    }
}

void Vans::VansGpuProfiler::EndFrame(void* device)
{
    if (m_ActiveFrameSlot < FRAME_SLOT_COUNT)
    {
        FrameSlot& frame = m_FrameSlots[m_ActiveFrameSlot];
        bool hasScopes = false;
        for (uint32_t lane = 0u; lane < LANE_COUNT; ++lane)
            hasScopes = hasScopes || frame.scopeCount[lane] != 0u;

        if (hasScopes)
        {
            frame.state = FrameSlotState::Pending;
        }
        else
        {
            VansGpuResolvedFrame result{};
            result.frameIndex = frame.frameIndex;
            result.overflow = frame.overflow;
            result.droppedEvents = frame.droppedEvents;
            VansProfiler::Get().SubmitGpuFrame(result);
            frame = FrameSlot{};
        }
        m_ActiveFrameSlot = INVALID_SLOT;
    }

    PollPendingFrames(device);
}

Vans::VansGpuScopeQuery::VansGpuScopeQuery(
    void* cmd,
    const char* name,
    VansGpuQueueLane lane)
    : m_Cmd(cmd),
      m_Lane(lane)
{
    m_Active = VansGpuProfiler::Get().Push(cmd, name, lane);
}

Vans::VansGpuScopeQuery::~VansGpuScopeQuery()
{
    if (m_Active)
        VansGpuProfiler::Get().Pop(m_Cmd, m_Lane);
}

Vans::VansProfiler& Vans::VansProfiler::Get()
{
    static VansProfiler profiler;
    return profiler;
}

void Vans::VansProfiler::BeginFrame()
{
    ProcessCompletedFrames();
    m_ActiveFrameIndex.store(INVALID_FRAME_INDEX, std::memory_order_release);
    if (!IsCaptureEnabled())
        return;

    RegisterCurrentThread("Main Thread", 0xff62c96bu);
    const uint64_t frameIndex = m_NextFrameIndex++;
    if (VansCpuProfiler::Get().BeginFrame(frameIndex, NowNs()))
        m_ActiveFrameIndex.store(frameIndex, std::memory_order_release);
}

void Vans::VansProfiler::EndFrame()
{
    const uint64_t frameIndex = m_ActiveFrameIndex.exchange(
        INVALID_FRAME_INDEX,
        std::memory_order_acq_rel);
    if (frameIndex != INVALID_FRAME_INDEX)
        VansCpuProfiler::Get().EndFrame(frameIndex, NowNs());
}

void Vans::VansProfiler::MarkCurrentFrameHasRender()
{
    const uint64_t frameIndex = GetActiveFrameIndex();
    if (frameIndex != INVALID_FRAME_INDEX)
        VansCpuProfiler::Get().MarkFrameHasRender(frameIndex);
}

uint64_t Vans::VansProfiler::GetActiveFrameIndex() const
{
    return m_ActiveFrameIndex.load(std::memory_order_acquire);
}

void Vans::VansProfiler::BeginRenderFrame(uint64_t frameIndex)
{
    VansCpuProfiler::Get().BindCurrentThreadToFrame(frameIndex);
    VansGpuProfiler::Get().BeginFrame(frameIndex);
}

void Vans::VansProfiler::EndRenderFrame(uint64_t frameIndex, void* device)
{
    VansGpuProfiler::Get().EndFrame(device);
    VansCpuProfiler::Get().EndRenderFrame(frameIndex);
}

void Vans::VansProfiler::SubmitGpuFrame(const VansGpuResolvedFrame& frame)
{
    if (frame.frameIndex == INVALID_FRAME_INDEX)
        return;

    std::lock_guard<std::mutex> lock(m_GpuResultMutex);
    if (m_GpuResultCount == GPU_RESULT_QUEUE_COUNT)
    {
        m_GpuResultHead = (m_GpuResultHead + 1u) % GPU_RESULT_QUEUE_COUNT;
        --m_GpuResultCount;
    }
    const uint32_t writeIndex =
        (m_GpuResultHead + m_GpuResultCount) % GPU_RESULT_QUEUE_COUNT;
    m_GpuResultQueue[writeIndex] = frame;
    ++m_GpuResultCount;
}

bool Vans::VansProfiler::TryPopGpuFrame(VansGpuResolvedFrame& frame)
{
    std::lock_guard<std::mutex> lock(m_GpuResultMutex);
    if (m_GpuResultCount == 0u)
        return false;
    frame = m_GpuResultQueue[m_GpuResultHead];
    m_GpuResultHead = (m_GpuResultHead + 1u) % GPU_RESULT_QUEUE_COUNT;
    --m_GpuResultCount;
    return true;
}

Vans::VansProfiler::FrameAssembly& Vans::VansProfiler::AcquireAssembly(uint64_t frameIndex)
{
    for (FrameAssembly& assembly : m_Assemblies)
    {
        if (assembly.occupied && assembly.frameIndex == frameIndex)
            return assembly;
    }
    for (FrameAssembly& assembly : m_Assemblies)
    {
        if (!assembly.occupied)
        {
            std::memset(&assembly, 0, sizeof(assembly));
            assembly.occupied = true;
            assembly.frameIndex = frameIndex;
            assembly.gpu.frameIndex = INVALID_FRAME_INDEX;
            return assembly;
        }
    }

    uint32_t replacementIndex = 0u;
    uint64_t oldestFrame = INVALID_FRAME_INDEX;
    for (uint32_t index = 0u; index < ASSEMBLY_COUNT; ++index)
    {
        if (static_cast<int32_t>(index) == m_PublishedAssembly)
            continue;
        if (m_Assemblies[index].frameIndex < oldestFrame)
        {
            oldestFrame = m_Assemblies[index].frameIndex;
            replacementIndex = index;
        }
    }

    FrameAssembly& assembly = m_Assemblies[replacementIndex];
    std::memset(&assembly, 0, sizeof(assembly));
    assembly.occupied = true;
    assembly.frameIndex = frameIndex;
    assembly.gpu.frameIndex = INVALID_FRAME_INDEX;
    return assembly;
}

void Vans::VansProfiler::BuildTrackEventRanges(ProfileFrame& frame)
{
    for (uint32_t trackIndex = 0u; trackIndex < frame.trackCount; ++trackIndex)
    {
        frame.tracks[trackIndex].firstEventIndex = 0u;
        frame.tracks[trackIndex].eventCount = 0u;
    }

    for (uint32_t eventIndex = 0u; eventIndex < frame.eventCount; ++eventIndex)
    {
        const uint32_t trackId = frame.events[eventIndex].trackId;
        if (trackId == 0u || trackId > frame.trackCount)
            continue;
        ProfileTrack& track = frame.tracks[trackId - 1u];
        if (track.eventCount == 0u)
            track.firstEventIndex = eventIndex;
        ++track.eventCount;
    }
}

void Vans::VansProfiler::ComposeAssembly(FrameAssembly& assembly)
{
    if (!assembly.occupied || !assembly.hasCpu || assembly.composed)
        return;
    if (assembly.frame.gpuExpected && !assembly.hasGpu)
        return;

    ProfileFrame& frame = assembly.frame;
    frame.gpuComplete = frame.gpuExpected && assembly.hasGpu;
    if (assembly.hasGpu)
    {
        frame.gpuDurationUs = assembly.gpu.durationUs;
        frame.timelineDurationUs = std::max(frame.timelineDurationUs, frame.gpuDurationUs);
        frame.droppedGpuEvents = assembly.gpu.droppedEvents;
        frame.overflow = frame.overflow || assembly.gpu.overflow;

        uint32_t laneTrackId[2] = {};
        static constexpr const char* laneNames[2] = {
            "GPU Graphics Queue",
            "GPU Compute Queue"
        };
        static constexpr uint32_t laneColors[2] = {
            0xff7a8cffu,
            0xff62c96bu
        };

        for (uint32_t lane = 0u; lane < 2u; ++lane)
        {
            if (assembly.gpu.laneEventCount[lane] == 0u ||
                frame.trackCount >= ProfileFrame::MAX_TRACKS)
            {
                continue;
            }
            ProfileTrack& track = frame.tracks[frame.trackCount];
            track.trackId = frame.trackCount + 1u;
            track.type = ProfileTrackType::GpuQueue;
            track.threadId = lane + 1u;
            track.color = laneColors[lane];
            std::snprintf(track.name, sizeof(track.name), "%s", laneNames[lane]);
            laneTrackId[lane] = track.trackId;
            ++frame.trackCount;
        }

        for (uint32_t eventIndex = 0u; eventIndex < assembly.gpu.eventCount; ++eventIndex)
        {
            const ProfileEvent& gpuEvent = assembly.gpu.events[eventIndex];
            const uint32_t lane = gpuEvent.trackId > 0u ? gpuEvent.trackId - 1u : 2u;
            if (lane >= 2u || laneTrackId[lane] == 0u)
                continue;
            if (frame.eventCount >= ProfileFrame::MAX_EVENTS)
            {
                ++frame.droppedGpuEvents;
                frame.overflow = true;
                continue;
            }
            ProfileEvent event = gpuEvent;
            event.trackId = laneTrackId[lane];
            frame.events[frame.eventCount++] = event;
        }
    }

    std::sort(
        frame.events,
        frame.events + frame.eventCount,
        [](const ProfileEvent& left, const ProfileEvent& right)
        {
            if (left.trackId != right.trackId)
                return left.trackId < right.trackId;
            if (left.startUs != right.startUs)
                return left.startUs < right.startUs;
            return left.depth < right.depth;
        });
    BuildTrackEventRanges(frame);
    assembly.composed = true;
}

void Vans::VansProfiler::PublishLatestReadyAssembly()
{
    if (IsPaused())
        return;

    int32_t latestIndex = -1;
    uint64_t latestFrame = m_LastPublishedFrameIndex;
    for (uint32_t index = 0u; index < ASSEMBLY_COUNT; ++index)
    {
        const FrameAssembly& assembly = m_Assemblies[index];
        if (!assembly.occupied || !assembly.composed)
            continue;
        if (m_LastPublishedFrameIndex == INVALID_FRAME_INDEX || assembly.frameIndex > latestFrame)
        {
            latestFrame = assembly.frameIndex;
            latestIndex = static_cast<int32_t>(index);
        }
    }

    if (latestIndex < 0)
        return;

    m_PublishedAssembly = latestIndex;
    m_LastPublishedFrameIndex = latestFrame;
    for (uint32_t index = 0u; index < ASSEMBLY_COUNT; ++index)
    {
        if (static_cast<int32_t>(index) == m_PublishedAssembly)
            continue;
        if (m_Assemblies[index].occupied && m_Assemblies[index].frameIndex < latestFrame)
        {
            std::memset(&m_Assemblies[index], 0, sizeof(FrameAssembly));
            m_Assemblies[index].frameIndex = INVALID_FRAME_INDEX;
            m_Assemblies[index].gpu.frameIndex = INVALID_FRAME_INDEX;
        }
    }
}

void Vans::VansProfiler::ProcessCompletedFrames()
{
    uint64_t cpuFrameIndex = INVALID_FRAME_INDEX;
    while (VansCpuProfiler::Get().PeekReadyFrame(cpuFrameIndex))
    {
        FrameAssembly& assembly = AcquireAssembly(cpuFrameIndex);
        if (!VansCpuProfiler::Get().CollectReadyFrame(cpuFrameIndex, assembly.frame))
            break;
        assembly.hasCpu = true;
        assembly.frameIndex = cpuFrameIndex;
    }

    VansGpuResolvedFrame gpuFrame{};
    while (TryPopGpuFrame(gpuFrame))
    {
        FrameAssembly& assembly = AcquireAssembly(gpuFrame.frameIndex);
        assembly.gpu = gpuFrame;
        assembly.hasGpu = true;
    }

    for (FrameAssembly& assembly : m_Assemblies)
        ComposeAssembly(assembly);
    PublishLatestReadyAssembly();
}

void Vans::VansProfiler::SetCaptureEnabled(bool enabled)
{
    m_CaptureRequested.store(enabled, std::memory_order_release);
}

bool Vans::VansProfiler::IsCaptureEnabled() const
{
    return m_CaptureRequested.load(std::memory_order_acquire) &&
        !m_Paused.load(std::memory_order_acquire);
}

void Vans::VansProfiler::SetPaused(bool paused)
{
    m_Paused.store(paused, std::memory_order_release);
}

bool Vans::VansProfiler::IsPaused() const
{
    return m_Paused.load(std::memory_order_acquire);
}

void Vans::VansProfiler::RegisterCurrentThread(const char* name, uint32_t color)
{
    VansCpuProfiler::Get().RegisterCurrentThread(name, color);
}

const Vans::ProfileFrame& Vans::VansProfiler::GetTimeline() const
{
    if (m_PublishedAssembly < 0)
        return m_EmptyFrame;
    return m_Assemblies[static_cast<uint32_t>(m_PublishedAssembly)].frame;
}

void Vans::VansProfiler::PrintTimeline() const
{
    const ProfileFrame& frame = GetTimeline();
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3);
    stream << "--- Frame " << frame.frameIndex
        << " main: " << (frame.frameDurationUs * 0.001) << " ms"
        << " GPU: " << (frame.gpuDurationUs * 0.001) << " ms"
        << " FPS: " << frame.fps << " ---\n";

    for (uint32_t eventIndex = 0u; eventIndex < frame.eventCount; ++eventIndex)
    {
        const ProfileEvent& event = frame.events[eventIndex];
        for (uint16_t depth = 0u; depth < event.depth; ++depth)
            stream << "  ";
        stream << "[Track " << event.trackId << "] " << event.name
            << " start=" << event.startUs * 0.001
            << "ms dur=" << (event.endUs - event.startUs) * 0.001 << "ms\n";
    }
    VANS_LOG(stream.str());
}

void Vans::VansProfiler::DumpFrameJson(const char* outputDir) const
{
    const ProfileFrame& frame = GetTimeline();
    json jsonFrame;
    jsonFrame["frameIndex"] = frame.frameIndex;
    jsonFrame["frameDurationUs"] = frame.frameDurationUs;
    jsonFrame["timelineDurationUs"] = frame.timelineDurationUs;
    jsonFrame["gpuDurationUs"] = frame.gpuDurationUs;
    jsonFrame["fps"] = frame.fps;
    jsonFrame["gpuExpected"] = frame.gpuExpected;
    jsonFrame["gpuComplete"] = frame.gpuComplete;
    jsonFrame["droppedCpuEvents"] = frame.droppedCpuEvents;
    jsonFrame["droppedGpuEvents"] = frame.droppedGpuEvents;
    jsonFrame["overflow"] = frame.overflow;

    json jsonTracks = json::array();
    for (uint32_t trackIndex = 0u; trackIndex < frame.trackCount; ++trackIndex)
    {
        const ProfileTrack& track = frame.tracks[trackIndex];
        json jsonTrack;
        jsonTrack["trackId"] = track.trackId;
        jsonTrack["type"] = TrackTypeName(track.type);
        jsonTrack["threadId"] = track.threadId;
        jsonTrack["name"] = track.name;
        jsonTrack["color"] = track.color;
        jsonTrack["eventCount"] = track.eventCount;
        jsonTracks.push_back(jsonTrack);
    }
    jsonFrame["tracks"] = jsonTracks;

    json jsonEvents = json::array();
    for (uint32_t eventIndex = 0u; eventIndex < frame.eventCount; ++eventIndex)
    {
        const ProfileEvent& event = frame.events[eventIndex];
        json jsonEvent;
        jsonEvent["eventId"] = event.eventId;
        jsonEvent["parentEventId"] = event.parentEventId;
        jsonEvent["trackId"] = event.trackId;
        jsonEvent["category"] = CategoryName(event.category);
        jsonEvent["name"] = event.name;
        jsonEvent["startUs"] = event.startUs;
        jsonEvent["endUs"] = event.endUs;
        jsonEvent["durationUs"] = event.endUs - event.startUs;
        jsonEvent["depth"] = event.depth;
        jsonEvent["flags"] = event.flags;
        jsonEvents.push_back(jsonEvent);
    }
    jsonFrame["events"] = jsonEvents;

    const char* destinationDirectory = outputDir != nullptr ? outputDir : "LOG";
    try
    {
        std::filesystem::create_directories(destinationDirectory);
    }
    catch (...)
    {
    }

    std::ostringstream filename;
    filename << destinationDirectory << "/profiler_frame_"
        << std::setfill('0') << std::setw(6) << frame.frameIndex << ".json";
    std::ofstream output(filename.str());
    if (output.is_open())
        output << jsonFrame.dump(2);
}

Vans::VansProfilerFrameScope::VansProfilerFrameScope(bool frameActive)
    : m_FrameActive(frameActive)
{
    if (m_FrameActive)
        VansProfiler::Get().BeginFrame();
}

Vans::VansProfilerFrameScope::~VansProfilerFrameScope()
{
    if (m_FrameActive)
        VansProfiler::Get().EndFrame();
}

#endif
