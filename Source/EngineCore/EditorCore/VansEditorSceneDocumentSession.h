#pragma once

#include <functional>
#include <memory>

namespace Vans
{
	class VansSceneDocument;
	class VansSceneEditService;
}

namespace VansGraphics
{
	class VansEditorSceneDocumentSession;

	class VansEditorSceneDocumentState final
	{
	public:
		VansEditorSceneDocumentState();
		~VansEditorSceneDocumentState();

		VansEditorSceneDocumentState(VansEditorSceneDocumentState&& other) noexcept;
		VansEditorSceneDocumentState& operator=(VansEditorSceneDocumentState&& other) noexcept;

		VansEditorSceneDocumentState(const VansEditorSceneDocumentState&) = delete;
		VansEditorSceneDocumentState& operator=(const VansEditorSceneDocumentState&) = delete;

		Vans::VansSceneDocument* Document() const { return m_Document.get(); }
		Vans::VansSceneEditService* EditService() const { return m_EditService.get(); }
		bool Empty() const { return !m_Document && !m_EditService; }

	private:
		friend class VansEditorSceneDocumentSession;

		VansEditorSceneDocumentState(
			std::unique_ptr<Vans::VansSceneDocument> document,
			std::unique_ptr<Vans::VansSceneEditService> editService);

		std::unique_ptr<Vans::VansSceneDocument> m_Document;
		std::unique_ptr<Vans::VansSceneEditService> m_EditService;
	};

	class VansEditorSceneDocumentSession final
	{
	public:
		using PreviewRefresh = std::function<bool()>;

		VansEditorSceneDocumentSession();
		~VansEditorSceneDocumentSession();

		VansEditorSceneDocumentSession(const VansEditorSceneDocumentSession&) = delete;
		VansEditorSceneDocumentSession& operator=(const VansEditorSceneDocumentSession&) = delete;

		Vans::VansSceneDocument* Document() const { return m_Document.get(); }
		Vans::VansSceneEditService* EditService() const { return m_EditService.get(); }
		bool Empty() const { return !m_Document && !m_EditService; }

		VansEditorSceneDocumentState ReplaceDocument(
			std::unique_ptr<Vans::VansSceneDocument> document,
			PreviewRefresh previewRefresh);
		void Restore(VansEditorSceneDocumentState state);
		void Reset();

	private:
		VansEditorSceneDocumentState Take();

		std::unique_ptr<Vans::VansSceneDocument> m_Document;
		std::unique_ptr<Vans::VansSceneEditService> m_EditService;
	};
}
