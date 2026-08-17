#pragma once

#include "vulkan/vulkan.h"

#include <string>
#include <vector>

namespace VansGraphics
{
	struct VansFrameSubmitNode;

	struct VansTrackedResourceState
	{
		std::string name;
		VkPipelineStageFlags lastStages = 0;
		VkAccessFlags lastAccess = 0;
		VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		bool lastAccessWasWrite = false;
		bool persistent = false;
		bool hostReadable = false;
		std::string lastNode;
	};

	class VansResourceStateTracker
	{
	public:
		bool ValidateAndBuild(
			const std::vector<VansFrameSubmitNode>& nodes,
			std::string* error = nullptr);

		const std::vector<VansTrackedResourceState>& GetStates() const { return m_States; }
		std::string BuildDebugSummary() const;

	private:
		std::vector<VansTrackedResourceState> m_States;
	};
}
