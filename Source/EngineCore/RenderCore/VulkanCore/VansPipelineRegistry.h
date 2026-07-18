#pragma once

#include "VansPipeline.h"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	struct VansPipelineRegistryMapStats
	{
		size_t bucketCount = 0;
		size_t activeCount = 0;
		size_t expiredCount = 0;
	};

	struct VansPipelineRegistryStats
	{
		VansPipelineRegistryMapStats graphics;
		VansPipelineRegistryMapStats compute;
		VansPipelineRegistryMapStats rayTracing;

		size_t GetTotalActiveCount() const
		{
			return graphics.activeCount + compute.activeCount + rayTracing.activeCount;
		}

		size_t GetTotalExpiredCount() const
		{
			return graphics.expiredCount + compute.expiredCount + rayTracing.expiredCount;
		}
	};

	class VansPipelineRegistry
	{
	public:
		static VansPipelineRegistry& Get();

		std::shared_ptr<VansVKGraphicsPipeline> FindGraphicsPipeline(const VansPipelineDescriptorKey& key);
		std::shared_ptr<VansVKComputePipeline> FindComputePipeline(const VansPipelineDescriptorKey& key);
		std::shared_ptr<VansVKRayTracingPipeline> FindRayTracingPipeline(const VansPipelineDescriptorKey& key);

		std::shared_ptr<VansVKGraphicsPipeline> StoreGraphicsPipeline(const std::shared_ptr<VansVKGraphicsPipeline>& pipeline);
		std::shared_ptr<VansVKComputePipeline> StoreComputePipeline(const std::shared_ptr<VansVKComputePipeline>& pipeline);
		std::shared_ptr<VansVKRayTracingPipeline> StoreRayTracingPipeline(const std::shared_ptr<VansVKRayTracingPipeline>& pipeline);

		void Compact();
		void Clear();
		VansPipelineRegistryStats GetStats() const;

	private:
		VansPipelineRegistry() = default;

		template<typename PipelineT>
		std::shared_ptr<PipelineT> FindPipeline(
			std::unordered_map<uint64_t, std::vector<std::weak_ptr<PipelineT>>>& pipelines,
			const VansPipelineDescriptorKey& key);

		template<typename PipelineT>
		std::shared_ptr<PipelineT> StorePipeline(
			std::unordered_map<uint64_t, std::vector<std::weak_ptr<PipelineT>>>& pipelines,
			const std::shared_ptr<PipelineT>& pipeline);

		template<typename PipelineT>
		void CompactMap(std::unordered_map<uint64_t, std::vector<std::weak_ptr<PipelineT>>>& pipelines);

		template<typename PipelineT>
		static VansPipelineRegistryMapStats GetMapStats(
			const std::unordered_map<uint64_t, std::vector<std::weak_ptr<PipelineT>>>& pipelines);

		std::unordered_map<uint64_t, std::vector<std::weak_ptr<VansVKGraphicsPipeline>>> m_GraphicsPipelines;
		std::unordered_map<uint64_t, std::vector<std::weak_ptr<VansVKComputePipeline>>> m_ComputePipelines;
		std::unordered_map<uint64_t, std::vector<std::weak_ptr<VansVKRayTracingPipeline>>> m_RayTracingPipelines;
	};
}
