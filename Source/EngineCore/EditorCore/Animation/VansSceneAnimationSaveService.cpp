#include "VansSceneAnimationSaveService.h"
#include "../VansAssetDocumentRegistry.h"
#include "../VansAssetDocumentTypeRegistry.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/Storage/VansStagedFileTransaction.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../SceneCore/VansSceneDocument.h"
#include <unordered_set>

namespace Vans
{
bool VansSceneAnimationSaveService::Save(VansSceneDocument& scene,
	const std::vector<std::shared_ptr<VansOpenAssetDocument>>& assets, std::string& error)
{
	VansScopedIOContext io(VansIODomain::Authoring,"SceneAnimation.Save",true);
	error.clear();
	std::unordered_set<const VansOpenAssetDocument*> seen;
	std::vector<std::shared_ptr<VansOpenAssetDocument>> documents;
	for (const auto& document : assets)
	{
		if (!document || !document->sourceDocument.IsLoaded())
		{ error = "Animation authoring document is unavailable"; return false; }
		if (!seen.insert(document.get()).second) continue;
		for (const auto& diagnostic : VansAssetDocumentTypeRegistry::Get().ValidateBeforeSave(
			VansAssetDatabase::Classify(document->sourcePath), document->sourcePath,
			document->sourceDocument.SerializedRootSnapshot()))
			if (diagnostic.severity == VansAssetDocumentDiagnosticSeverity::Error)
			{ error = diagnostic.propertyPath + ": " + diagnostic.message; return false; }
		documents.push_back(document);
	}
	// 所有文档先验证并暂存，发布失败时由同一事务恢复原文件。
	VansStagedFileTransaction transaction;
	SceneDocumentSaveStage sceneStage;
	if (!scene.StageSave(sceneStage, error)) return false;
	if (!sceneStage.targetPath.empty()) transaction.Add({sceneStage.targetPath, sceneStage.temporaryPath});
	std::vector<VansAssetDocumentSaveStage> stages(documents.size());
	for (std::size_t i = 0; i < documents.size(); ++i)
	{
		if (!documents[i]->sourceDocument.StageSave(stages[i], error)) return false;
		if (!stages[i].targetPath.empty()) transaction.Add({stages[i].targetPath, stages[i].temporaryPath});
	}
	if (!transaction.Empty() && !transaction.Publish(error)) return false;
	if (!scene.AdoptStagedSave(sceneStage, error)) return false;
	for (std::size_t i = 0; i < documents.size(); ++i)
		if (!documents[i]->sourceDocument.AdoptStagedSave(stages[i], error)) return false;
	return true;
}
}
