#pragma once

#include "VansVKBuffer.h"
#include "../VansDrawSubmission.h"

#include <cstdint>
#include <vector>

namespace VansGraphics
{
	// Vulkan 后端拥有的逐实例上传区。每个 frame-context slot 使用独立分段，
	// 因此 RT 在复用分段前只依赖对应 slot fence，不依赖 Scene 生命周期。
	class VansVKDrawInstanceArena final
	{
	public:
		static constexpr std::uint32_t FrameSlotCount = 2;
		static constexpr std::uint32_t RecordsPerFrame = 262144;
		static constexpr std::uint32_t InvalidRecordOffset = UINT32_MAX;

		bool Initialize(VkDevice device);
		void Destroy(VkDevice device);
		void BeginFrame(std::uint32_t frameSlot);
		std::uint32_t Upload(const std::vector<VansDrawInstanceDataGPU>& records);

		bool IsReady() const;
		VansVKBuffer& GetBuffer() { return m_Buffer; }
		const VansVKBuffer& GetBuffer() const { return m_Buffer; }

	private:
		VansVKBuffer m_Buffer;
		std::uint32_t m_FrameSlot = 0;
		std::uint32_t m_RecordCount = 0;
	};
}
