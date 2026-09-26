#pragma once

#include "../PackagingCore/VansGamePackageBuilder.h"

#include <functional>
#include <string>

namespace VansGraphics
{
	enum class VansEditorPackageOutcome
	{
		None,
		Success,
		MissingInput,
		DirtyScene,
		DirtyAssets,
		DirtyProjectDocuments,
		BuildFailed
	};

	struct VansEditorPackageContext
	{
		Vans::VansGamePackageRequest request;
		bool sceneDirty = false;
		bool assetsDirty = false;
		bool projectDocumentsDirty = false;
	};

	struct VansEditorPackageStatus
	{
		VansEditorPackageOutcome outcome = VansEditorPackageOutcome::None;
		std::string message;
		std::string outputPath;

		bool HasAttempt() const { return outcome != VansEditorPackageOutcome::None; }
		bool Succeeded() const { return outcome == VansEditorPackageOutcome::Success; }
	};

	class VansEditorPackageSession final
	{
	public:
		using BuildOperation = std::function<Vans::VansGamePackageResult(
			const Vans::VansGamePackageRequest&)>;

		explicit VansEditorPackageSession(BuildOperation buildOperation = {});

		const VansEditorPackageStatus& Execute(const VansEditorPackageContext& context);
		const VansEditorPackageStatus& Status() const { return m_Status; }

	private:
		BuildOperation m_BuildOperation;
		VansEditorPackageStatus m_Status;
	};
}
