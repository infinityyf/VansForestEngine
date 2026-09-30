#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
#include "../VulkanCore/VansVKImage.h"

namespace VansGraphics
{
class VansScene;
class VansVKCommandBuffer;
class VansGraphicsShader;
class VansComputeShader;
struct GlobalStateData;

// RenderThread-owned rain presentation. Weather scalar evolution remains in
// WeatherCore; this object owns only scene-persistent Vulkan descriptors.
class VansRainRenderSystem final
{
public:
	VansRainRenderSystem() = default;
	~VansRainRenderSystem();
	VansRainRenderSystem(const VansRainRenderSystem&) = delete;
	VansRainRenderSystem& operator=(const VansRainRenderSystem&) = delete;

	bool Initialize(VansScene& scene);
	bool RebindSceneTextures();
	void Shutdown();
	void GenerateSurfaceRipple(VansVKCommandBuffer& commandBuffer);
	void Render(VansVKCommandBuffer& commandBuffer, GlobalStateData& globalState);
	VansVKImage* GetSurfaceRippleImage() { return m_SurfaceRippleImage.HasResources() ? &m_SurfaceRippleImage : nullptr; }

	static constexpr std::uint32_t RainLayerCount = 4;
	static constexpr std::uint32_t DoubleConeSegments = 64;
	static constexpr std::uint32_t MaximumSplashCount = 512;

private:
	VansScene* m_Scene = nullptr;
	VansGraphicsShader* m_StreakShader = nullptr;
	VansGraphicsShader* m_SplashShader = nullptr;
	VansComputeShader* m_SurfaceRippleShader = nullptr;
	VkDescriptorSetLayout m_PassLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_SurfaceRippleLayout = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> m_PassSets;
	std::vector<VkDescriptorSet> m_SurfaceRippleSets;
	std::vector<VkDescriptorSetLayout> m_Layouts;
	std::vector<VkDescriptorSet> m_Sets;
	VansVKImage m_SurfaceRippleImage;
	// 16×16 个雨滴种子的圆环波带需要至少约两个纹素，避免生成阶段欠采样。
	static constexpr std::uint32_t SurfaceRippleResolution = 1024;
};
}
