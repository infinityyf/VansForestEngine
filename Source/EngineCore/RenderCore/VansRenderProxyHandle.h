#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace VansGraphics
{
	// Main 只分配/传递 opaque handle；只有 render-consume 侧的 RenderWorld 可以解析它。
	struct VansRenderProxyHandle final
	{
		std::uint32_t index = (std::numeric_limits<std::uint32_t>::max)();
		std::uint32_t generation = 0;

		bool IsValid() const
		{
			return index != (std::numeric_limits<std::uint32_t>::max)() && generation != 0;
		}

		friend bool operator==(VansRenderProxyHandle left, VansRenderProxyHandle right)
		{
			return left.index == right.index && left.generation == right.generation;
		}

		friend bool operator!=(VansRenderProxyHandle left, VansRenderProxyHandle right)
		{
			return !(left == right);
		}
	};

	struct VansRenderProxyHandleHash final
	{
		std::size_t operator()(VansRenderProxyHandle handle) const noexcept
		{
			return static_cast<std::size_t>(
				(static_cast<std::uint64_t>(handle.generation) << 32u) |
				static_cast<std::uint64_t>(handle.index));
		}
	};

	// 仅用于必须使用整数 key 的 GPU/debug 协议；CPU 结构应优先保留强类型 handle。
	inline std::uint64_t MakeRenderProxyStableId(VansRenderProxyHandle handle) noexcept
	{
		return handle.IsValid()
			? (static_cast<std::uint64_t>(handle.generation) << 32u) |
				static_cast<std::uint64_t>(handle.index)
			: 0u;
	}
}
