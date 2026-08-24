#include "VansVKDrawInstanceArena.h"

#include "../../Util/VansLog.h"

bool VansGraphics::VansVKDrawInstanceArena::Initialize(VkDevice device)
{
	if (IsReady())
		return true;

	const VkDeviceSize recordCount =
		static_cast<VkDeviceSize>(RecordsPerFrame) * FrameSlotCount;
	if (!m_Buffer.CreatVulkanBuffer(
		device,
		recordCount * sizeof(VansDrawInstanceDataGPU),
		VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		return false;
	}
	if (!m_Buffer.PersistentMap())
	{
		m_Buffer.DestroyVulkanBuffer(device);
		return false;
	}

	m_FrameSlot = 0;
	m_RecordCount = 0;
	return true;
}

void VansGraphics::VansVKDrawInstanceArena::Destroy(VkDevice device)
{
	if (m_Buffer.GetNativeBuffer() != VK_NULL_HANDLE)
		m_Buffer.DestroyVulkanBuffer(device);
	m_FrameSlot = 0;
	m_RecordCount = 0;
}

void VansGraphics::VansVKDrawInstanceArena::BeginFrame(std::uint32_t frameSlot)
{
	m_FrameSlot = frameSlot % FrameSlotCount;
	m_RecordCount = 0;
}

std::uint32_t VansGraphics::VansVKDrawInstanceArena::Upload(
	const std::vector<VansDrawInstanceDataGPU>& records)
{
	const std::uint32_t frameBase = m_FrameSlot * RecordsPerFrame;
	if (records.empty())
		return frameBase + m_RecordCount;
	if (!IsReady())
	{
		VANS_LOG_ERROR("[VansVKDrawInstanceArena] Upload requested before initialization.");
		return InvalidRecordOffset;
	}
	if (records.size() > RecordsPerFrame - m_RecordCount)
	{
		VANS_LOG_ERROR("[VansVKDrawInstanceArena] Frame segment exhausted: requested="
			<< records.size() << ", used=" << m_RecordCount
			<< ", capacity=" << RecordsPerFrame);
		return InvalidRecordOffset;
	}

	const std::uint32_t firstRecord = frameBase + m_RecordCount;
	const VkDeviceSize byteOffset =
		static_cast<VkDeviceSize>(firstRecord) * sizeof(VansDrawInstanceDataGPU);
	const VkDeviceSize byteSize =
		static_cast<VkDeviceSize>(records.size()) * sizeof(VansDrawInstanceDataGPU);
	m_Buffer.UpdateMapped(records.data(), byteOffset, byteSize);
	m_RecordCount += static_cast<std::uint32_t>(records.size());
	return firstRecord;
}

bool VansGraphics::VansVKDrawInstanceArena::IsReady() const
{
	return m_Buffer.GetNativeBuffer() != VK_NULL_HANDLE && m_Buffer.IsMapped();
}
