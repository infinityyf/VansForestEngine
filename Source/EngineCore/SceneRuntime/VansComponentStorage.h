#pragma once

#include "VansRuntimeHandle.h"

#include <algorithm>
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
	virtual VansComponentHandle FindFirstOwnedBy(VansEntityHandle owner) const = 0;
	virtual void RecomputeEffectiveEnabled(const std::function<bool(VansEntityHandle)>& activeQuery) = 0;
	virtual void Clear() = 0;
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
		m_OwnerDenseIndices[OwnerKey(owner)].push_back(dense);
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

	T* FindFirstEffectiveOwnedBy(VansEntityHandle owner)
	{
		const auto found = m_OwnerDenseIndices.find(OwnerKey(owner));
		if (found == m_OwnerDenseIndices.end())
			return nullptr;
		for (std::uint32_t dense : found->second)
			if (m_Headers[dense].effectiveEnabled)
				return &m_Data[dense];
		return nullptr;
	}

	const T* FindFirstEffectiveOwnedBy(VansEntityHandle owner) const
	{
		const auto found = m_OwnerDenseIndices.find(OwnerKey(owner));
		if (found == m_OwnerDenseIndices.end())
			return nullptr;
		for (std::uint32_t dense : found->second)
			if (m_Headers[dense].effectiveEnabled)
				return &m_Data[dense];
		return nullptr;
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
		VansComponentHeader* header = MutableHeader(handle);
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
		VansComponentHeader* header = MutableHeader(handle);
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
		RemoveOwnerDenseIndex(m_Headers[dense].owner, dense);
		const std::uint32_t last = static_cast<std::uint32_t>(m_Data.size() - 1);
		if (dense != last)
		{
			// swap-remove 后仍按 dense 顺序返回，保持既有首组件和遍历语义。
			auto& movedIndices = m_OwnerDenseIndices.at(OwnerKey(m_Headers[last].owner));
			movedIndices.erase(std::lower_bound(movedIndices.begin(), movedIndices.end(), last));
			movedIndices.insert(std::lower_bound(movedIndices.begin(), movedIndices.end(), dense), dense);
			m_Data[dense] = std::move(m_Data[last]);
			m_Headers[dense] = std::move(m_Headers[last]);
			m_SlotToDense[m_Headers[dense].self.index] = dense;
			if (!m_Headers[dense].stableGuid.empty())
				m_GuidIndex[m_Headers[dense].stableGuid] = m_Headers[dense].self;
		}

		m_Data.pop_back();
		m_Headers.pop_back();
		m_SlotToDense[handle.index] = VansInvalidRuntimeIndex;
		m_SlotGenerations[handle.index] = NextRuntimeGeneration(
			m_SlotGenerations[handle.index]);
		m_FreeSlots.push_back(handle.index);
		return true;
	}

	void Clear() override
	{
		for (const VansComponentHeader& header : m_Headers)
		{
			const std::uint32_t slot = header.self.index;
			m_SlotGenerations[slot] = NextRuntimeGeneration(m_SlotGenerations[slot]);
			m_SlotToDense[slot] = VansInvalidRuntimeIndex;
		}

		m_Data.clear();
		m_Headers.clear();
		m_OwnerDenseIndices.clear();
		m_GuidIndex.clear();
		m_FreeSlots.clear();
		m_FreeSlots.reserve(m_SlotGenerations.size());
		for (std::size_t slot = m_SlotGenerations.size(); slot > 0; --slot)
			m_FreeSlots.push_back(static_cast<std::uint32_t>(slot - 1));
	}

	void RemoveOwnedBy(VansEntityHandle owner) override
	{
		for (;;)
		{
			const auto found = m_OwnerDenseIndices.find(OwnerKey(owner));
			if (found == m_OwnerDenseIndices.end())
				break;
			Remove(m_Headers[found->second.back()].self);
		}
	}

	void CollectOwnedBy(VansEntityHandle owner, std::vector<VansComponentHandle>& outComponents) const override
	{
		const auto found = m_OwnerDenseIndices.find(OwnerKey(owner));
		if (found != m_OwnerDenseIndices.end())
			for (std::uint32_t dense : found->second)
				outComponents.push_back(m_Headers[dense].self);
	}

	VansComponentHandle FindFirstOwnedBy(VansEntityHandle owner) const override
	{
		const auto found = m_OwnerDenseIndices.find(OwnerKey(owner));
		return found == m_OwnerDenseIndices.end()
			? VansComponentHandle{} : m_Headers[found->second.front()].self;
	}

	void RecomputeEffectiveEnabled(const std::function<bool(VansEntityHandle)>& activeQuery) override
	{
		for (VansComponentHeader& header : m_Headers)
			header.effectiveEnabled = header.selfEnabled && activeQuery(header.owner);
	}

	const std::vector<T>& DenseData() const { return m_Data; }
	std::vector<T>& DenseData() { return m_Data; }
	const std::vector<VansComponentHeader>& Headers() const { return m_Headers; }

private:
	static std::uint64_t OwnerKey(VansEntityHandle owner)
	{
		return (static_cast<std::uint64_t>(owner.generation) << 32) | owner.index;
	}

	VansComponentHeader* MutableHeader(VansComponentHandle handle)
	{
		const std::uint32_t dense = DenseIndexFor(handle);
		return dense == VansInvalidRuntimeIndex ? nullptr : &m_Headers[dense];
	}

	void RemoveOwnerDenseIndex(VansEntityHandle owner, std::uint32_t dense)
	{
		auto found = m_OwnerDenseIndices.find(OwnerKey(owner));
		auto& indices = found->second;
		indices.erase(std::lower_bound(indices.begin(), indices.end(), dense));
		if (indices.empty())
			m_OwnerDenseIndices.erase(found);
	}

	std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> m_OwnerDenseIndices;
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
