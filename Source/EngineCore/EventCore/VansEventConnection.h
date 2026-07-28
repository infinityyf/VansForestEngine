#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace Vans
{
	class VansEventConnection
	{
	public:
		VansEventConnection() = default;

		explicit VansEventConnection(std::function<void()> disconnect)
			: m_State(std::make_shared<State>())
		{
			m_State->disconnect = std::move(disconnect);
		}

		VansEventConnection(VansEventConnection&& other) noexcept
			: m_State(std::move(other.m_State))
		{
		}

		VansEventConnection& operator=(VansEventConnection&& other) noexcept
		{
			if (this != &other)
			{
				Disconnect();
				m_State = std::move(other.m_State);
			}
			return *this;
		}

		~VansEventConnection()
		{
			Disconnect();
		}

		void Disconnect()
		{
			if (!m_State)
				return;

			std::function<void()> disconnect;
			{
				std::lock_guard<std::mutex> lock(m_State->mutex);
				if (!m_State->connected)
					return;
				m_State->connected = false;
				disconnect = std::move(m_State->disconnect);
			}

			if (disconnect)
				disconnect();
		}

		bool IsConnected() const
		{
			if (!m_State)
				return false;
			std::lock_guard<std::mutex> lock(m_State->mutex);
			return m_State->connected;
		}

		VansEventConnection(const VansEventConnection&) = delete;
		VansEventConnection& operator=(const VansEventConnection&) = delete;

	private:
		struct State
		{
			mutable std::mutex mutex;
			bool connected = true;
			std::function<void()> disconnect;
		};

		std::shared_ptr<State> m_State;
	};

	class VansScopedEventConnections
	{
	public:
		~VansScopedEventConnections()
		{
			DisconnectAll();
		}

		void Add(VansEventConnection connection)
		{
			m_Connections.push_back(std::move(connection));
		}

		void DisconnectAll()
		{
			for (VansEventConnection& connection : m_Connections)
				connection.Disconnect();
			m_Connections.clear();
		}

	private:
		std::vector<VansEventConnection> m_Connections;
	};
}
