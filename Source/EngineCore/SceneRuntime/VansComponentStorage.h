#pragma once

#include "VansRuntimeHandle.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vans
{
struct VansComponentHeader
{
	VansEntityHandle owner;
	std::string stableGuid;
	VansComponentHandle self;
	bool selfEnabled = true;
	bool effectiveEnabled = true;
	std::uint32_t dirtyMask = 0;
};

class IVansComponentStorage
{
public:
	virtual ~IVansComponentStorage() = default;
	virtual std::uint16_t GetTypeId() const = 0;
	virtual bool Contains(VansComponentHandle handle) const = 0;
	virtual VansComponentHandle FindByStableGuid(const std::string& stableGuid) const = 0;
	virtual const VansComponentHeader* GetHeaderUntyped(VansComponentHandle handle) const = 0;
	virtual bool SetEnabled(
		VansComponentHandle handle,
		bool enabled,
		const std::function<bool(VansEntityHandle)>& activeQuery) = 0;
	virtual bool Remove(VansComponentHandle handle) = 0;
	virtual void RemoveOwnedBy(VansEntityHandle owner) = 0;
	virtual void CollectOwnedBy(VansEntityHandle owner, std::vector<VansComponentHandle>& outComponents) const = 0;
	virtual void RecomputeEffectiveEnabled(const std::function<bool(VansEntityHandle)>& activeQuery) = 0;
	virtual std::size_t Size() const = 0;
};

template <typename T>
class VansComponentStorage final : public IVansComponentStorage
{
public:
	explicit VansComponentStorage(std::uint16_t typeId)
		: m_TypeId(typeId)
	{
	}

	std::uint16_t GetTypeId() const override { return m_TypeId; }
	std::size_t Size() const override { return m_Data.size(); }

	VansComponentHandle Add(
		VansEntityHandle owner,
		T value,
		std::string stableGuid = std::string(),
		bool enabled = true,
		bool ownerActive = true)
	{
		std::uint32_t slot = VansInvalidRuntimeIndex;
		if (!m_FreeSlots.empty())
		{
			slot = m_FreeSlots.back();
			m_FreeSlots.pop_back();
		}
		else
		{
			slot = static_cast<std::uint32_t>(m_SlotGenerations.size());
			m_SlotGenerations.push_back(1);
			m_SlotToDense.push_back(VansInvalidRuntimeIndex);
		}

		const std::uint32_t dense = static_cast<std::uint32_t>(m_Data.size());
		VansComponentHandle handle{ m_TypeId, slot, m_SlotGenerations[slot] };
		VansComponentHeader header;
		header.owner = owner;
		header.stableGuid = std::move(stableGuid);
		header.self = handle;
		header.selfEnabled = enabled;
		header.effectiveEnabled = enabled && ownerActive;

		m_Data.push_back(std::move(value));
		m_Headers.push_back(std::move(header));
		m_SlotToDense[slot] = dense;
		if (!m_Headers.back().stableGuid.empty())
			m_GuidIndex[m_Headers.back().stableGuid] = handle;
		return handle;
	}

	bool Contains(VansComponentHandle handle) const override
	{
		return DenseIndexFor(handle) != VansInvalidRuntimeIndex;
	}

	VansComponentHandle FindByStableGuid(const std::string& stableGuid) const override
	{
		const auto found = m_GuidIndex.find(stableGuid);
		if (found == m_GuidIndex.end())
			return VansComponentHandle{};
		return Contains(found->second) ? found->second : VansComponentHandle{};
	}

	T* Get(VansComponentHandle handle)
	{
		const std::uint32_t dense = DenseIndexFor(handle);
		return dense == VansInvalidRuntimeIndex ? nullptr : &m_Data[dense];
	}

	const T* Get(VansComponentHandle handle) const
	{
		const std::uint32_t dense = DenseIndexFor(handle);
		return dense == VansInvalidRuntimeIndex ? nullptr : &m_Data[dense];
	}

	VansComponentHeader* GetHeader(VansComponentHandle handle)
	{
		const std::uint32_t dense = DenseIndexFor(handle);
		return dense == VansInvalidRuntimeIndex ? nullptr : &m_Headers[dense];
	}

	const VansComponentHeader* GetHeader(VansComponentHandle handle) const
	{
		const std::uint32_t dense = DenseIndexFor(handle);
		return dense == VansInvalidRuntimeIndex ? nullptr : &m_Headers[dense];
	}

	const VansComponentHeader* GetHeaderUntyped(VansComponentHandle handle) const override
	{
		return GetHeader(handle);
	}

	bool SetEnabled(VansComponentHandle handle, bool enabled, bool ownerActive)
	{
		VansComponentHeader* header = GetHeader(handle);
		if (!header)
			return false;
		header->selfEnabled = enabled;
		header->effectiveEnabled = enabled && ownerActive;
		return true;
	}

	bool SetEnabled(
		VansComponentHandle handle,
		bool enabled,
		const std::function<bool(VansEntityHandle)>& activeQuery) override
	{
		VansComponentHeader* header = GetHeader(handle);
		if (!header)
			return false;
		header->selfEnabled = enabled;
		header->effectiveEnabled = enabled && activeQuery(header->owner);
		return true;
	}

	bool Remove(VansComponentHandle handle) override
	{
		const std::uint32_t dense = DenseIndexFor(handle);
		if (dense == VansInvalidRuntimeIndex)
			return false;

		if (!m_Headers[dense].stableGuid.empty())
			m_GuidIndex.erase(m_Headers[dense].stableGuid);
		const std::uint32_t last = static_cast<std::uint32_t>(m_Data.size() - 1);
		if (dense != last)
		{
			m_Data[dense] = std::move(m_Data[last]);
			m_Headers[dense] = std::move(m_Headers[last]);
			m_SlotToDense[m_Headers[dense].self.index] = dense;
			if (!m_Headers[dense].stableGuid.empty())
				m_GuidIndex[m_Headers[dense].stableGuid] = m_Headers[dense].self;
		}

		m_Data.pop_back();
		m_Headers.pop_back();
		m_SlotToDense[handle.index] = VansInvalidRuntimeIndex;
		++m_SlotGenerations[handle.index];
		m_FreeSlots.push_back(handle.index);
		return true;
	}

	void RemoveOwnedBy(VansEntityHandle owner) override
	{
		for (std::uint32_t dense = static_cast<std::uint32_t>(m_Headers.size()); dense > 0; --dense)
		{
			const VansComponentHeader& header = m_Headers[dense - 1];
			if (header.owner == owner)
				Remove(header.self);
		}
	}

	void CollectOwnedBy(VansEntityHandle owner, std::vector<VansComponentHandle>& outComponents) const override
	{
		for (const VansComponentHeader& header : m_Headers)
			if (header.owner == owner)
				outComponents.push_back(header.self);
	}

	void RecomputeEffectiveEnabled(const std::function<bool(VansEntityHandle)>& activeQuery) override
	{
		for (VansComponentHeader& header : m_Headers)
			header.effectiveEnabled = header.selfEnabled && activeQuery(header.owner);
	}

	const std::vector<T>& DenseData() const { return m_Data; }
	std::vector<T>& DenseData() { return m_Data; }
	const std::vector<VansComponentHeader>& Headers() const { return m_Headers; }
	std::vector<VansComponentHeader>& Headers() { return m_Headers; }

private:
	std::uint32_t DenseIndexFor(VansComponentHandle handle) const
	{
		if (handle.typeId != m_TypeId ||
			handle.index >= m_SlotGenerations.size() ||
			m_SlotGenerations[handle.index] != handle.generation)
		{
			return VansInvalidRuntimeIndex;
		}
		const std::uint32_t dense = m_SlotToDense[handle.index];
		return dense < m_Data.size() ? dense : VansInvalidRuntimeIndex;
	}

	std::uint16_t m_TypeId = VansInvalidComponentTypeId;
	std::vector<T> m_Data;
	std::vector<VansComponentHeader> m_Headers;
	std::vector<std::uint32_t> m_SlotGenerations;
	std::vector<std::uint32_t> m_SlotToDense;
	std::vector<std::uint32_t> m_FreeSlots;
	std::unordered_map<std::string, VansComponentHandle> m_GuidIndex;
};
}
