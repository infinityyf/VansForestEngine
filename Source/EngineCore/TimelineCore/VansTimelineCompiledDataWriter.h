#pragma once

#include "VansTimelineSourceSchema.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace Vans
{
struct VansTimelineCompiledDataView
{
	std::uint32_t byteOffset = 0;
	std::uint32_t byteSize = 0;
	std::uint32_t byteAlignment = 1;
	std::uint32_t valueOffset = 0;
	std::uint32_t valueCount = 0;
};

class VansTimelineCompiledDataWriter
{
public:
	template <typename Value>
	VansTimelineCompiledDataView Write(const Value& value)
	{
		static_assert(std::is_trivially_copyable_v<Value>,
			"Timeline compiled blocks must be trivially copyable");
		const std::size_t alignment = alignof(Value);
		const std::size_t aligned = (m_Bytes.size() + alignment - 1) & ~(alignment - 1);
		m_Bytes.resize(aligned + sizeof(Value));
		std::memcpy(m_Bytes.data() + aligned, &value, sizeof(Value));
		return { static_cast<std::uint32_t>(aligned), static_cast<std::uint32_t>(sizeof(Value)),
			static_cast<std::uint32_t>(alignment), static_cast<std::uint32_t>(m_Values.size()), 0 };
	}

	VansTimelineCompiledDataView WriteValues(std::vector<VansTimelineValue> values);
	bool WriteSchema(
		const VansTimelineSourceSchema& schema,
		const VansSerializedValue& extensionData,
		VansTimelineCompiledDataView& view,
		VansTimelineDiagnostics& diagnostics,
		const VansTimelineId& objectId);

	const std::vector<std::byte>& Bytes() const { return m_Bytes; }
	const std::vector<VansTimelineValue>& Values() const { return m_Values; }

private:
	std::vector<std::byte> m_Bytes;
	std::vector<VansTimelineValue> m_Values;
};

class VansTimelineCompiledDataReader
{
public:
	VansTimelineCompiledDataReader(
		const std::vector<std::byte>& bytes,
		const std::vector<VansTimelineValue>& values)
		: m_Bytes(bytes), m_Values(values) {}

	template <typename Value>
	const Value* Read(const VansTimelineCompiledDataView& view) const
	{
		static_assert(std::is_trivially_copyable_v<Value>);
		if (view.byteSize != sizeof(Value) || view.byteAlignment != alignof(Value) ||
			static_cast<std::size_t>(view.byteOffset) + sizeof(Value) > m_Bytes.size()) return nullptr;
		return reinterpret_cast<const Value*>(m_Bytes.data() + view.byteOffset);
	}

	const VansTimelineValue* ValueAt(const VansTimelineCompiledDataView& view, std::size_t slot) const
	{
		if (slot >= view.valueCount || static_cast<std::size_t>(view.valueOffset) + slot >= m_Values.size())
			return nullptr;
		return &m_Values[view.valueOffset + slot];
	}

private:
	const std::vector<std::byte>& m_Bytes;
	const std::vector<VansTimelineValue>& m_Values;
};
}
