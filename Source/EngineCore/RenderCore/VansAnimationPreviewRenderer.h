#pragma once

#include "../AnimationCore/VansAnimationTypes.h"
#include "../AssetCore/VansSkeletalMeshImportSettings.h"
#include "VulkanCore/VansVKImage.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace VansGraphics
{
	class VansVKDevice;

	struct VansAnimationPreviewView
	{
		float yaw = 0.0f;
		float pitch = 0.0f;
		float zoom = 1.0f;
	};

	struct VansAnimationPreviewRenderStats
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::size_t vertexCount = 0;
		std::size_t sourceTriangleCount = 0;
		std::size_t renderedTriangleCount = 0;
		float renderMilliseconds = 0.0f;
	};

	// RenderCore-owned isolated preview resource. It imports the same skeleton,
	// skin weights, node-transform policy and scale as scene rendering, performs
	// deterministic CPU skinning/rasterization, then publishes a sampled Vulkan
	// image. EngineAPILayer only translates the image to an opaque editor handle.
	class VansAnimationPreviewRenderer
	{
	public:
		VansAnimationPreviewRenderer() = default;
		~VansAnimationPreviewRenderer();
		VansAnimationPreviewRenderer(const VansAnimationPreviewRenderer&) = delete;
		VansAnimationPreviewRenderer& operator=(const VansAnimationPreviewRenderer&) = delete;

		bool PrepareCpu(
			const std::filesystem::path& modelPath,
			float scaleFactor,
			const Vans::VansSkeletalMeshImportSettings& importSettings,
			std::string& error);
		bool InitializeGpuRenderThread(VansVKDevice& device, std::string& error);
		void Shutdown();

		bool RasterizeFrame(
			const BoneMatricesSSBO& boneMatrices,
			const std::vector<glm::vec4>& perBoneVisualizationColors,
			const glm::vec3& modelOffset,
			const VansAnimationPreviewView& view,
			std::string& error);
		bool UploadRenderThread(std::string& error);

		bool IsReady() const { return m_Ready; }
		const Skeleton& GetSkeleton() const { return m_Skeleton; }
		VansVKImage& GetColorImage() { return m_ColorImage; }
		const glm::vec3& GetModelCenter() const { return m_ModelCenter; }
		float GetModelRadius() const { return m_ModelRadius; }
		const VansAnimationPreviewRenderStats& GetStats() const { return m_Stats; }

		static constexpr std::uint32_t PreviewWidth = 512;
		static constexpr std::uint32_t PreviewHeight = 512;

	private:
		struct CpuVertex
		{
			glm::vec3 bindPosition = glm::vec3(0.0f);
			glm::vec3 bindNormal = glm::vec3(0.0f, 1.0f, 0.0f);
			glm::vec3 baseColor = glm::vec3(0.72f);
			VertexBoneData skin;
		};

		struct ProjectedVertex
		{
			float x = 0.0f;
			float y = 0.0f;
			float depth = 0.0f;
			glm::vec3 color = glm::vec3(0.0f);
			bool valid = false;
		};

		bool LoadCpuModel(
			const std::filesystem::path& modelPath,
			float scaleFactor,
			const Vans::VansSkeletalMeshImportSettings& importSettings,
			std::string& error);
		void Rasterize(
			const BoneMatricesSSBO& boneMatrices,
			const std::vector<glm::vec4>& perBoneVisualizationColors,
			const glm::vec3& modelOffset,
			const VansAnimationPreviewView& view);
		VansVKDevice* m_Device = nullptr;
		VansVKImage m_ColorImage;
		Skeleton m_Skeleton;
		std::vector<CpuVertex> m_Vertices;
		std::vector<std::uint32_t> m_Indices;
		std::vector<ProjectedVertex> m_ProjectedVertices;
		std::vector<std::uint8_t> m_Pixels;
		std::vector<float> m_Depth;
		glm::vec3 m_ModelCenter = glm::vec3(0.0f);
		float m_ModelRadius = 1.0f;
		std::size_t m_TriangleStride = 1;
		bool m_Ready = false;
		VansAnimationPreviewRenderStats m_Stats;
	};
}
