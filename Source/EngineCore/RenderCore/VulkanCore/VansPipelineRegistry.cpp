#include "VansPipelineRegistry.h"

#include <algorithm>

namespace VansGraphics
{
	VansPipelineRegistry& VansPipelineRegistry::Get()
	{
		static VansPipelineRegistry registry;
		return registry;
	}

	std::shared_ptr<VansVKGraphicsPipeline> VansPipelineRegistry::FindGraphicsPipeline(const VansPipelineDescriptorKey& key)
	{
		return FindPipeline(m_GraphicsPipelines, key);
	}

	std::shared_ptr<VansVKComputePipeline> VansPipelineRegistry::FindComputePipeline(const VansPipelineDescriptorKey& key)
	{
		return FindPipeline(m_ComputePipelines, key);
	}

	std::shared_ptr<VansVKRayTracingPipeline> VansPipelineRegistry::FindRayTracingPipeline(const VansPipelineDescriptorKey& key)
	{
		return FindPipeline(m_RayTracingPipelines, key);
	}

	std::shared_ptr<VansVKGraphicsPipeline> VansPipelineRegistry::StoreGraphicsPipeline(const std::shared_ptr<VansVKGraphicsPipeline>& pipeline)
	{
		return StorePipeline(m_GraphicsPipelines, pipeline);
	}

	std::shared_ptr<VansVKComputePipeline> VansPipelineRegistry::StoreComputePipeline(const std::shared_ptr<VansVKComputePipeline>& pipeline)
	{
		return StorePipeline(m_ComputePipelines, pipeline);
	}

	std::shared_ptr<VansVKRayTracingPipeline> VansPipelineRegistry::StoreRayTracingPipeline(const std::shared_ptr<VansVKRayTracingPipeline>& pipeline)
	{
		return StorePipeline(m_RayTracingPipelines, pipeline);
	}

	void VansPipelineRegistry::Compact()
	{
		CompactMap(m_GraphicsPipelines);
		CompactMap(m_ComputePipelines);
		CompactMap(m_RayTracingPipelines);
	}

	void VansPipelineRegistry::Clear()
	{
		m_GraphicsPipelines.clear();
		m_ComputePipelines.clear();
		m_RayTracingPipelines.clear();
	}

	VansPipelineRegistryStats VansPipelineRegistry::GetStats() const
	{
		VansPipelineRegistryStats stats{};
		stats.graphics = GetMapStats(m_GraphicsPipelines);
		stats.compute = GetMapStats(m_ComputePipelines);
		stats.rayTracing = GetMapStats(m_RayTracingPipelines);
		return stats;
	}

	template<typename PipelineT>
	std::shared_ptr<PipelineT> VansPipelineRegistry::FindPipeline(
		std::unordered_map<uint64_t, std::vector<std::weak_ptr<PipelineT>>>& pipelines,
		const VansPipelineDescriptorKey& key)
	{
		if (!key.IsValid())
		{
			return nullptr;
		}

		auto it = pipelines.find(key.hash);
		if (it == pipelines.end())
		{
			return nullptr;
		}

		auto& bucket = it->second;
		for (auto bucketIt = bucket.begin(); bucketIt != bucket.end();)
		{
			std::shared_ptr<PipelineT> pipeline = bucketIt->lock();
			if (pipeline == nullptr)
			{
				bucketIt = bucket.erase(bucketIt);
				continue;
			}

			if (pipeline->GetDescriptorKey().text == key.text)
			{
				return pipeline;
			}

			++bucketIt;
		}

		if (bucket.empty())
		{
			pipelines.erase(it);
		}

		return nullptr;
	}

	template<typename PipelineT>
	std::shared_ptr<PipelineT> VansPipelineRegistry::StorePipeline(
		std::unordered_map<uint64_t, std::vector<std::weak_ptr<PipelineT>>>& pipelines,
		const std::shared_ptr<PipelineT>& pipeline)
	{
		if (pipeline == nullptr || !pipeline->GetDescriptorKey().IsValid())
		{
			return pipeline;
		}

		if (std::shared_ptr<PipelineT> cached = FindPipeline(pipelines, pipeline->GetDescriptorKey()))
		{
			return cached;
		}

		pipelines[pipeline->GetDescriptorKey().hash].emplace_back(pipeline);
		return pipeline;
	}

	template<typename PipelineT>
	void VansPipelineRegistry::CompactMap(std::unordered_map<uint64_t, std::vector<std::weak_ptr<PipelineT>>>& pipelines)
	{
		for (auto it = pipelines.begin(); it != pipelines.end();)
		{
			auto& bucket = it->second;
			bucket.erase(
				std::remove_if(
					bucket.begin(),
					bucket.end(),
					[](const std::weak_ptr<PipelineT>& pipeline) { return pipeline.expired(); }),
				bucket.end());

			if (bucket.empty())
			{
				it = pipelines.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	template<typename PipelineT>
	VansPipelineRegistryMapStats VansPipelineRegistry::GetMapStats(
		const std::unordered_map<uint64_t, std::vector<std::weak_ptr<PipelineT>>>& pipelines)
	{
		VansPipelineRegistryMapStats stats{};
		stats.bucketCount = pipelines.size();

		for (const auto& bucketPair : pipelines)
		{
			for (const auto& pipeline : bucketPair.second)
			{
				if (pipeline.expired())
				{
					++stats.expiredCount;
				}
				else
				{
					++stats.activeCount;
				}
			}
		}

		return stats;
	}
}
