#include "Public/VansUISystem.h"
#include "Public/VansUIDocument.h"
#include "Public/VansUIScreenManager.h"

#include "Private/Noesis/VansNoesisUISystem.h"
#include "Private/Noesis/VansNoesisDocument.h"

// Forward: VansVKDevice is needed to construct VansNoesisUISystem.
// We access it via the singleton pattern similar to other engine systems.
#include "../RenderCore/VulkanCore/VansVKDevice.h"

#include <cassert>
#include <memory>

namespace VansRuntime
{

// ─────────────────────────────────────────────────────────────────────────────
// Pimpl implementation struct
// ─────────────────────────────────────────────────────────────────────────────

struct VansUISystem::Impl
{
    std::unique_ptr<VansNoesisUISystem> m_NoesisSystem;
    std::unique_ptr<VansUIScreenManager> m_ScreenManager;
};

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────

VansUISystem& VansUISystem::Get()
{
    static VansUISystem s_Instance;
    return s_Instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool VansUISystem::Initialize(const VansUIInitDesc& desc)
{
    if (m_Impl) return true; // already initialized

    // VansVKDevice is the engine's Vulkan device singleton.
    // The UI system is initialized by the render core after device creation.
    // The caller is expected to pass the device via the rendering pipeline —
    // here we access it through the design: the InitDesc could carry it, or
    // the VansNoesisUISystem is initialized from the render pass setup code.
    //
    // For now, VansUISystem::Initialize is called from main rendering setup
    // which has direct access to VansVKDevice.  We store a reference to be
    // consistent with the rest of the engine, but we rely on the caller to
    // call InitializeWithDevice() when they have the device handle.
    //
    // If called without a device (e.g., from test code), m_NoesisSystem stays
    // null and the system silently no-ops.

    m_Impl = std::make_unique<Impl>();
    m_Impl->m_ScreenManager = std::make_unique<VansUIScreenManager>(*this);

    // Note: Noesis initialization requires a VansVKDevice pointer.
    // Use InitializeWithDevice() after calling this function.
    (void)desc; // Saved by the render-core caller for InitializeWithDevice
    return true;
}

bool VansUISystem::InitializeWithDevice(const VansUIInitDesc& desc,
                                         VansGraphics::VansVKDevice* device)
{
    if (!m_Impl)
    {
        m_Impl = std::make_unique<Impl>();
        m_Impl->m_ScreenManager = std::make_unique<VansUIScreenManager>(*this);
    }

    if (m_Impl->m_NoesisSystem) return true; // already done

    m_Impl->m_NoesisSystem = std::make_unique<VansNoesisUISystem>(device);
    return m_Impl->m_NoesisSystem->Initialize(desc);
}

void VansUISystem::Shutdown()
{
    if (!m_Impl) return;

    if (m_Impl->m_NoesisSystem)
    {
        m_Impl->m_NoesisSystem->Shutdown();
        m_Impl->m_NoesisSystem.reset();
    }

    m_Impl->m_ScreenManager.reset();
    m_Impl.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame
// ─────────────────────────────────────────────────────────────────────────────

void VansUISystem::Update(float deltaTime)
{
    if (!m_Impl || !m_Impl->m_NoesisSystem) return;
    m_Impl->m_NoesisSystem->Update(deltaTime);
}

// ─────────────────────────────────────────────────────────────────────────────
// Document management
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<VansUIDocument> VansUISystem::LoadDocument(const std::string& xamlPath)
{
    if (!m_Impl || !m_Impl->m_NoesisSystem) return nullptr;
    return m_Impl->m_NoesisSystem->LoadDocument(xamlPath);
}

void VansUISystem::UnloadDocument(const std::shared_ptr<VansUIDocument>& document)
{
    if (!m_Impl || !m_Impl->m_NoesisSystem) return;

    // VansUIDocument is the public interface; the concrete type is VansNoesisDocument.
    // We need to cast: both share_ptrs point to the same object due to the covariant type.
    auto noesisDoc = std::dynamic_pointer_cast<VansNoesisDocument>(document);
    if (noesisDoc)
    {
        m_Impl->m_NoesisSystem->UnloadDocument(noesisDoc);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen size
// ─────────────────────────────────────────────────────────────────────────────

void VansUISystem::SetScreenSize(uint32_t width, uint32_t height)
{
    if (!m_Impl || !m_Impl->m_NoesisSystem) return;
    m_Impl->m_NoesisSystem->SetScreenSize(width, height);
}

void VansUISystem::SetSceneViewport(float screenX, float screenY,
                                     float screenW, float screenH)
{
    if (!m_Impl || !m_Impl->m_NoesisSystem) return;
    m_Impl->m_NoesisSystem->SetSceneViewport(screenX, screenY, screenW, screenH);
}

// ─────────────────────────────────────────────────────────────────────────────
// ScreenManager
// ─────────────────────────────────────────────────────────────────────────────

VansUIScreenManager& VansUISystem::GetScreenManager()
{
    assert(m_Impl && m_Impl->m_ScreenManager &&
           "VansUISystem: GetScreenManager() called before Initialize()");
    return *m_Impl->m_ScreenManager;
}

// ─────────────────────────────────────────────────────────────────────────────
// Input capture query
// ─────────────────────────────────────────────────────────────────────────────

bool VansUISystem::WantsMouse() const
{
    if (!m_Impl || !m_Impl->m_NoesisSystem) return false;
    return m_Impl->m_NoesisSystem->WantsMouse();
}

bool VansUISystem::WantsKeyboard() const
{
    if (!m_Impl || !m_Impl->m_NoesisSystem) return false;
    return m_Impl->m_NoesisSystem->WantsKeyboard();
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug
// ─────────────────────────────────────────────────────────────────────────────

bool VansUISystem::IsInitialized() const
{
    return m_Impl && m_Impl->m_NoesisSystem && m_Impl->m_NoesisSystem->IsInitialized();
}

// ─────────────────────────────────────────────────────────────────────────────
// 渲染接口（转发到 VansNoesisUISystem）
// ─────────────────────────────────────────────────────────────────────────────

void VansUISystem::RenderOffscreen(void* nativeCmdBuffer)
{
    if (!m_Impl || !m_Impl->m_NoesisSystem) return;
    m_Impl->m_NoesisSystem->RenderOffscreenPass(
        static_cast<VkCommandBuffer>(nativeCmdBuffer));
}

void VansUISystem::RenderDocuments(void* nativeRenderPass, uint32_t sampleCount)
{
    if (!m_Impl || !m_Impl->m_NoesisSystem) return;
    m_Impl->m_NoesisSystem->RenderDocumentsPass(
        static_cast<VkRenderPass>(nativeRenderPass), sampleCount);
}

} // namespace VansRuntime
