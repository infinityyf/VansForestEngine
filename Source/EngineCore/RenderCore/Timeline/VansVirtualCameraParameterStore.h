#pragma once

#include "../../SceneRuntime/VansRuntimeHandle.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace VansGraphics
{
struct VansVirtualCameraParameters
{
	float fieldOfView = 60.0f;
	float nearClip = 0.1f;
	float farClip = 1000.0f;
};

class VansVirtualCameraParameterStore
{
public:
	const VansVirtualCameraParameters* Find(Vans::VansEntityHandle entity) const
	{
		const auto found = m_Parameters.find(entity);
		return found == m_Parameters.end() ? nullptr : &found->second;
	}

	bool Set(Vans::VansEntityHandle entity, VansVirtualCameraParameters parameters)
	{
		if (!entity.IsValid()) return false;
		m_Parameters.insert_or_assign(entity, parameters);
		return true;
	}

	bool Remove(Vans::VansEntityHandle entity)
	{
		return m_Parameters.erase(entity) != 0;
	}

	void Clear() { m_Parameters.clear(); }
	std::size_t Size() const { return m_Parameters.size(); }

private:
	struct EntityHash
	{
		std::size_t operator()(Vans::VansEntityHandle entity) const noexcept
		{
			const std::uint64_t packed =
				(static_cast<std::uint64_t>(entity.generation) << 32) | entity.index;
			return static_cast<std::size_t>(packed ^ (packed >> 33));
		}
	};

	std::unordered_map<Vans::VansEntityHandle, VansVirtualCameraParameters, EntityHash> m_Parameters;
};
}
