#include "VansNoesisRenderDevice.h"

// Include Noesis VKFactory — this is in the VKRenderDevice package under Src.
// The project must add the following additional include directory:
//   External/NoesisGUI/Src/Packages/Render/VKRenderDevice/Include
#include <NsRender/VKFactory.h>

// ForestEngine
#include "../../../RenderCore/VulkanCore/VansVKDevice.h"
// Forward-declare only the function pointer we need from VansGraphics,
// to avoid pulling in the full VansVKFunctions.h with platform-specific inl expansions.
namespace VansGraphics { extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr; }

#include <cassert>

namespace VansRuntime
{

// ─────────────────────────────────────────────────────────────────────────────

VansNoesisRenderDevice::VansNoesisRenderDevice(VansGraphics::VansVKDevice* device)
    : m_VKDevice(device)
{}

VansNoesisRenderDevice::~VansNoesisRenderDevice()
{
    Shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize
// ─────────────────────────────────────────────────────────────────────────────

bool VansNoesisRenderDevice::Initialize(bool sRGB)
{
    if (m_Initialized) return true;
    assert(m_VKDevice != nullptr);

    // Build the InstanceInfo from ForestEngine's Vulkan device
    NoesisApp::VKFactory::InstanceInfo info {};
    info.instance           = m_VKDevice->GetInstance();
    info.physicalDevice     = m_VKDevice->GetPhysicalDevice();
    info.device             = m_VKDevice->GetLogicDevice();
    info.pipelineCache      = VK_NULL_HANDLE;  // optional — no cache at startup
    info.queueFamilyIndex   = m_VKDevice->GetGraphicsQueueFamilyIndex();

    // Use the project's dynamically loaded vkGetInstanceProcAddr function pointer
    // (loaded from vulkan-1.dll at runtime by VansVKFunctions).
    info.vkGetInstanceProcAddr = VansGraphics::vkGetInstanceProcAddr;

    m_Device = NoesisApp::VKFactory::CreateDevice(sRGB, info);
    if (!m_Device)
    {
        return false;
    }

    m_Initialized = true;
    return true;
}

void VansNoesisRenderDevice::Shutdown()
{
    if (!m_Initialized) return;
    m_Device.Reset();
    m_Initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame
// ─────────────────────────────────────────────────────────────────────────────

void VansNoesisRenderDevice::SetActiveCommandBuffer(
    VkCommandBuffer cmd,
    uint64_t        frameNumber,
    uint64_t        safeFrameNumber)
{
    if (!m_Device) return;

    NoesisApp::VKFactory::RecordingInfo info {};
    info.commandBuffer  = cmd;
    info.frameNumber    = frameNumber;
    info.safeFrameNumber = safeFrameNumber;

    NoesisApp::VKFactory::SetCommandBuffer(m_Device.GetPtr(), info);
}

void VansNoesisRenderDevice::SetActiveRenderPass(VkRenderPass renderPass, uint32_t sampleCount)
{
    if (!m_Device) return;
    NoesisApp::VKFactory::SetRenderPass(m_Device.GetPtr(), renderPass, sampleCount);
}

} // namespace VansRuntime
