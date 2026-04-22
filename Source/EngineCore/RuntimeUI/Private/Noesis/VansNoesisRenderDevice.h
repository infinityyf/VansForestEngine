#pragma once

#include <NsCore/Ptr.h>
#include <NsRender/RenderDevice.h>

// Forward decls from ForestEngine
namespace VansGraphics
{
    class VansVKDevice;
}

// Vulkan handles used in the API
#include "vulkan/vulkan.h"

namespace VansRuntime
{

/// ─────────────────────────────────────────────────────────────────────────────
/// VansNoesisRenderDevice
///
/// Thin wrapper that owns the Noesis VKRenderDevice (from Noesis VKFactory) and
/// provides a ForestEngine-friendly interface to set the current command buffer
/// and render pass before Noesis rendering begins each frame.
///
/// Initialization flow:
///   1. Construct with the VansVKDevice pointer.
///   2. Call Initialize() once after Vulkan device is ready — creates the
///      underlying Noesis::RenderDevice via VKFactory::CreateDevice().
///   3. Before recording UI commands each frame, call:
///        SetActiveCommandBuffer(cmd, frameNumber, safeFrameNumber)
///        SetActiveRenderPass(renderPass, sampleCount)
///   4. Call Shutdown() before the Vulkan device is destroyed.
/// ─────────────────────────────────────────────────────────────────────────────
class VansNoesisRenderDevice
{
public:
    explicit VansNoesisRenderDevice(VansGraphics::VansVKDevice* device);
    ~VansNoesisRenderDevice();

    // Create the Noesis VKRenderDevice using Vulkan handles from VansVKDevice.
    // sRGB — set true if the swapchain uses an sRGB surface format.
    bool Initialize(bool sRGB = false);

    void Shutdown();

    // ── Per-frame update ──────────────────────────────────────────────────

    /// Must be called after vkBeginCommandBuffer and before Noesis rendering.
    /// frameNumber       — monotonically increasing frame counter.
    /// safeFrameNumber   — all resources used in frames <= safeFrameNumber
    ///                     are safe to release (typically frameNumber - MaxFramesInFlight).
    void SetActiveCommandBuffer(VkCommandBuffer cmd,
                                uint64_t        frameNumber,
                                uint64_t        safeFrameNumber);

    /// Must be called after vkCmdBeginRenderPass and before Render().
    /// Noesis compiles PSOs lazily on first encounter of a renderPass/sampleCount combo.
    void SetActiveRenderPass(VkRenderPass renderPass, uint32_t sampleCount);

    // ── Accessors ─────────────────────────────────────────────────────────

    /// The raw Noesis::RenderDevice — forwarded to Noesis::GUI::CreateView().
    Noesis::RenderDevice* GetNoesisDevice() const { return m_Device.GetPtr(); }

    bool IsInitialized() const { return m_Initialized; }

private:
    VansGraphics::VansVKDevice* m_VKDevice     = nullptr; // NOT owned
    Noesis::Ptr<Noesis::RenderDevice> m_Device;           // owned
    bool                        m_Initialized  = false;
};

} // namespace VansRuntime
