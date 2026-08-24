#pragma once

#include "VansRenderProxyHandle.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

namespace VansGraphics
{
	constexpr std::uint32_t VANS_INVALID_RENDER_TRANSFORM_SLOT =
		(std::numeric_limits<std::uint32_t>::max)();

	// 代理中的静态绑定由类型化 mutation 更新；逐帧矩阵仍由 frame snapshot 提供。
	struct VansRenderProxyStaticData final
	{
		std::uint32_t transformSlot = VANS_INVALID_RENDER_TRANSFORM_SLOT;
		bool enabled = true;

		friend bool operator==(
			const VansRenderProxyStaticData& left,
			const VansRenderProxyStaticData& right)
		{
			return left.transformSlot == right.transformSlot && left.enabled == right.enabled;
		}

		friend bool operator!=(
			const VansRenderProxyStaticData& left,
			const VansRenderProxyStaticData& right)
		{
			return !(left == right);
		}
	};

	struct VansCreateRenderProxy final
	{
		VansRenderProxyHandle handle;
		VansRenderProxyStaticData staticData;
	};

	struct VansUpdateRenderProxy final
	{
		VansRenderProxyHandle handle;
		VansRenderProxyStaticData staticData;
	};

	struct VansDestroyRenderProxy final
	{
		VansRenderProxyHandle handle;
	};

	using VansRenderMutation = std::variant<
		VansCreateRenderProxy,
		VansUpdateRenderProxy,
		VansDestroyRenderProxy>;

	// 跨线程 mutation 只允许封闭 value command；禁止 callback、Scene/Node 指针和 Vk handle。
	class VansRenderMutationBatch final
	{
	public:
		VansRenderMutationBatch() = default;
		VansRenderMutationBatch(const VansRenderMutationBatch&) = delete;
		VansRenderMutationBatch& operator=(const VansRenderMutationBatch&) = delete;
		VansRenderMutationBatch(VansRenderMutationBatch&&) noexcept = default;
		VansRenderMutationBatch& operator=(VansRenderMutationBatch&&) noexcept = default;

		void Reserve(std::size_t count) { m_Commands.reserve(count); }
		void AddCreate(VansRenderProxyHandle handle, VansRenderProxyStaticData staticData);
		void AddUpdate(VansRenderProxyHandle handle, VansRenderProxyStaticData staticData);
		void AddDestroy(VansRenderProxyHandle handle);
		void Append(VansRenderMutationBatch&& other);

		bool Empty() const { return m_Commands.empty(); }
		std::size_t Size() const { return m_Commands.size(); }
		const std::vector<VansRenderMutation>& Commands() const { return m_Commands; }

	private:
		std::vector<VansRenderMutation> m_Commands;
	};

	// Main-only opaque handle allocator。它不解析 RT storage，只保证 index/generation 不被误复用。
	class VansRenderProxyHandleAllocator final
	{
	public:
		VansRenderProxyHandle Allocate();
		bool Release(VansRenderProxyHandle handle);
		std::size_t ActiveCount() const { return m_ActiveCount; }

	private:
		std::vector<std::uint32_t> m_Generations;
		std::vector<bool> m_Allocated;
		std::vector<std::uint32_t> m_FreeIndices;
		std::size_t m_ActiveCount = 0;
	};

	// 此 store 只在 render-consume 侧应用 mutation 和解析句柄。
	class VansRenderWorld final
	{
	public:
		bool Apply(const VansRenderMutationBatch& batch);
		const VansRenderProxyStaticData* Resolve(VansRenderProxyHandle handle) const;

		std::size_t ActiveProxyCount() const { return m_ActiveProxyCount; }
		std::uint64_t RejectedMutationBatchCount() const { return m_RejectedMutationBatchCount; }

	private:
		struct Slot final
		{
			std::uint32_t generation = 0;
			bool active = false;
			VansRenderProxyStaticData staticData;
		};

		std::vector<Slot> m_Slots;
		std::size_t m_ActiveProxyCount = 0;
		std::uint64_t m_RejectedMutationBatchCount = 0;
	};
}
