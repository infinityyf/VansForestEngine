#pragma once
#include "VansVKImage.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <optional>
#include <glm/glm.hpp>

namespace VansGraphics
{
enum class VansRenderTextureSlotBinding : std::uint8_t {Single, FrameContext, GBuffer};
struct VansRuntimeRenderTextureInfo
{
    std::string key,name,category,owner,status;
    std::uint64_t generation = 0;
    std::uint32_t width = 0,height = 0,layers = 0,mips = 0,samples = 0,frameSlots = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    bool initialized = false,history = false,previousAvailable = false;
};
struct VansResolvedRenderTexture
{
    VansVKImage* image = nullptr;
    std::string physicalKey;
    std::optional<glm::vec4> worldRect;
};
// 通用运行时资源目录。物理指针仅由 RenderThread 使用；Main 只读取值快照。
class VansRuntimeRenderTextureCatalog final
{
public:
    struct Source
    {
        std::string key,name,category,owner;
        std::vector<VansVKImage*> images;
        VansRenderTextureSlotBinding slots = VansRenderTextureSlotBinding::Single;
        bool initialized = true,history = false;
        std::uint64_t ownerGeneration = 0;
        std::uint32_t previousSlot = UINT32_MAX;
        // An owner with a history ring maps the current produced image explicitly.
        // UINT32_MAX leaves selection to the declared frame-context/GBuffer mapping.
        std::uint32_t currentSlot = UINT32_MAX;
        std::optional<glm::vec4> worldRect; // 与当前成功提交的图像一起发布，不随失败帧移动。
    };
    // 资源 owner 在 RenderThread 发布外部预览源，销毁图像前必须取消；目录不接管所有权。
    bool RegisterSource(Source source, std::string& error);
    void UnregisterSource(const std::string& key);
    void Publish(std::vector<Source> sources, std::uint64_t sceneEpoch);
    std::vector<VansRuntimeRenderTextureInfo> CaptureSnapshot() const;
    VansVKImage* Resolve(const std::string& key, std::uint64_t generation, std::uint32_t frameSlot, std::uint32_t gbufferSlot, bool previousFrame = false);
    VansResolvedRenderTexture ResolveSampled(const std::string& key, std::uint32_t frameSlot, std::uint32_t gbufferSlot);
    void Clear();
private:
    struct Entry {Source source; VansRuntimeRenderTextureInfo info; std::vector<VkImage> identities; std::uint64_t epoch = 0;};
    std::unordered_map<std::string,Entry> m_Entries;
    std::unordered_map<std::string,Source> m_RegisteredSources;
    std::uint64_t m_Generation = 0;
    mutable std::mutex m_Mutex;
    std::vector<VansRuntimeRenderTextureInfo> m_Snapshot;
};
}
