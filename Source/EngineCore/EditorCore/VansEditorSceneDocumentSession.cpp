#include "VansEditorSceneDocumentSession.h"

#include "../SceneCore/VansSceneDocument.h"
#include "VansSceneEditService.h"

#include <utility>

VansGraphics::VansEditorSceneDocumentState::VansEditorSceneDocumentState() = default;
VansGraphics::VansEditorSceneDocumentState::~VansEditorSceneDocumentState() = default;

VansGraphics::VansEditorSceneDocumentState::VansEditorSceneDocumentState(
	VansEditorSceneDocumentState&& other) noexcept
	: m_Document(std::move(other.m_Document)),
	  m_EditService(std::move(other.m_EditService))
{
}

VansGraphics::VansEditorSceneDocumentState&
VansGraphics::VansEditorSceneDocumentState::operator=(
	VansEditorSceneDocumentState&& other) noexcept
{
	if (this == &other)
		return *this;

	// EditService 持有 Document 引用，因此必须按此顺序释放并安装成对状态。
	m_EditService.reset();
	m_Document.reset();
	m_Document = std::move(other.m_Document);
	m_EditService = std::move(other.m_EditService);
	return *this;
}

VansGraphics::VansEditorSceneDocumentState::VansEditorSceneDocumentState(
	std::unique_ptr<Vans::VansSceneDocument> document,
	std::unique_ptr<Vans::VansSceneEditService> editService)
	: m_Document(std::move(document)),
	  m_EditService(std::move(editService))
{
}

VansGraphics::VansEditorSceneDocumentSession::VansEditorSceneDocumentSession() = default;
VansGraphics::VansEditorSceneDocumentSession::~VansEditorSceneDocumentSession() = default;

VansGraphics::VansEditorSceneDocumentState
VansGraphics::VansEditorSceneDocumentSession::ReplaceDocument(
	std::unique_ptr<Vans::VansSceneDocument> document,
	PreviewRefresh previewRefresh)
{
	if (!document)
		return {};

	// 先构造新 pair，再摘除当前 pair；若分配失败，当前会话仍保持完整。
	auto editService = std::make_unique<Vans::VansSceneEditService>(*document);
	editService->SetPrefabPreviewRefresh(std::move(previewRefresh));

	VansEditorSceneDocumentState previous = Take();
	m_Document = std::move(document);
	m_EditService = std::move(editService);
	return previous;
}

void VansGraphics::VansEditorSceneDocumentSession::Restore(
	VansEditorSceneDocumentState state)
{
	Reset();
	m_Document = std::move(state.m_Document);
	m_EditService = std::move(state.m_EditService);
}

void VansGraphics::VansEditorSceneDocumentSession::Reset()
{
	// Service 持有 Document 引用，必须先于 Document 销毁。
	m_EditService.reset();
	m_Document.reset();
}

VansGraphics::VansEditorSceneDocumentState
VansGraphics::VansEditorSceneDocumentSession::Take()
{
	return VansEditorSceneDocumentState{
		std::move(m_Document),
		std::move(m_EditService) };
}
