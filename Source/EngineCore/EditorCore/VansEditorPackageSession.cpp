#include "VansEditorPackageSession.h"

#include <utility>

VansGraphics::VansEditorPackageSession::VansEditorPackageSession(
	BuildOperation buildOperation)
	: m_BuildOperation(std::move(buildOperation))
{
	if (!m_BuildOperation)
	{
		m_BuildOperation = [](const Vans::VansGamePackageRequest& request)
		{
			return Vans::VansGamePackageBuilder::Build(request);
		};
	}
}

const VansGraphics::VansEditorPackageStatus&
VansGraphics::VansEditorPackageSession::Execute(
	const VansEditorPackageContext& context)
{
	m_Status = {};
	if (context.request.projectRootPath.empty() || context.request.scenePath.empty())
	{
		m_Status.outcome = VansEditorPackageOutcome::MissingInput;
		m_Status.message = "Open a project and load a scene before packaging";
		return m_Status;
	}
	if (context.sceneDirty)
	{
		m_Status.outcome = VansEditorPackageOutcome::DirtyScene;
		m_Status.message = "Save the current scene before packaging";
		return m_Status;
	}
	if (context.assetsDirty)
	{
		m_Status.outcome = VansEditorPackageOutcome::DirtyAssets;
		m_Status.message = "Save dirty assets before packaging";
		return m_Status;
	}
	if (context.projectDocumentsDirty)
	{
		m_Status.outcome = VansEditorPackageOutcome::DirtyProjectDocuments;
		m_Status.message = "Save dirty project documents before packaging";
		return m_Status;
	}

	const Vans::VansGamePackageResult result = m_BuildOperation(context.request);
	m_Status.outcome = result.success
		? VansEditorPackageOutcome::Success
		: VansEditorPackageOutcome::BuildFailed;
	m_Status.message = result.message;
	m_Status.outputPath = result.outputPath;
	return m_Status;
}
