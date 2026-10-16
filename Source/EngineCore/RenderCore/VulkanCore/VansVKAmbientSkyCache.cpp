#include "VansVKAmbientSkyCache.h"
#include "../GICore/VansGIMemoryPublication.h"
#include "VansVKDevice.h"
#include "VansVKDescriptorManager.h"
#include "VansDescriptorSetLayouts.h"
#include "../VansRuntimeRenderTextureManager.h"
#include "../VansShaderManager.h"
#include "../GICore/VansGISettings.h"
#include "../../Util/VansLog.h"

void VansGraphics::VansVKAmbientSkyCache::Initialize(VansVKDevice& device, VansVKCommandBuffer& command,
    VansRuntimeRenderTextureManager& textures, const GIAmbientSkyCacheSettings& settings,
    const glm::vec3& cameraPosition, bool hasWorld)
{
    m_MemoryPublication = device.GetGIMemoryPublication();
	auto& cache = *this;
	VkDevice logicalDevice = device.GetLogicDevice();
	const bool cacheEnabled = settings.enabled && hasWorld;
	VANS_LOG("[AmbientSkyCache] prepare enabled=" << (cacheEnabled ? 1 : 0)
		<< " spacing=" << settings.gridSpacing << " cellUpdatesPerFrame=" << settings.cellUpdatesPerFrame);

	const uint32_t gridX = cacheEnabled ? 16u : 1u;
	const uint32_t gridY = cacheEnabled ? 8u : 1u;
	const uint32_t gridZ = cacheEnabled ? 16u : 1u;
	const float spacing = settings.gridSpacing;
	const glm::vec3 snapped = glm::floor(cameraPosition / spacing) * spacing;
	cache.origin = cacheEnabled
		? snapped - glm::vec3(gridX, gridY, gridZ) * spacing * 0.5f
		: cameraPosition;
	cache.ringOffset = glm::ivec3(0);
	cache.initialized = cacheEnabled;
	cache.frameOffset = 0u;

	auto createCache = [&](const char* name)
	{
		VansTexture* texture = new VansTexture();
		if (!texture->InitTextureWithoutData(command, gridX, gridY, gridZ,
			VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_VIEW_TYPE_3D, false, true,
			VK_SAMPLER_ADDRESS_MODE_REPEAT))
		{
			delete texture;
			return static_cast<VansTexture*>(nullptr);
		}
		textures.Add(name, texture);
		return texture;
	};
	VansTexture* cacheX = createCache(VansVKAmbientSkyCache::TextureX);
	VansTexture* cacheY = createCache(VansVKAmbientSkyCache::TextureY);
	VansTexture* cacheZ = createCache(VansVKAmbientSkyCache::TextureZ);
	if (cacheX == nullptr || cacheY == nullptr || cacheZ == nullptr)
    {
        RefreshMemory(textures);
        return;
    }

	if (command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
	{
		const VkClearColorValue unknown{};
		command.ClearColorImage(cacheX->GetImage(), VK_IMAGE_LAYOUT_GENERAL, unknown);
		command.ClearColorImage(cacheY->GetImage(), VK_IMAGE_LAYOUT_GENERAL, unknown);
		command.ClearColorImage(cacheZ->GetImage(), VK_IMAGE_LAYOUT_GENERAL, unknown);
		command.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(), logicalDevice,
			{ command.GetVKCommandBuffer() }, {}, {}, command.m_CommandBufferFinishSubmitFence);
		command.ResetCommandBuffer(false);
	}

	VansAmbientSkyCacheParameters info{};
	info.originAndSpacing = glm::vec4(cache.origin, spacing);
	info.gridAndQuery = glm::uvec4(gridX, gridY, gridZ, 0u);
	info.ringOffset = glm::ivec4(cache.ringOffset, 0);
	for (VansVKBuffer& buffer : cache.info)
	{
		buffer.CreatVulkanBuffer(
			logicalDevice, sizeof(info), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		buffer.SetBufferData(&info, 0, sizeof(info));
	}

	if (!cacheEnabled)
    {
        RefreshMemory(textures);
        return;
    }

	cache.shader = VansGraphics::VansShaderManager::Get().FindComputeShader("AmbientSkyTransmittance");
	VansDescriptorSetLayoutFactory::CreateAndAllocate_AmbientSkyCache(
		cache.layout, cache.sets,
		VansCommandRecordingConfig::MaximumFramesInFlight);
	VansAmbientSkyCacheParameters params{};
	params.originAndSpacing = glm::vec4(cache.origin, spacing);
	params.ringOffset = glm::ivec4(cache.ringOffset, 0);
	const uint32_t cellQueries = settings.cellUpdatesPerFrame;
	params.gridAndQuery = glm::uvec4(gridX, gridY, gridZ, cellQueries);
	for (VansVKBuffer& buffer : cache.params)
	{
		buffer.CreatVulkanBuffer(
			logicalDevice, sizeof(params), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		buffer.SetBufferData(&params, 0, sizeof(params));
	}
    RefreshMemory(textures);
}

void VansGraphics::VansVKAmbientSkyCache::Destroy(VkDevice device, VansRuntimeRenderTextureManager& textures)
{
	auto& cache = *this;
	textures.Remove(TextureX);
	textures.Remove(TextureY);
	textures.Remove(TextureZ);
	for (VansVKBuffer& buffer : cache.params)
		buffer.DestroyVulkanBuffer(device);
	for (VansVKBuffer& buffer : cache.info)
		buffer.DestroyVulkanBuffer(device);
	if (auto* descriptorManager = VansVKDescriptorManager::GetInstance())
	{
		descriptorManager->DestroyDescriptorSet(cache.sets);
		descriptorManager->ReleaseDescriptorSetLayout(cache.layout);
	}
	cache.shader = nullptr;
	cache.origin = glm::vec3(0.0f);
	cache.ringOffset = glm::ivec3(0);
	cache.initialized = false;
	cache.frameOffset = 0u;
    RefreshMemory(textures);
    m_MemoryPublication.reset();
}

void VansGraphics::VansVKAmbientSkyCache::RefreshMemory(const VansRuntimeRenderTextureManager& textures) const
{
    if (!m_MemoryPublication) return;
    VansGIMemoryUsage usage;
    for (const char* name : {TextureX, TextureY, TextureZ})
        if (auto* texture = textures.Get(name))
            usage.imageAllocationBytes += texture->GetImage().GetAllocationSize();
    for (const auto* buffers : {&params, &info})
        for (const auto& buffer : *buffers)
        {
            usage.bufferCapacityBytes += buffer.GetBufferSize();
            usage.bufferAllocationBytes += buffer.GetAllocationSize();
        }
    m_MemoryPublication->PublishConsumer(&VansGIMemorySnapshot::ambientSky, usage);
}
