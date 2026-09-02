#include "VansAnimationRigSaveService.h"

#include "../../AssetCore/Storage/VansStagedFileTransaction.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetDocument.h"
#include "../VansAssetDocumentRegistry.h"
#include "../VansAssetDocumentTypeRegistry.h"

#include <utility>

namespace Vans
{
	VansAnimationRigSaveResult VansAnimationRigSaveService::Save(
		const std::shared_ptr<VansOpenAssetDocument>& rigDocument)
	{
		VansAnimationRigSaveResult result;
		if (!rigDocument || !rigDocument->sourceDocument.IsLoaded())
		{
			result.message = "Animation Rig document is unavailable";
			return result;
		}

		const VansAssetType type = VansAssetDatabase::Classify(rigDocument->sourcePath);
		for (const VansAssetDocumentDiagnostic& diagnostic :
			VansAssetDocumentTypeRegistry::Get().ValidateBeforeSave(
				type, rigDocument->sourcePath,
				rigDocument->sourceDocument.SerializedRootSnapshot()))
		{
			if (diagnostic.severity != VansAssetDocumentDiagnosticSeverity::Error)
				continue;
			result.message = diagnostic.propertyPath.empty()
				? diagnostic.message
				: diagnostic.propertyPath + ": " + diagnostic.message;
			return result;
		}

		VansAssetDocumentSaveStage stage;
		std::string error;
		if (!rigDocument->sourceDocument.StageSave(stage, error))
		{
			result.message = std::move(error);
			return result;
		}
		VansStagedFileTransaction transaction;
		if (!stage.targetPath.empty())
			transaction.Add({ stage.targetPath, stage.temporaryPath });
		if (!transaction.Empty() && !transaction.Publish(error))
		{
			result.message = std::move(error);
			return result;
		}
		if (!rigDocument->sourceDocument.AdoptStagedSave(stage, error))
		{
			result.message = std::move(error);
			return result;
		}
		result.success = true;
		result.published = !stage.targetPath.empty();
		result.message = result.published
			? "Animation Rig published" : "Animation Rig has no changes";
		return result;
	}
}
