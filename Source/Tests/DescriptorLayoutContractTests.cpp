#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace VansGraphics;
uint64_t creates = 0, destroys = 0;
VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice, const VkDescriptorSetLayoutCreateInfo*,
    const VkAllocationCallbacks*, VkDescriptorSetLayout* result)
{
    *result = reinterpret_cast<VkDescriptorSetLayout>(uintptr_t(++creates));
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL Destroy(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*) { ++destroys; }
void Check(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
}

bool TestDescriptorLayoutSharingContract()
{
    using namespace VansGraphics;
    const auto create = VansGraphics::vkCreateDescriptorSetLayout;
    const auto destroy = VansGraphics::vkDestroyDescriptorSetLayout;
    auto* manager = VansVKDescriptorManager::GetInstance();
    struct Restore
    {
        PFN_vkCreateDescriptorSetLayout create;
        PFN_vkDestroyDescriptorSetLayout destroy;
        ~Restore()
        {
            VansVKDescriptorManager::GetInstance()->DestroyDescriptorPool();
            VansGraphics::vkCreateDescriptorSetLayout = create;
            VansGraphics::vkDestroyDescriptorSetLayout = destroy;
        }
    } restore{create, destroy};
    VansGraphics::vkCreateDescriptorSetLayout = Create;
    VansGraphics::vkDestroyDescriptorSetLayout = Destroy;
    VkPhysicalDevice physical{}; VkDevice device{}; VkCommandBuffer command{};
    manager->BindDevice(physical, device, command);
    creates = destroys = 0;
    try
    {
		Check(MAX_BINDLESS_TEXTURES == 4096,
			"Global bindless capacity no longer covers large material scenes");
		Check(IsBindlessTextureCountSupported(MAX_BINDLESS_TEXTURES)
			&& !IsBindlessTextureCountSupported(
				static_cast<std::uint64_t>(MAX_BINDLESS_TEXTURES) + 1u),
			"Bindless descriptor capacity guard is not exact");
        std::vector<VkDescriptorSetLayoutBinding> bindings{
            {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr}};
        VkDescriptorSetLayout shared{};
        Check(manager->CreateDesciptorSetLayout(bindings, shared), "First layout failed");
        // 覆盖整池首次使用、乱序 binding、释放所有实例后再次启用。
        for (int i=0; i<256; ++i)
        {
            VkDescriptorSetLayout layout{};
            std::reverse(bindings.begin(), bindings.end());
            Check(manager->CreateDesciptorSetLayoutWithFlags(bindings, {0,0}, 0, layout)
                && layout == shared, "Equivalent definitions did not share a layout");
            manager->ReleaseDescriptorSetLayout(layout);
            Check(!layout && destroys == 0, "Releasing one instance destroyed the shared layout");
        }
        auto changed = bindings;
        const auto different = [&](const std::vector<VkDescriptorSetLayoutBinding>& value,
            const std::vector<VkDescriptorBindingFlags>& flags, VkDescriptorSetLayoutCreateFlags layoutFlags)
        {
            VkDescriptorSetLayout layout{};
            Check(manager->CreateDesciptorSetLayoutWithFlags(value,flags,layoutFlags,layout)
                && layout != shared, "Incompatible definitions were merged");
            manager->ReleaseDescriptorSetLayout(layout);
        };
        changed[0].descriptorCount = 4; different(changed, {}, 0);
        changed = bindings; changed[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; different(changed, {}, 0);
        changed = bindings; changed[0].stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT; different(changed, {}, 0);
        changed = bindings; changed[0].binding = 8; different(changed, {}, 0);
        different(bindings, {VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,0}, 0);
        different(bindings, {VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,0}, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT);
        Check(manager->GetDiagnostics().updateAfterBindLayoutCount == 1, "Bindless pool classification was lost");
        VkDescriptorSetLayout invalid{};
        Check(!manager->CreateDesciptorSetLayoutWithFlags(bindings,{0},0,invalid), "Mismatched flags accepted");
        changed = bindings; changed[1].binding = changed[0].binding;
        Check(!manager->CreateDesciptorSetLayout(changed,invalid), "Duplicate bindings accepted");
        // 带 immutable sampler 的布局保留调用方生命周期，不能留存失效采样器引用。
        const VkSampler sampler = reinterpret_cast<VkSampler>(uintptr_t(42));
        changed = bindings; changed[0].pImmutableSamplers = &sampler;
        VkDescriptorSetLayout immutable{};
        Check(manager->CreateDesciptorSetLayout(changed,immutable), "Immutable sampler layout failed");
        manager->ReleaseDescriptorSetLayout(immutable);
        Check(destroys == 1, "Immutable sampler layout did not release with its owner");
        const auto saved = shared;
        manager->ReleaseDescriptorSetLayout(shared);
        Check(manager->CreateDesciptorSetLayout(bindings,shared) && shared == saved, "Repeated effect recreated its layout");
        const auto stats = manager->GetDiagnostics();
        Check(stats.sharedLayoutCount == 7 && stats.layoutCacheHits == 257, "Layout cache grew per instance");
        manager->DestroyDescriptorPool();
        Check(destroys == creates && manager->GetDiagnostics().sharedLayoutCount == 0, "Device teardown leaked or double-destroyed layouts");
        std::cout << "[DescriptorSharing] PASS: 256 equivalent instances; reordered bindings; independent release; incompatible flags/types/counts/stages; repeated effects; device cleanup\n";
        return true;
    }
    catch (const std::exception& e) { std::cerr << "[DescriptorSharing] " << e.what() << '\n'; return false; }
}
