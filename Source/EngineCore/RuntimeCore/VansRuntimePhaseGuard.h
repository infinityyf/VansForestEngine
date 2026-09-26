#pragma once

#include <functional>
#include <utility>

namespace Vans
{
class VansRuntimePhaseGuard final
{
  public:
	explicit VansRuntimePhaseGuard(std::function<void()> rollback) : m_Rollback(std::move(rollback))
	{
	}

	~VansRuntimePhaseGuard()
	{
		if (m_Active)
			m_Rollback();
	}

	VansRuntimePhaseGuard(const VansRuntimePhaseGuard&) = delete;
	VansRuntimePhaseGuard& operator=(const VansRuntimePhaseGuard&) = delete;

	void Commit()
	{
		m_Active = false;
	}

  private:
	std::function<void()> m_Rollback;
	bool m_Active = true;
};
} // namespace Vans
