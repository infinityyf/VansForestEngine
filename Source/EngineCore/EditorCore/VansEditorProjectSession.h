#pragma once

#include <memory>
#include <optional>
#include <string>

namespace Vans
{
	class VansProjectSelector;
}

namespace VansGraphics
{
	struct VansEditorPendingProjectRequest
	{
		bool createNew = false;
		std::string projectPath;
		std::string projectName;
	};

	class VansEditorProjectSession final
	{
	public:
		VansEditorProjectSession();
		~VansEditorProjectSession();

		VansEditorProjectSession(const VansEditorProjectSession&) = delete;
		VansEditorProjectSession& operator=(const VansEditorProjectSession&) = delete;

		void Initialize();
		void Shutdown();

		bool IsLoaded() const { return m_Loaded; }
		void MarkLoaded(bool loaded) { m_Loaded = loaded; }
		Vans::VansProjectSelector* Selector() const { return m_Selector.get(); }

		void QueueOpen(std::string projectPath);
		void QueueCreate(std::string projectPath, std::string projectName);
		bool HasPendingRequest() const { return m_PendingRequest.has_value(); }
		const VansEditorPendingProjectRequest* PendingRequest() const
		{
			return m_PendingRequest ? &*m_PendingRequest : nullptr;
		}
		VansEditorPendingProjectRequest TakePendingRequest();
		void ClearPendingRequest() { m_PendingRequest.reset(); }

	private:
		std::unique_ptr<Vans::VansProjectSelector> m_Selector;
		bool m_Loaded = false;
		std::optional<VansEditorPendingProjectRequest> m_PendingRequest;
	};
}
