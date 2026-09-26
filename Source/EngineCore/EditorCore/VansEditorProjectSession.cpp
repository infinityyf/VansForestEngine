#include "VansEditorProjectSession.h"

#include "Windows/VansProjectSelector.h"

#include <utility>

VansGraphics::VansEditorProjectSession::VansEditorProjectSession() = default;
VansGraphics::VansEditorProjectSession::~VansEditorProjectSession() = default;

void VansGraphics::VansEditorProjectSession::Initialize()
{
	m_Selector = std::make_unique<Vans::VansProjectSelector>();
	m_Loaded = false;
	m_PendingRequest.reset();
}

void VansGraphics::VansEditorProjectSession::Shutdown()
{
	m_PendingRequest.reset();
	m_Loaded = false;
	m_Selector.reset();
}

void VansGraphics::VansEditorProjectSession::QueueOpen(std::string projectPath)
{
	VansEditorPendingProjectRequest request;
	request.projectPath = std::move(projectPath);
	m_PendingRequest = std::move(request);
}

void VansGraphics::VansEditorProjectSession::QueueCreate(
	std::string projectPath,
	std::string projectName)
{
	VansEditorPendingProjectRequest request;
	request.createNew = true;
	request.projectPath = std::move(projectPath);
	request.projectName = std::move(projectName);
	m_PendingRequest = std::move(request);
}

VansGraphics::VansEditorPendingProjectRequest
VansGraphics::VansEditorProjectSession::TakePendingRequest()
{
	if (!m_PendingRequest)
		return {};

	VansEditorPendingProjectRequest request = std::move(*m_PendingRequest);
	m_PendingRequest.reset();
	return request;
}
