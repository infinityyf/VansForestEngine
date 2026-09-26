#pragma once

#include "Windows/VansBaseWindowComponent.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VansGraphics
{
	// Owns editor tool windows in their stable draw/destruction order while
	// providing one type-safe lookup path for cross-window coordination.
	class VansEditorWindowRegistry final
	{
	public:
		template <typename T, typename... Args>
		T& Add(Args&&... args)
		{
			const std::type_index type = std::type_index(typeid(T));
			if (m_ByType.find(type) != m_ByType.end())
				throw std::logic_error("Editor window type is already registered");

			auto window = std::make_unique<T>(std::forward<Args>(args)...);
			T* instance = window.get();
			m_Windows.push_back(std::move(window));
			m_ByType.emplace(type, instance);
			return *instance;
		}

		template <typename T>
		T* Find() const
		{
			const auto it = m_ByType.find(std::type_index(typeid(T)));
			return it == m_ByType.end() ? nullptr : static_cast<T*>(it->second);
		}

		const std::vector<std::unique_ptr<VansBaseWindowComponent>>& All() const
		{
			return m_Windows;
		}

		std::size_t Size() const { return m_Windows.size(); }
		bool Empty() const { return m_Windows.empty(); }

		void Clear()
		{
			m_ByType.clear();
			m_Windows.clear();
		}

	private:
		std::vector<std::unique_ptr<VansBaseWindowComponent>> m_Windows;
		std::unordered_map<std::type_index, VansBaseWindowComponent*> m_ByType;
	};
}
