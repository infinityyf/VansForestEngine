#include "PcgAssetContractTests.h"
#include <chrono>
#include "../EngineCore/PcgCore/Serialization/VansPlantTypeAssetCodec.h"
#include "../EngineCore/PcgCore/Storage/VansPlantTypeAssetStorage.h"
#include "../EngineCore/PcgCore/Serialization/VansPcgMaskAssetCodec.h"
#include "../EngineCore/PcgCore/Serialization/VansPcgRecipeCodec.h"
#include "../EngineCore/PcgCore/VansPcgExecutor.h"
#include "../EngineCore/PcgCore/VansPcgBatchPlan.h"
#include "../EngineCore/PcgCore/VansPcgResourcePlan.h"
#include "../EngineCore/PcgCore/VansPcgTerrainSurface.h"
#include "../EngineCore/PcgCore/Storage/VansPcgSplineFieldStorage.h"
#include "../EngineCore/TerrainCore/VansTerrainAsset.h"
#include "../EngineCore/PcgCore/Storage/VansPcgMaskAssetStorage.h"
#include "../EngineCore/EditorCore/Pcg/VansPcgMaskAuthoringSession.h"
#include "../EngineCore/AssetCore/Serialization/VansDataPngEncoder.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineCore/AssetCore/Storage/VansJsonFileStorage.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../EngineCore/AssetCore/Storage/VansAssetMetaStorage.h"
#include "../EngineCore/AssetCore/Storage/VansFileStorage.h"
#include "../EngineCore/AssetCore/VansAssetDocument.h"
#include "../EngineCore/AssetCore/VansAssetObjectRepository.h"
#include "../EngineCore/EditorCore/VansAssetDocumentEditService.h"
#include "../EngineCore/EditorCore/VansAssetDocumentTypeRegistry.h"
#include "../EngineCore/SceneCore/VansAssetObjectBootstrapper.h"
#include "../EngineCore/SceneCore/VansPackagedResourcePlan.h"
#include "../EngineCore/SceneCore/Serialization/VansVegetationConfigCodec.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace
{
using namespace Vans;
using Value = VansSerializedValue;
namespace fs = std::filesystem;

bool Check(bool success, const std::string& message)
{
	if (!success) std::cerr << "PCG assets: " << message << '\n';
	return success;
}

VansAssetGuid Guid(const char* name) { return VansAssetGuid::FromStableName("PcgAssetContract", name); }

VansPlantTypeAsset TreeFixture()
{
	VansPlantTypeAsset asset;
	asset.name = "User tree";
	asset.category = VansPlantCategory::Tree;
	VansPlantVariant variant;
	variant.id = "tree-a";
	variant.name = "User model A";
	variant.geometry = VansPlantGeometry::Mesh;
	variant.weight = 3;
	variant.footprintRadius = 2;
	variant.cullingRadius = 10;
	variant.parts = {
		{ "trunk", VansPlantPartKind::Trunk, Guid("mesh-a"), 1, Guid("bark") },
		{ "leaves", VansPlantPartKind::Leaves, Guid("mesh-a"), 0, Guid("leaves") }
	};
	asset.variants.push_back(variant);
	variant.id = "tree-b";
	variant.name = "User model B";
	variant.weight = 1;
	variant.parts = { { "surface", VansPlantPartKind::Surface, Guid("mesh-b"), -1, Guid("bark") } };
	asset.variants.push_back(variant);
	return asset;
}

bool TestPlantSchema()
{
	std::string error;
	VansPlantTypeAsset blank, decoded;
	Value root;
	if (!Check(VansPlantTypeAssetCodec::Encode(blank, root, error) &&
		VansPlantTypeAssetCodec::Decode(root, decoded, error) && decoded.variants.empty() &&
		decoded.Dependencies().empty() && decoded.grass.boneCount == 0 &&
		!ValidatePlantTypeAsset(decoded, true).empty(), "Empty plant must be saveable but cannot generate")) return false;
	const auto tree = TreeFixture();
	if (!Check(ValidatePlantTypeAsset(tree, true).empty() && tree.Dependencies().size() == 4 &&
		VansPlantTypeAssetCodec::Encode(tree, root, error) &&
		VansPlantTypeAssetCodec::Decode(root, decoded, error) &&
		decoded.Dependencies() == tree.Dependencies() && decoded.variants.size() == 2 &&
		decoded.variants[0].parts[0].material == Guid("bark") && decoded.variants[0].parts[1].material == Guid("leaves") &&
		decoded.variants[1].parts[0].material == Guid("bark"),
		"Plant round trip lost distinct model/part/material choices: " + error)) return false;

	// 错误文档不能局部覆盖现有工作副本，也不能静默接收旧字段。
	const auto reject = [&](Value malformed, const char* label) {
		VansPlantTypeAsset unchanged = tree;
		return Check(!VansPlantTypeAssetCodec::Decode(malformed, unchanged, error) && !error.empty() &&
			unchanged.name == tree.name && unchanged.Dependencies() == tree.Dependencies(), label);
	};
	Value malformed = root;
	malformed.objectFields.emplace_back("version", Value::Int(1));
	if (!reject(malformed, "Unknown schema fields must fail")) return false;
	malformed = root;
	malformed.objectFields.emplace_back("name", Value::String("duplicate"));
	if (!reject(malformed, "Duplicate fields must fail")) return false;
	malformed = root;
	malformed.objectFields.erase(malformed.objectFields.begin());
	if (!reject(malformed, "Missing fields must fail")) return false;
	malformed = root;
	auto& part = FindObjectField(FindObjectField(malformed, "variants")->arrayItems[0], "parts")->arrayItems[0];
	SetSerializedObjectField(part, "mesh", MakeSerializedProjectAssetObjectReference(Guid("mesh-a").ToString(), "material"));
	if (!reject(malformed, "Wrong typed model reference must fail")) return false;
	SetSerializedObjectField(part, "mesh", MakeSerializedProjectAssetObjectReference("00000000-0000-0000-0000-000000000000", "model"));
	if (!reject(malformed, "A null GUID must be encoded as an unassigned reference")) return false;
	malformed = root;
	SetSerializedObjectField(FindObjectField(malformed, "variants")->arrayItems[0], "weight", Value::Float(std::numeric_limits<double>::quiet_NaN()));
	if (!reject(malformed, "Nonfinite weights must fail")) return false;

	auto invalid = tree;
	invalid.variants[1].id = invalid.variants[0].id;
	if (!Check(!ValidatePlantTypeAsset(invalid, false).empty(), "Duplicate variant IDs must fail")) return false;
	invalid = tree;
	invalid.variants[0].rotation = { 0, 0, 0, 0 };
	if (!Check(!ValidatePlantTypeAsset(invalid, false).empty(), "Invalid calibration rotation must fail")) return false;
	invalid = tree;
	invalid.variants[0].parts[0].mesh = {};
	if (!Check(ValidatePlantTypeAsset(invalid, false).empty() && !ValidatePlantTypeAsset(invalid, true).empty(),
		"Missing mesh must remain a draft, never select fallback geometry")) return false;
	invalid = tree;
	invalid.variants[0].parts[0].material = {};
	if (!Check(ValidatePlantTypeAsset(invalid, false).empty() && !ValidatePlantTypeAsset(invalid, true).empty(),
		"An unassigned material must remain a draft, never inherit a hidden material")) return false;

	VansPlantTypeAsset grass;
	grass.name = "User procedural grass";
	grass.grass.boneCount = 3;
	grass.grass.bladeHeight = 0.8f;
	grass.grass.subBladeCount = 15;
	VansPlantVariant blade;
	blade.id = "blade";
	blade.geometry = VansPlantGeometry::ProceduralBlade;
	blade.weight = 1;
	blade.bladeWidth = 0.02f;
	blade.parts = { { "surface", VansPlantPartKind::Surface, {}, -1, Guid("grass") } };
	grass.variants = { blade };
	if (!Check(ValidatePlantTypeAsset(grass, true).empty() && VansPlantTypeAssetCodec::Encode(grass, root, error) &&
		VansPlantTypeAssetCodec::Decode(root, decoded, error) && decoded.grass.subBladeCount == 15,
		"Explicit procedural geometry did not round trip: " + error)) return false;
	SetSerializedObjectField(*FindObjectField(root, "grass"), "boneCount", Value::Int(-1));
	if (!reject(root, "Negative counts must fail before conversion to unsigned")) return false;
	grass.grass.subBladeCount = 33;
	if (!Check(!ValidatePlantTypeAsset(grass, false).empty(), "Sub-blade count must respect actual GPU buffer capacity")) return false;
	grass.grass.subBladeCount = 15;
	grass.category = VansPlantCategory::Tree;
	return Check(!ValidatePlantTypeAsset(grass, false).empty(), "Trees cannot use procedural grass geometry");
}

bool TestPlantAuthoringLifecycle()
{
	struct Fixture
	{
		fs::path path = fs::temp_directory_path() / ("ForestPcgAsset-" + VansAssetGuid::New().ToString());
		~Fixture() { std::error_code ec; fs::remove_all(path, ec); }
	} fixture;
	const auto assetsRoot = fixture.path / "Assets";
	const auto plantPath = assetsRoot / "Plants" / "UserPlant.vplant";
	fs::create_directories(plantPath.parent_path());
	std::string error;
	VansAssetMeta meta;
	meta.guid = Guid("plant");
	meta.importer = VansAssetDatabase::ImporterFor(VansAssetType::PlantType);
	auto plant = TreeFixture();
	{
		VansScopedIOContext write(VansIODomain::Authoring, "ContractFixture.PlantCreate", true);
		if (!Check(VansPlantTypeAssetStorage::SaveAtomic(plantPath, plant, error) &&
			VansAssetMetaStorage::SaveAtomic(VansAssetMeta::MetaPathFor(plantPath), meta, error), error)) return false;
	}
	VansAssetDatabase database(assetsRoot, fixture.path / "Artifacts");
	const auto scan = database.Scan(VansAssetOperationPolicy::ReadOnly());
	const auto record = database.Find(meta.guid);
	if (!Check(scan.errors.empty() && scan.registered == 1 && scan.generatedMeta == 0 && record &&
		record->type == VansAssetType::PlantType && VansAssetDatabase::SerializedTypeName(record->type) == "plantType" &&
		VansAssetObjectBootstrapper::Supports(record->type), "Plant asset registration failed")) return false;
	VansAssetObjectRepository repository;
	const auto bootstrap = VansAssetObjectBootstrapper::Publish(database.All(), repository);
	VansAssetObjectHandle<VansPlantTypeAsset> originalHandle;
	const auto original = repository.ResolveLatest<VansPlantTypeAsset>(meta.guid, &originalHandle);
	if (!Check(static_cast<bool>(bootstrap) && original && original->Dependencies() == plant.Dependencies(),
		"Plant bootstrap did not publish a typed immutable object")) return false;
	VansAssetObjectSnapshotInfo info;
	if (!Check(repository.FindInfo(meta.guid, info) && info.dependencies == plant.Dependencies(), "Plant dependency list was lost")) return false;
	const auto* descriptor = VansAssetDocumentTypeRegistry::Get().Find(record->type);
	Value root;
	if (!Check(descriptor && VansPlantTypeAssetCodec::Encode(plant, root, error) &&
		descriptor->validateBeforeSave(plantPath, root).empty() && descriptor->collectDependencies(plantPath, root).size() == 4,
		"Plant document registration failed")) return false;
	VansAssetDocument document;
	if (!Check(document.Load(plantPath, error), error)) return false;
	struct HistoryGuard { VansAssetDocument& doc; ~HistoryGuard() { VansAssetDocumentEditService::ClearHistory(doc); } } history{ document };
	const auto fingerprint = VansAssetDocument::Fingerprint(plantPath, error);
	plant.name = "Edited plant";
	plant.variants[0].parts[1].material = Guid("edited-leaves");
	if (!Check(VansPlantTypeAssetCodec::Encode(plant, root, error), error)) return false;
	VansIOAudit::Reset();
	const auto edit = VansAssetDocumentEditService::ReplaceRoot(document, root);
	if (!Check(edit.success && document.IsDirty() &&
		VansAssetObjectBootstrapper::PublishSerialized(*record, document.SerializedRootSnapshot(), info.contentHash ^ 1u, repository, error),
		"Plant working-copy publication failed: " + error)) return false;
	const auto events = VansIOAudit::Snapshot();
	const auto updated = repository.ResolveLatest<VansPlantTypeAsset>(meta.guid);
	if (!Check(events.empty() && updated && updated->name == "Edited plant" && original->name == "User tree" &&
		original->variants[0].parts[1].material == Guid("leaves") &&
		VansAssetDocument::Fingerprint(plantPath, error) == fingerprint,
		"Memory edits must preserve disk bytes and previously acquired snapshots")) return false;
	if (!Check(VansAssetDocumentEditService::Undo(document).success && !document.IsDirty() &&
		VansAssetDocumentEditService::Redo(document).success && document.IsDirty(), "Plant authoring undo/redo failed")) return false;
	Value malformed = root;
	malformed.objectFields.emplace_back("instanceCount", Value::Int(100));
	if (!Check(!VansAssetObjectBootstrapper::PublishSerialized(*record, malformed, info.contentHash ^ 2u, repository, error) &&
		repository.ResolveLatest<VansPlantTypeAsset>(meta.guid) == updated, "Invalid edit replaced the published plant")) return false;
	{
		VansScopedIOContext write(VansIODomain::Authoring, "ContractFixture.PlantExplicitSave", true);
		if (!Check(VansPlantTypeAssetStorage::SaveAtomic(plantPath, plant, error), error)) return false;
	}
	VansPlantTypeAsset reopened;
	if (!Check(VansPlantTypeAssetStorage::Load(plantPath, reopened, error) && reopened.name == "Edited plant" &&
		reopened.variants[0].parts[1].material == Guid("edited-leaves"), "Explicit save did not persist the current plant")) return false;
	// 运行使用已发布对象，删除作者文件也不能触发隐式读取或失效。
	fs::remove(plantPath);
	VansIOAudit::Reset();
	return Check(repository.ResolveLatest<VansPlantTypeAsset>(meta.guid) == updated && VansIOAudit::Snapshot().empty(),
		"Memory plant lookup touched the authoring filesystem");
}

VansPcgMaskAsset MaskFixture(const char* maskName, const char* layerName, const char* pixelName)
{
	VansPcgMaskAsset asset;
	asset.name = maskName;
	asset.pixelAsset = Guid(pixelName);
	std::string error;
	VansPcgMask::CreateBlank({ "user-region", layerName, Guid(maskName).ToString() },
		{ { -8, -4 }, { 8, 4 } }, 32, 16, asset.mask, error);
	asset.brush.radius = 2;
	asset.brush.strength = 0.7f;
	asset.brush.hardness = 0.5f;
	return asset;
}

bool TestMaskImageAndSchema()
{
	auto mask = MaskFixture("mask-a", "grass-a", "pixels-a");
	std::string error, bytes;
	for (std::size_t i = 0; i < mask.mask.pixels.size(); ++i) mask.mask.pixels[i] = static_cast<std::uint16_t>(i * 127);
	Value root;
	VansPcgMaskAsset decoded;
	if (!Check(VansPcgMaskAssetCodec::EncodeDefinition(mask, root, error) &&
		VansPcgMaskAssetCodec::DecodeDefinition(root, decoded, error) && decoded.mask.pixels.empty() &&
		VansPcgMaskAssetCodec::EncodePixels(mask, bytes, error) && VansPcgMaskAssetCodec::DecodePixels(bytes, decoded, error) &&
		decoded.mask.ContentHash() == mask.mask.ContentHash() && decoded.brush.strength == mask.brush.strength,
		"Mask definition/pixels round trip changed precision or identity: " + error)) return false;
	const auto unchangedHash = decoded.mask.ContentHash();
	if (!Check(!VansPcgMaskAssetCodec::DecodePixels(bytes.substr(0, 20), decoded, error) && decoded.mask.ContentHash() == unchangedHash,
		"Truncated image partially overwrote the current Mask")) return false;
	std::vector<std::uint8_t> rgba(mask.mask.pixels.size() * 4);
	for (std::size_t i = 0; i < rgba.size() / 4; ++i)
	{
		rgba[i * 4] = 128;
		rgba[i * 4 + 1] = 64;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = 0;
	}
	if (!Check(VansDataPngEncoder::EncodeRGBA8(mask.mask.width, mask.mask.height, rgba, bytes, error) &&
		!VansPcgMaskAssetCodec::DecodePixels(bytes, decoded, error) && decoded.mask.ContentHash() == unchangedHash &&
		VansPcgMaskAssetCodec::ImportPixels(bytes, 0, decoded, error) && decoded.mask.pixels[0] == 128 * 257 &&
		VansPcgMaskAssetCodec::ImportPixels(bytes, 1, decoded, error) && decoded.mask.pixels[0] == 64 * 257,
		"Explicit image import must choose raw linear channel without color conversion")) return false;
	const auto importedHash = decoded.mask.ContentHash();
	if (!Check(!VansPcgMaskAssetCodec::ImportPixels(bytes, 4, decoded, error) && decoded.mask.ContentHash() == importedHash,
		"Missing image channel silently used another channel")) return false;
	SetSerializedObjectField(root, "width", Value::Int(-1));
	if (!Check(!VansPcgMaskAssetCodec::DecodeDefinition(root, decoded, error) && decoded.mask.ContentHash() == importedHash,
		"Invalid dimensions partially overwrote the Mask")) return false;
	return true;
}

bool TestIndependentMaskSessions()
{
	struct Fixture
	{
		fs::path path = fs::temp_directory_path() / ("ForestPcgMask-" + VansAssetGuid::New().ToString());
		~Fixture()
		{
			VansAssetDocumentRegistry::Get().Clear();
			std::error_code ec;
			fs::remove_all(path, ec);
		}
	} fixture;
	const auto assetsRoot = fixture.path / "Assets";
	fs::create_directories(assetsRoot);
	auto maskA = MaskFixture("mask-a", "grass-a", "pixels-a");
	auto maskB = MaskFixture("mask-b", "grass-b", "pixels-b");
	maskB.brush.radius = 3;
	maskB.brush.strength = 0.2f;
	std::string error;
	const auto writeFixture = [&](const VansPcgMaskAsset& mask, const fs::path& path, const fs::path& pixelPath) {
		Value root;
		std::string pixels;
		VansAssetMeta meta, pixelMeta;
		VansAssetGuid::TryParse(mask.mask.target.maskId, meta.guid);
		meta.importer = VansAssetDatabase::ImporterFor(VansAssetType::PcgMask);
		pixelMeta.guid = mask.pixelAsset;
		pixelMeta.importer = VansAssetDatabase::ImporterFor(VansAssetType::Texture);
		VansScopedIOContext io(VansIODomain::Authoring, "ContractFixture.PcgMaskCreate", true);
		return VansPcgMaskAssetCodec::EncodeDefinition(mask, root, error) &&
			VansPcgMaskAssetCodec::EncodePixels(mask, pixels, error) &&
			VansJsonFileStorage::WriteAtomic(path, EncodeSerializedValueJson<nlohmann::ordered_json>(root), error) &&
			VansFileStorage::WriteAtomicBytes(pixelPath, pixels, error) &&
			VansAssetMetaStorage::SaveAtomic(VansAssetMeta::MetaPathFor(path), meta, error) &&
			VansAssetMetaStorage::SaveAtomic(VansAssetMeta::MetaPathFor(pixelPath), pixelMeta, error);
	};
	const auto pathA = assetsRoot / "A.vpcgmask", pathB = assetsRoot / "B.vpcgmask";
	const auto pixelsA = assetsRoot / "A.png", pixelsB = assetsRoot / "B.png";
	if (!Check(writeFixture(maskA, pathA, pixelsA) && writeFixture(maskB, pathB, pixelsB), error)) return false;
	VansAssetDatabase database(assetsRoot, fixture.path / "Artifacts");
	const auto scan = database.Scan(VansAssetOperationPolicy::ReadOnly());
	const auto recordA = database.Find(Guid("mask-a")), recordB = database.Find(Guid("mask-b"));
	if (!Check(scan.errors.empty() && recordA && recordB && recordA->type == VansAssetType::PcgMask,
		"Mask type registration failed")) return false;
	VansAssetObjectRepository repository;
	const auto bootstrap = VansAssetObjectBootstrapper::Publish(database.All(), repository);
	if (!Check(static_cast<bool>(bootstrap), bootstrap.errors.empty() ? "Mask bootstrap failed" : bootstrap.errors.front())) return false;
	const auto originalA = repository.ResolveLatest<VansPcgMaskAsset>(recordA->guid);
	const auto originalB = repository.ResolveLatest<VansPcgMaskAsset>(recordB->guid);
	auto sessionA = VansPcgMaskAuthoringSession::Open(*recordA, originalA, pixelsA, repository, error);
	auto sessionB = VansPcgMaskAuthoringSession::Open(*recordB, originalB, pixelsB, repository, error);
	if (!Check(sessionA && sessionB && !sessionA->IsDirty() && !sessionB->IsDirty() &&
		VansPcgMaskAuthoringSession::Open(*recordA, originalA, pixelsA, repository, error) == sessionA,
		"Reopening a Mask lost its existing editing session: " + error)) return false;
	const auto fingerprintA = VansAssetDocument::Fingerprint(pixelsA, error);
	const auto fingerprintB = VansAssetDocument::Fingerprint(pixelsB, error);
	const auto sourceFingerprint = VansAssetDocument::Fingerprint(pathA, error);
	VansIOAudit::Reset();
	const auto& targetA = maskA.mask.target;
	const auto& targetB = maskB.mask.target;
	if (!Check(!sessionA->BeginStroke(targetB, error) && sessionA->BeginStroke(targetA, error) &&
		sessionA->AddPoint(targetA, -2, 0, false, error) && !sessionA->AddPoint(targetB, 2, 0, false, error),
		"Scene brush did not lock the complete target")) return false;
	std::vector<VansStagedFile> files;
	if (!Check(!sessionA->StageSave(files, error) && !VansAssetDocumentEditService::CanUndo(sessionA->Document()->sourceDocument) &&
		sessionA->AddPoint(targetA, 2, 0, false, error) && sessionA->EndStroke(error),
		"An unfinished stroke was saved or entered undo history")) return false;
	const auto paintedHash = sessionA->WorkingAsset().mask.ContentHash();
	const auto painted = repository.ResolveLatest<VansPcgMaskAsset>(recordA->guid);
	if (!Check(repository.ResolveLatest<VansAssetMeta>(recordA->guid)!=nullptr,
		"Brush publication invalidated the metadata required by later configuration previews")) return false;
	if (!Check(VansIOAudit::Snapshot().empty() && paintedHash != originalA->mask.ContentHash() &&
		painted && painted->mask.ContentHash() == paintedHash &&
		sessionB->WorkingAsset().mask.ContentHash() == originalB->mask.ContentHash() &&
		sessionB->WorkingAsset().brush.radius == 3 && !sessionB->IsDirty() &&
		!sessionA->TakePendingPixelChange().Empty() && sessionA->TakePendingPixelChange().Empty(),
		"Painting touched disk, another grass, or failed to publish its dirty pixels")) return false;
	if (!Check(VansAssetDocument::Fingerprint(pixelsA, error) == fingerprintA &&
		VansAssetDocument::Fingerprint(pixelsB, error) == fingerprintB &&
		VansAssetDocument::Fingerprint(pathA, error) == sourceFingerprint,
		"Mask brush wrote authoring files before Save")) return false;
	if (!Check(VansAssetDocumentEditService::Undo(sessionA->Document()->sourceDocument).success &&
		!sessionA->Document()->IsDirty() && sessionA->WorkingAsset().mask.ContentHash() == originalA->mask.ContentHash() &&
		VansAssetDocumentEditService::Redo(sessionA->Document()->sourceDocument).success &&
		sessionA->WorkingAsset().mask.ContentHash() == paintedHash,
		"One stroke did not undo/redo exact pixels and dirty state")) return false;
	if (!Check(sessionA->BeginStroke(targetA, error) && sessionA->AddPoint(targetA, 0, 0, true, error) &&
		sessionA->CancelStroke(error) && sessionA->WorkingAsset().mask.ContentHash() == paintedHash,
		"Cancel did not restore the published painted Mask")) return false;
	Value definition;
	VansAssetObjectSnapshotInfo publishedInfo;
	if (!Check(VansPcgMaskAssetCodec::EncodeDefinition(sessionA->WorkingAsset(), definition, error) &&
		VansAssetObjectBootstrapper::PublishSerialized(*recordA, definition, 91, repository, error) &&
		repository.FindInfo(recordA->guid, publishedInfo) && publishedInfo.contentHash == 91 &&
		repository.ResolveLatest<VansPcgMaskAsset>(recordA->guid)->mask.ContentHash() == paintedHash,
		"Definition publication lost unsaved brush pixels or broke the working-copy hash contract")) return false;
	SetSerializedObjectField(*FindObjectField(definition, "owner"), "layer", Value::String("grass-b"));
	if (!Check(!VansAssetObjectBootstrapper::PublishSerialized(*recordA, definition, 92, repository, error),
		"A generic property edit reassigned a writable Mask to another grass")) return false;

	VansAssetDocumentSaveStage sourceStage;
	if (!Check(sessionA->Document()->sourceDocument.StageSave(sourceStage, error) && sessionA->StageSave(files, error) && files.size() == 1,
		"Mask and source did not stage together: " + error)) return false;
	VansStagedFileTransaction transaction;
	transaction.Add({ sourceStage.targetPath, sourceStage.temporaryPath });
	for (auto& file : files) transaction.Add(std::move(file));
	if (!Check(transaction.Publish(error) && sessionA->Document()->sourceDocument.ObservePublishedSave(sourceStage, error) &&
		sessionA->ObservePublishedSave(error), "Mask save transaction failed: " + error)) return false;
	sessionA->Document()->sourceDocument.AdoptObservedSave(sourceStage);
	sessionA->AdoptObservedSave();
	if (!Check(sessionB->BeginStroke(targetB, error) && sessionB->AddPoint(targetB, 3, 1, false, error) && sessionB->EndStroke(error), error)) return false;
	const auto unsavedB = repository.ResolveLatest<VansPcgMaskAsset>(recordB->guid);
	const auto refreshA = VansAssetObjectBootstrapper::Publish({ *recordA }, repository, database.All());
	if (!Check(static_cast<bool>(refreshA) && repository.ResolveLatest<VansPcgMaskAsset>(recordB->guid) == unsavedB && sessionB->IsDirty(),
		"Saving one Mask reloaded and discarded another Mask's unsaved pixels")) return false;
	VansPcgMaskAsset reopened;
	if (!Check(!sessionA->Document()->IsDirty() &&
		VansPcgMaskAssetStorage::Load(pathA, [&](VansAssetGuid guid) -> std::optional<fs::path> {
			return guid == maskA.pixelAsset ? std::optional<fs::path>{ pixelsA } : std::nullopt;
		}, reopened, error) && reopened.mask.ContentHash() == paintedHash &&
		VansAssetDocument::Fingerprint(pixelsB, error) == fingerprintB,
		"Saved Mask did not reopen with exact pixels or modified the other grass")) return false;
	if (!Check(VansAssetDocumentEditService::Undo(sessionA->Document()->sourceDocument).success && sessionA->IsDirty(),
		"Undo after Save must mark the pixel companion dirty again")) return false;
	// 磁盘冲突不能被保存覆盖，也不能推进 SavedState。
	{
		VansScopedIOContext write(VansIODomain::Authoring, "ContractFixture.PcgExternalEdit", true);
		if (!VansFileStorage::WriteAtomicBytes(pixelsA, "external-change", error)) return false;
	}
	if (!Check(!sessionA->StageSave(files, error) && sessionA->Document()->IsDirty(),
		"External Mask edits were overwritten by Save")) return false;
	// 像素 GUID 也必须独占，不能只隔离 JSON 文档却保存到同一图片。
	maskB.pixelAsset = maskA.pixelAsset;
	if (!VansPcgMaskAssetCodec::EncodeDefinition(maskB, definition, error)) return false;
	{
		VansScopedIOContext write(VansIODomain::Authoring, "ContractFixture.PcgSharedPixels", true);
		std::string bytes;
		if (!VansPcgMaskAssetCodec::EncodePixels(maskA, bytes, error) || !VansFileStorage::WriteAtomicBytes(pixelsA, bytes, error) ||
			!VansJsonFileStorage::WriteAtomic(pathB, EncodeSerializedValueJson<nlohmann::ordered_json>(definition), error)) return false;
	}
	database.Scan(VansAssetOperationPolicy::ReadOnly());
	const auto invalidRefresh = VansAssetObjectBootstrapper::Publish({ *database.Find(recordB->guid) }, repository, database.All());
	if (!Check(!static_cast<bool>(invalidRefresh) && repository.ResolveLatest<VansPcgMaskAsset>(recordB->guid) == unsavedB,
		"Targeted refresh reassigned another Mask's pixel texture or replaced the last valid snapshot")) return false;
	VansAssetObjectRepository invalidRepository;
	const auto invalid = VansAssetObjectBootstrapper::Publish(database.All(), invalidRepository);
	return Check(!static_cast<bool>(invalid) && std::any_of(invalid.errors.begin(), invalid.errors.end(), [](const std::string& text) {
		return text.find("cannot be shared") != std::string::npos;
	}), "Different editable Masks were allowed to share a pixel texture");
}

bool TestCountGeneration()
{
	auto asset = MaskFixture("count-mask", "count-layer", "count-pixels");
	std::fill(asset.mask.pixels.begin(), asset.mask.pixels.end(), 65535);
	VansPcgDistributionSettings settings;
	settings.regionId = asset.mask.target.regionId;
	settings.layerId = asset.mask.target.layerId;
	settings.bounds = asset.mask.bounds;
	settings.positionJitter = 1;
	settings.variants = { { "one", 1, 0 } };
	const VansPcgGenerationBudget budget{ 10000, 500 };
	const VansPcgSurfaceSampler surface = [](float, float, VansPcgSurfacePoint& point) { point.height = 17; return true; };
	const auto generate = [&](std::uint32_t count, const VansPcgBounds& bounds) {
		return VansPcgPointGenerator::GenerateCount(settings, asset.mask, nullptr, count, bounds, surface, budget);
	};
	const auto whole = generate(100, settings.bounds);
	if (!Check(whole && whole.points.size() == 100 && whole.stats.generatedCount == 100 && !whole.stats.candidateBudgetExhausted,
		"Explicit target count was not reached")) return false;
	const auto less = generate(40, settings.bounds);
	std::unordered_set<std::uint64_t> ids;
	for (const auto& point : whole.points) ids.insert(point.id);
	for (const auto& point : less.points)
		if (!Check(ids.count(point.id) == 1 && point.position[1] == 17, "Count change reshuffled existing candidate identity or grounding")) return false;
	const auto left = generate(100, { { -8, -4 }, { 0, 4 } });
	const auto right = generate(100, { { 0, -4 }, { 8, 4 } });
	std::unordered_set<std::uint64_t> chunks;
	for (const auto* result : { &left, &right }) for (const auto& point : result->points) chunks.insert(point.id);
	if (!Check(left && right && chunks == ids && left.points.size() + right.points.size() == 100,
		"Count output chunks did not partition the single regional target")) return false;
	std::fill(asset.mask.pixels.begin(), asset.mask.pixels.end(), 16384);
	const auto grey = generate(100, settings.bounds);
	if (!Check(grey && grey.points.size() == 100 && grey.stats.candidates > whole.stats.candidates,
		"Count mode must explicitly refill within the candidate budget")) return false;
	std::fill(asset.mask.pixels.begin(), asset.mask.pixels.end(), 0);
	const auto black = generate(100, settings.bounds);
	if (!Check(black && black.points.empty() && black.stats.requestedCount == 100 && black.stats.candidateBudgetExhausted &&
		black.stats.candidates == budget.maxCandidates, "Count shortfall did not remain bounded and visible")) return false;
	std::fill(asset.mask.pixels.begin(), asset.mask.pixels.end(), 65535);
	settings.minimumSpacing = 1;
	const auto spaced = generate(40, settings.bounds);
	if (!Check(spaced && spaced.points.size() == 40, "Count spacing unexpectedly exhausted a feasible fixture")) return false;
	for (std::size_t a = 0; a < spaced.points.size(); ++a) for (std::size_t b = a + 1; b < spaced.points.size(); ++b)
	{
		const auto dx = spaced.points[a].position[0] - spaced.points[b].position[0];
		const auto dz = spaced.points[a].position[2] - spaced.points[b].position[2];
		if (!Check(dx * dx + dz * dz >= 1, "Count generation violated minimum spacing")) return false;
	}
	return Check(!generate(501, settings.bounds), "Target count crossed the hard instance budget");
}

bool TestTerrainSurface()
{
	auto terrain = std::make_shared<VansTerrainAsset>();
	terrain->width = 2; terrain->height = 2;
	terrain->heights = { 0,65535,0,65535 };
	terrain->settings.terrainSize = 4;
	terrain->settings.maxHeight = 8;
	terrain->settings.heightOffset = -2;
	std::string error;
	const auto surface = CreatePcgTerrainSurface(terrain,error);
	VansPcgSurfacePoint point;
	if (!Check(surface && surface(0,0,point) && std::abs(point.height-2) < 0.0001f &&
		std::abs(point.normal[0]+4/std::sqrt(17.0f)) < 0.0001f,
		"PCG terrain sampling does not match the linear height texture")) return false;
	if (!Check(surface(-2,0,point) && point.height == -2 && point.normal == std::array<float,3>{0,1,0} &&
		!surface(-2.01f,0,point), "PCG terrain bounds or clamp sampling is incorrect")) return false;
	VansPcgSurfaceHit hit;
	if (!Check(RaycastPcgTerrainSurface(terrain,{0,20,0},{0,-2,0},100,hit) &&
		std::abs(hit.position[1]-2)<0.0001f && hit.normal[0]<0,
		"Viewport ray did not intersect the selected terrain or return its normal")) return false;
	if (!Check(!RaycastPcgTerrainSurface(terrain,{3,20,0},{0,-1,0},100,hit) &&
		!RaycastPcgTerrainSurface(terrain,{0,20,0},{0,0,0},100,hit) &&
		!RaycastPcgTerrainSurface(terrain,{0,20,0},{0,-1,0},1,hit),
		"Viewport ray ignored bounds, invalid direction or range")) return false;
	terrain->heights={0,65535,65535,0};
	if (!Check(RaycastPcgTerrainSurface(terrain,{-2,0,-2},{1,0,1},10,hit) &&
		std::abs(hit.position[0]+std::sqrt(0.5f))<0.001f,
		"Viewport ray skipped the nearest of two bilinear surface crossings")) return false;
	terrain->heights={32768,32768,32768,32768};
	if (!Check(RaycastPcgTerrainSurface(terrain,{0,20,0},{0,-1,0},100,hit) &&
		std::abs(hit.position[1]-(32768*8.0f/65535-2))<0.0001f,
		"Viewport ray missed a flat heightfield")) return false;
	return Check(!CreatePcgTerrainSurface({},error) && !error.empty(), "Missing bound terrain selected an implicit fallback");
}

bool TestRecipeAndExecution()
{
	std::string error;
	Value root;
	VansPcgRecipeAsset blank, decoded;
	if (!Check(VansPcgRecipeCodec::Encode(blank, root, error) && VansPcgRecipeCodec::Decode(root, decoded, error) &&
		decoded.regions.empty() && decoded.Dependencies().empty() &&
		VansPcgExecutor::Generate(decoded, VansAssetObjectRepository{}, {}).layers.empty(), "Empty recipe created default plants or regions")) return false;
	VansAssetObjectRepository repository;
	std::uint64_t generation = 1;
	auto plant = TreeFixture();
	plant.category = VansPlantCategory::Grass;
	plant.grass.boneCount = 3;
	plant.grass.bladeHeight = 1;
	for (auto& variant : plant.variants) variant.footprintRadius = 0;
	const auto plantGuid = Guid("recipe-plant");
	if (!Check(repository.Publish<VansPlantTypeAsset>(plantGuid, VansAssetType::PlantType, generation++,
		std::make_shared<const VansPlantTypeAsset>(plant), plant.Dependencies(), error).IsValid(), error)) return false;
	auto maskA = MaskFixture("recipe-mask-a", "grass-a", "recipe-pixels-a");
	auto maskB = MaskFixture("recipe-mask-b", "grass-b", "recipe-pixels-b");
	std::fill(maskA.mask.pixels.begin(), maskA.mask.pixels.end(), 65535);
	std::fill(maskB.mask.pixels.begin(), maskB.mask.pixels.end(), 65535);
	const auto publishMask = [&](const VansPcgMaskAsset& mask) {
		VansAssetGuid guid;
		VansAssetGuid::TryParse(mask.mask.target.maskId, guid);
		return repository.Publish<VansPcgMaskAsset>(guid, VansAssetType::PcgMask, generation++,
			std::make_shared<const VansPcgMaskAsset>(mask), mask.Dependencies(), error).IsValid();
	};
	if (!Check(publishMask(maskA) && publishMask(maskB), error)) return false;
	VansPcgRecipeAsset recipe;
	recipe.name = "User recipe";
	VansPcgRegion region;
	region.id = maskA.mask.target.regionId;
	region.enabled = true;
	region.bounds = maskA.mask.bounds;
	region.cellSize = 4;
	region.surface.kind = VansPcgSurfaceKind::Plane;
	region.surface.planeHeight = 7;
	VansPcgLayer layer;
	layer.id = "grass-a";
	layer.enabled = true;
	layer.plant = plantGuid;
	layer.densityMask = Guid("recipe-mask-a");
	layer.placement.density = 2;
	layer.placement.positionJitter = 1;
	layer.budget = { 20000, 1000 };
	region.layers = { layer };
	recipe.regions = { region };
	VansIOAudit::Reset();
	const auto first = VansPcgExecutor::Generate(recipe, repository, {});
	if (!Check(first && first.layers.size() == 1 && first.layers[0].points.size() == 256 && VansIOAudit::Snapshot().empty(),
		"Memory recipe execution failed or read authoring files: " + first.error)) return false;
	for (const auto& point : first.layers[0].points)
		if (!Check(point.position[1] == 7, "Explicit plane was not used for grounding")) return false;
	layer.id = "grass-b";
	layer.densityMask = Guid("recipe-mask-b");
	recipe.regions[0].layers.push_back(layer);
	const auto both = VansPcgExecutor::Generate(recipe, repository, {});
	const auto findResource = [&](VansAssetGuid guid) -> std::optional<VansAssetRecord> {
		VansAssetRecord record;
		record.guid = guid;
		if (guid == plantGuid) record.type = VansAssetType::PlantType;
		else if (guid == Guid("recipe-mask-a") || guid == Guid("recipe-mask-b")) record.type = VansAssetType::PcgMask;
		else if (guid == maskA.pixelAsset || guid == maskB.pixelAsset) record.type = VansAssetType::Texture;
		else if (guid == Guid("mesh-a") || guid == Guid("mesh-b")) record.type = VansAssetType::Model;
		else if (guid == Guid("bark") || guid == Guid("leaves")) record.type = VansAssetType::Material;
		else return std::nullopt;
		return record;
	};
	VansIOAudit::Reset();
	const auto resourcePlan = BuildPcgResourcePlan(recipe, repository, findResource);
	if (!Check(resourcePlan && resourcePlan.resources.size() == 9 &&
		std::count_if(resourcePlan.resources.begin(),resourcePlan.resources.end(),[](const auto& item) { return item.maskPixels; }) == 2 &&
		std::count_if(resourcePlan.resources.begin(),resourcePlan.resources.end(),[](const auto& item) { return item.meshCpuData; }) == 2 &&
		VansIOAudit::Snapshot().empty(), "Typed PCG resource closure lost mask pixels or grass model CPU data")) return false;
	const auto missingResource = BuildPcgResourcePlan(recipe,repository,[&](VansAssetGuid guid) {
		return guid == Guid("mesh-b") ? std::optional<VansAssetRecord>{} : findResource(guid);
	});
	if (!Check(!missingResource && missingResource.resources.empty(), "Missing PCG dependencies published a partial plan")) return false;
	if (!Check(both && both.layers.size() == 2 && both.layers[1].points.size() == 256 &&
		both.layers[0].points[0].id == first.layers[0].points[0].id, "Adding a grass layer changed another layer's stable result")) return false;
	std::fill(maskA.mask.pixels.begin(), maskA.mask.pixels.end(), 0);
	if (!publishMask(maskA)) return false;
	const auto erased = VansPcgExecutor::Generate(recipe, repository, {});
	if (!Check(erased && erased.layers[0].points.empty() && erased.layers[1].points.size() == both.layers[1].points.size() &&
		erased.layers[1].points[0].id == both.layers[1].points[0].id, "Erasing one grass affected the other grass's distribution")) return false;
	if (!Check(VansPcgRecipeCodec::Encode(recipe, root, error) && VansPcgRecipeCodec::Decode(root, decoded, error) &&
		decoded.Dependencies() == recipe.Dependencies() && decoded.regions[0].layers.size() == 2,
		"Recipe round trip lost user configuration: " + error)) return false;
	const auto grassJson=EncodeSerializedValueJson<nlohmann::ordered_json>(root);
	if (!Check(!grassJson["regions"][0]["layers"][0].contains("targetCount"),"Grass serialized a removed target-count field")) return false;
	auto obsoleteCount=grassJson;
	obsoleteCount["regions"][0]["layers"][0]["targetCount"]=256;
	if (!Check(!VansPcgRecipeCodec::Decode(DecodeSerializedValueJson(obsoleteCount),decoded,error),
		"Grass accepted the removed count field")) return false;
	auto countRecipe=recipe;countRecipe.regions[0].layers[0].source=VansPcgSourceMode::Count;
	if (!Check(!ValidatePcgRecipe(countRecipe,false).empty(),"Grass still accepts Count distribution")) return false;
	root.objectFields.emplace_back("instanceCount", Value::Int(100));
	if (!Check(!VansPcgRecipeCodec::Decode(root, decoded, error) && decoded.regions.size() == 1,
		"Recipe accepted obsolete fields or replaced the last valid configuration")) return false;
	auto shared = recipe;
	shared.regions[0].layers[1].densityMask = shared.regions[0].layers[0].densityMask;
	if (!Check(!ValidatePcgRecipe(shared, false).empty(), "Different layers shared a writable Mask reference")) return false;

	// 固定草保留原有语义；本次 Mask 过滤仅针对固定树木。
	recipe.regions[0].layers.resize(1);
	auto& fixed = recipe.regions[0].layers[0];
	fixed.source = VansPcgSourceMode::Fixed;
	fixed.targetCount = 0;
	fixed.fixedInstances = { { "author-tree-01", "tree-a", { 19, 3, -5 }, { 0, 0, 0, 1 }, { 2, 3, 4 } } };
	recipe.regions[0].surface.kind = VansPcgSurfaceKind::Unassigned;
	const auto fixedResult = VansPcgExecutor::Generate(recipe, repository, {});
	if (!Check(fixedResult && fixedResult.layers[0].points.size() == 1 && fixedResult.layers[0].points[0].position == fixed.fixedInstances[0].position &&
		fixedResult.layers[0].points[0].scale == fixed.fixedInstances[0].scale,
		"Fixed source silently switched to procedural generation or changed authored transforms")) return false;
	const auto fixedId = VansPcgPointGenerator::PointIdText(fixedResult.layers[0].points[0].id);
	fixed.overrides = { { fixedId, VansPcgOverrideKind::Transform, { 8, 9, 10 } }, { "ffffffffffffffff", VansPcgOverrideKind::Remove } };
	const auto edited = VansPcgExecutor::Generate(recipe, repository, {});
	if (!Check(edited && edited.layers[0].points[0].position == std::array<float, 3>{ 8, 9, 10 } &&
		edited.layers[0].orphanOverrides.size() == 1, "Transform overrides or orphan diagnostics failed")) return false;
	fixed.overrides[0].kind = VansPcgOverrideKind::Remove;
	const auto removed = VansPcgExecutor::Generate(recipe, repository, {});
	if (!Check(removed && removed.layers[0].points.empty(), "Stable instance removal was not reapplied")) return false;
	fixed.overrides.clear();
	fixed.fixedInstances.clear();
	VansPcgInstanceOverride locked;locked.target=fixedId;locked.kind=VansPcgOverrideKind::Lock;locked.variant="tree-a";
	locked.position={12,3,5};fixed.overrides={locked};
	const auto retained=VansPcgExecutor::Generate(recipe,repository,{});
	if (!Check(retained && retained.layers[0].points.size()==1 && retained.layers[0].points[0].position==locked.position &&
		VansPcgRecipeCodec::Encode(recipe,root,error) && VansPcgRecipeCodec::Decode(root,decoded,error) &&
		decoded.regions[0].layers[0].overrides[0].variant=="tree-a",
		"Locked instance did not retain its model/transform when its source point disappeared")) return false;
	fixed.overrides.clear();
	const auto noFallback = VansPcgExecutor::Generate(recipe, repository, {});
	return Check(noFallback && noFallback.layers[0].points.empty(), "Empty fixed source generated fallback trees or target-count instances");
}

bool TestFixedTreeMaskFiltering()
{
	VansAssetObjectRepository repository;
	std::string error;
	std::uint64_t generation = 1;
	const auto plant = TreeFixture();
	const auto plantGuid = Guid("fixed-tree-plant");
	if (!Check(repository.Publish<VansPlantTypeAsset>(plantGuid, VansAssetType::PlantType, generation++,
		std::make_shared<const VansPlantTypeAsset>(plant), plant.Dependencies(), error).IsValid(), error)) return false;
	auto mask = MaskFixture("fixed-tree-mask", "trees", "fixed-tree-pixels");
	auto otherMask = MaskFixture("other-tree-mask", "other-trees", "other-tree-pixels");
	auto exclusion = MaskFixture("fixed-tree-exclusion", "trees", "fixed-tree-exclusion-pixels");
	std::fill(mask.mask.pixels.begin(), mask.mask.pixels.end(), 65535);
	std::fill(otherMask.mask.pixels.begin(), otherMask.mask.pixels.end(), 65535);
	const auto publish = [&](const VansPcgMaskAsset& asset) {
		VansAssetGuid guid;
		VansAssetGuid::TryParse(asset.mask.target.maskId, guid);
		return repository.Publish<VansPcgMaskAsset>(guid, VansAssetType::PcgMask, generation++,
			std::make_shared<const VansPcgMaskAsset>(asset), asset.Dependencies(), error).IsValid();
	};
	if (!Check(publish(mask) && publish(otherMask) && publish(exclusion), error)) return false;
	VansPcgLayer layer;
	layer.id = "trees"; layer.enabled = true; layer.category = VansPlantCategory::Tree;
	layer.plant = plantGuid; layer.densityMask = Guid("fixed-tree-mask");
	layer.source = VansPcgSourceMode::Fixed; layer.targetCount = 999; layer.placement.density = 100;
	layer.budget = { 10000, 100 };
	layer.fixedInstances = {
		{ "left", "tree-a", { -4, 3, 0 }, { 0, 0, 0, 1 }, { 2, 3, 4 } },
		{ "right", "tree-b", { 4, 7, 0 }, { 0, 1, 0, 0 }, { 1, 2, 3 } }
	};
	VansPcgRegion region;
	region.id = mask.mask.target.regionId; region.enabled = true; region.bounds = mask.mask.bounds; region.cellSize = 4;
	region.layers = { layer };
	layer.id = "other-trees"; layer.densityMask = Guid("other-tree-mask");
	region.layers.push_back(layer);
	VansPcgRecipeAsset recipe; recipe.regions = { region };
	auto& fixed = recipe.regions[0].layers[0];
	const auto run = [&]() { return VansPcgExecutor::Generate(recipe, repository, {}); };
	const auto same = [](const VansPcgLayerResult& a, const VansPcgLayerResult& b) {
		if (a.variantIds != b.variantIds || a.points.size() != b.points.size()) return false;
		for (std::size_t i = 0; i < a.points.size(); ++i) {
			const auto& x = a.points[i]; const auto& y = b.points[i];
			if (x.id != y.id || x.variantIndex != y.variantIndex || x.position != y.position ||
				x.rotation != y.rotation || x.scale != y.scale) return false;
		}
		return true;
	};
	const auto white = run();
	if (!Check(white && white.layers.size() == 2 && white.layers[0].points.size() == 2,
		"White fixed tree Mask did not preserve exactly the authored trees: " + white.error)) return false;
	VansPcgMaskStroke stroke;
	VansPcgMaskEdit edit;
	VansPcgPixelRect changed;
	auto brush = mask.brush; brush.operation = VansPcgBrushOperation::Erase;
	brush.radius = 2; brush.hardness = 1; brush.strength = 1;
	if (!Check(stroke.Begin(mask.mask, brush, error) && stroke.AddPoint(mask.mask, -4, 0, false, changed, error) &&
		stroke.Finish(mask.mask, edit, error) && publish(mask), error)) return false;
	const auto erased = run();
	if (!Check(erased && erased.layers[0].points.size() == 1 && erased.layers[0].points[0].position[0] == 4 &&
		erased.layers[0].stats.maskRejected == 1 && same(erased.layers[1], white.layers[1]),
		"Root brush did not hide the fixed tree independently of the other layer")) return false;
	if (!Check(edit.Apply(mask.mask, false, error) && publish(mask), error)) return false;
	const auto undone = run();
	if (!Check(undone && same(undone.layers[0], white.layers[0]), "Undo did not restore exact fixed identities/models/transforms")) return false;
	if (!Check(edit.Apply(mask.mask, true, error) && publish(mask), error)) return false;
	const auto redone = run();
	if (!Check(redone && same(redone.layers[0], erased.layers[0]), "Redo changed the filtered fixed tree set")) return false;
	// 空白区域涂白不会创建候选点；全黑更新必须覆盖并移除所有旧渲染批次。
	std::fill(mask.mask.pixels.begin(), mask.mask.pixels.end(), 0);
	brush.operation = VansPcgBrushOperation::Add;
	if (!Check(stroke.Begin(mask.mask, brush, error) && stroke.AddPoint(mask.mask, 0, 0, false, changed, error) &&
		stroke.Finish(mask.mask, edit, error) && publish(mask), error)) return false;
	const auto emptyArea = run();
	if (!Check(emptyArea && emptyArea.layers[0].points.empty(), "Painting unoccupied space generated new fixed trees")) return false;
	std::fill(mask.mask.pixels.begin(), mask.mask.pixels.end(), 0);
	if (!publish(mask)) return false;
	const auto black = run();
	VansPcgBatchUpdate originalBatches, clearedBatches;
	if (!Check(black && black.layers[0].points.empty() && black.layers[0].requiresWholeRegionUpdate &&
		BuildPcgBatchUpdate(region, white.layers[0], std::nullopt, originalBatches, error) &&
		BuildPcgBatchUpdate(region, black.layers[0], std::nullopt, clearedBatches, error) && clearedBatches.batches.empty(), error)) return false;
	for (const auto& batch : originalBatches.batches)
		if (!Check(clearedBatches.Contains(batch.first), "Empty Mask update left an old tree batch outside coverage")) return false;
	std::fill(mask.mask.pixels.begin(), mask.mask.pixels.end(), 65535);
	if (!publish(mask)) return false;
	const auto restored = run();
	if (!Check(restored && same(restored.layers[0], white.layers[0]) && fixed.fixedInstances.size() == 2,
		"Painting white lost original trees or mutated authored data")) return false;
	// 灰度固定子集不因密度、种子或重建而移动/补种；阈值、倍率和排除层复用同一采样规则。
	std::fill(mask.mask.pixels.begin(), mask.mask.pixels.end(), 32768);
	if (!publish(mask)) return false;
	const auto gray = run();
	fixed.seed = 999; fixed.placement.density = 0; fixed.targetCount = 0;
	const auto grayAgain = run();
	if (!Check(gray && grayAgain && same(gray.layers[0], grayAgain.layers[0]), "Gray fixed tree set depends on density/seed")) return false;
	fixed.placement.maskThreshold = .75f;
	const auto threshold = run();
	if (!Check(threshold && threshold.layers[0].points.empty(), "Fixed tree Mask threshold was ignored")) return false;
	fixed.placement.maskMultiplier = 2;
	const auto multiplied = run();
	if (!Check(multiplied && same(multiplied.layers[0], white.layers[0]), "Fixed tree Mask multiplier was ignored")) return false;
	fixed.placement.maskThreshold = 0; fixed.placement.maskMultiplier = 1; fixed.placement.invertMask = true;
	std::fill(mask.mask.pixels.begin(), mask.mask.pixels.end(), 0);
	if (!publish(mask)) return false;
	fixed.fixedInstances.push_back({ "outside", "tree-a", { 19, 3, 0 } });
	const auto inverted = run();
	if (!Check(inverted && same(inverted.layers[0], white.layers[0]), "Inversion resurrected out-of-bounds trees")) return false;
	fixed.fixedInstances.pop_back();
	fixed.exclusionMask = Guid("fixed-tree-exclusion");
	std::fill(exclusion.mask.pixels.begin(), exclusion.mask.pixels.end(), 65535);
	if (!publish(exclusion)) return false;
	const auto excluded = run();
	if (!Check(excluded && excluded.layers[0].points.empty(), "Fixed tree exclusion Mask was ignored")) return false;
	fixed.exclusionMask = {}; fixed.placement.invertMask = false;
	const auto leftId = VansPcgPointGenerator::PointIdText(VansPcgPointGenerator::AuthoredInstanceId(region.id, fixed.id, "left"));
	// 显式变换后按最终根部位置过滤；Mask 隐藏不应误报为丢失原实例。
	fixed.overrides = { { leftId, VansPcgOverrideKind::Transform, { 0, 3, 0 } } };
	if (!Check(stroke.Begin(mask.mask, brush, error) && stroke.AddPoint(mask.mask, 0, 0, false, changed, error) &&
		stroke.Finish(mask.mask, edit, error) && publish(mask), error)) return false;
	const auto moved = run();
	if (!Check(moved && moved.layers[0].points.size() == 1 && moved.layers[0].points[0].position[0] == 0 &&
		moved.layers[0].orphanOverrides.empty(), "Fixed tree Mask did not follow final transformed root")) return false;
	std::fill(mask.mask.pixels.begin(), mask.mask.pixels.end(), 0);
	if (!publish(mask)) return false;
	fixed.overrides[0].kind = VansPcgOverrideKind::Lock; fixed.overrides[0].variant = "tree-a";
	fixed.addedInstances = { { "manual", "tree-b", { 2, 5, 0 } } };
	const auto explicitEdits = run();
	return Check(explicitEdits && explicitEdits.layers[0].points.size() == 2 && explicitEdits.layers[0].orphanOverrides.empty(),
		"Fixed Mask filtering changed explicit Lock or manually added instance semantics");
}
}

bool RunPcgAssetContractTests()
{
	const bool passed = TestPlantSchema() && TestPlantAuthoringLifecycle() && TestMaskImageAndSchema() && TestIndependentMaskSessions() &&
		TestCountGeneration() && TestTerrainSurface() && TestRecipeAndExecution() && TestFixedTreeMaskFiltering();
	if (passed) std::cout << "[PcgAssets] User plants, independent masks, brush undo and explicit pixel saves passed\n";
	return passed;
}

// 验证真实项目的当前 schema、资源闭包、实例稳定性与只读加载；不含旧格式转换。
bool RunPcgProjectContractTests(const char* projectPath)
{
	const fs::path project = fs::u8path(projectPath);
	VansAssetDatabase database(project / "Assets", project / "Library/Artifacts");
	std::vector<VansAssetRecord> records;
	const fs::path packagePlanPath = project / VansPackagedResourcePlanIO::DefaultRelativePath();
	if (fs::is_regular_file(packagePlanPath))
	{
		VansPackagedResourcePlan package;
		std::string error;
		if (!Check(VansPackagedResourcePlanIO::Load(packagePlanPath, project, package, error), error)) return false;
		for (const auto& indexed : package.assetIndex)
		{
			VansAssetRecord record;
			if (!Check(VansAssetGuid::TryParse(indexed.guid, record.guid), "Invalid packaged GUID")) return false;
			record.type = VansAssetDatabase::ParseSerializedType(indexed.type);
			record.state = indexed.missing ? VansAssetState::Missing : VansAssetState::Discovered;
			record.artifactPath = indexed.artifactPath;
			record.metaPath = indexed.metaPath;
			record.sourceHash = indexed.sourceHash;
			record.metaHash = indexed.metaHash;
			record.artifactFormat = indexed.artifactFormat == "source" ? VansAssetArtifactFormat::Source :
				indexed.artifactFormat == "imported" ? VansAssetArtifactFormat::Imported : VansAssetArtifactFormat::None;
			if (record.artifactFormat == VansAssetArtifactFormat::Source) record.sourcePath = record.artifactPath;
			// 包验收只使用发布索引，禁止扫描作者文件掩盖丢失的 Mask 像素依赖。
			const bool pcgDefinition = record.type == VansAssetType::VegetationConfig ||
				record.type == VansAssetType::PlantType || record.type == VansAssetType::PcgMask ||
				record.type == VansAssetType::Terrain || record.type == VansAssetType::PcgSpline;
			if (pcgDefinition && !Check(indexed.sourcePath.empty() && indexed.authoringPath.empty(),
				"Packaged PCG authoring path leaked")) return false;
			records.push_back(std::move(record));
		}
	}
	else
	{
		if (!Check(bool(database.Scan(VansAssetOperationPolicy::ReadOnly())), "Project scan failed")) return false;
		records = database.All();
	}
	std::vector<VansAssetRecord> inputs;
	for (const auto& record : records)
		if (record.type == VansAssetType::VegetationConfig || record.type == VansAssetType::PlantType ||
			record.type == VansAssetType::PcgMask || record.type == VansAssetType::Terrain || record.type == VansAssetType::PcgSpline)
			inputs.push_back(record);
	VansAssetObjectRepository repository;
	const auto published = VansAssetObjectBootstrapper::Publish(inputs, repository, records);
	for (const auto& error : published.errors) std::cerr << error << '\n';
	if (!published || inputs.empty()) return false;
	std::size_t recipes = 0;
    std::shared_ptr<const VansPcgSplineFieldSnapshot> projectSplineField;
	for (const auto& record:inputs) if (record.type==VansAssetType::PcgSpline)
	{
		const auto splines=repository.ResolveLatest<VansPcgSplineAsset>(record.guid);
		const auto terrain=splines?repository.ResolveLatest<VansTerrainAsset>(splines->terrain):nullptr;
		std::string error;
		if (!Check(bool(splines)&&bool(terrain),"Project spline asset and terrain dependency must be published")) return false;
        const auto buildStart=std::chrono::steady_clock::now();
		const auto generated=VansPcgSplineFieldBuilder::Build(*splines,terrain,{},error);
		if (!Check(bool(generated),"Project spline field generation: "+error)) return false;
        std::cout<<"[PcgProject] field build ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-buildStart).count()<<'\n';
        auto edit=*splines;
        for(auto& spline:edit.splines) spline.excludeVegetation=true;
        projectSplineField=VansPcgSplineFieldBuilder::Build(edit,terrain,generated,error);
        if (!Check(bool(projectSplineField),"Project spline exclusion: "+error)) return false;
		std::cout<<"[PcgProject] spline fields="<<generated->tiles.size()<<" road meshes="<<generated->roads.size()<<'\n';
		if (fs::is_regular_file(packagePlanPath))
		{
			const auto baked=VansPcgSplineFieldStorage::Load(VansPcgSplineFieldStorage::CachePath(project,record.guid),*splines,terrain,error);
			if (!Check(baked && baked->effectiveTerrain->heights==generated->effectiveTerrain->heights,"Packaged spline bake mismatch: "+error)) return false;
		}
	}
	for (const auto& record : inputs)
	{
		if (record.type != VansAssetType::VegetationConfig) continue;
		++recipes;
		const auto asset = repository.ResolveLatest<VansVegetationConfigAsset>(record.guid);
		if (!Check(bool(asset), "Project recipe is not published")) return false;
		const auto plan = BuildPcgResourcePlan(asset->config, repository,
			[&](VansAssetGuid guid) -> std::optional<VansAssetRecord> {
				const auto found = std::find_if(records.begin(), records.end(),
					[&](const auto& value) { return value.guid == guid; });
				return found == records.end() ? std::nullopt : std::optional<VansAssetRecord>(*found);
			});
		if (!Check(bool(plan), "Project PCG dependency closure: " + plan.error)) return false;
		const auto surface = [&](const VansPcgSurfaceBinding& binding, std::string& error) {
			return CreatePcgTerrainSurface(repository.ResolveLatest<VansTerrainAsset>(binding.terrain), error);
		};
		const auto result = VansPcgExecutor::Generate(asset->config, repository, surface);
		if (!Check(bool(result), "Project generation: " + result.error)) return false;
        if (projectSplineField)
        {
            const auto effectiveSurface=[&](const VansPcgSurfaceBinding& binding,std::string& error) {
                return CreatePcgTerrainSurface(binding.terrain==projectSplineField->terrainGuid?projectSplineField->effectiveTerrain:
                    repository.ResolveLatest<VansTerrainAsset>(binding.terrain),error);
            };
            const auto composed=VansPcgExecutor::Generate(asset->config,repository,effectiveSurface,std::nullopt,projectSplineField);
            if (!Check(bool(composed),"Project vegetation exclusion failed: "+composed.error)) return false;
            std::size_t removed=0;
            for(std::size_t index=0;index<composed.layers.size();++index)
            {
                const auto& baseline=result.layers[index];const auto& filtered=composed.layers[index];
                if (!Check(filtered.points.size()<=baseline.points.size(),"Exclusion added vegetation")) return false;
                removed+=baseline.points.size()-filtered.points.size();
                for(const auto& point:filtered.points)
                    if (!Check(projectSplineField->SampleVegetationExclusion(point.position[0],point.position[2])<1,
                        "Vegetation remains in a fully excluded spline interior")) return false;
                const auto region=std::find_if(asset->config.regions.begin(),asset->config.regions.end(),[&](const auto& r){return r.id==filtered.regionId;});
                const auto layer=std::find_if(region->layers.begin(),region->layers.end(),[&](const auto& l){return l.id==filtered.layerId;});
                if(layer->source!=VansPcgSourceMode::Density || !layer->overrides.empty() || !layer->addedInstances.empty() || layer->placement.rootOffset!=0) continue;
                VansPcgRecipeAsset selected;selected.name=asset->config.name;selected.regions={*region};selected.regions.front().layers={*layer};
                const VansPcgBounds area{{32,64},{128,128}};
                const auto started=std::chrono::steady_clock::now();
                const auto local=VansPcgExecutor::Generate(selected,repository,effectiveSurface,area,projectSplineField);
                if(!Check(bool(local) && local.layers.size()==1,"Local spline vegetation update failed")) return false;
                std::size_t cursor=0;
                for(const auto& point:filtered.points) if(area.Contains(point.position[0],point.position[2]))
                {
                    if(!Check(cursor<local.layers.front().points.size() && local.layers.front().points[cursor].id==point.id &&
                        local.layers.front().points[cursor].position==point.position,"Local vegetation differs from full field regeneration")) return false;
                    ++cursor;
                }
                if(!Check(cursor==local.layers.front().points.size(),"Local vegetation produced extra instances")) return false;
                std::cout<<"[PcgProject] local "<<filtered.layerId<<" candidates="<<local.layers.front().stats.candidates<<
                    " fullCandidates="<<filtered.stats.candidates<<" localMs="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<'\n';
            }
            if(!Check(removed>0,"DemoHall splines did not exclude any vegetation")) return false;
            std::cout<<"[PcgProject] spline vegetation excluded="<<removed<<'\n';
        }
		const auto repeated = VansPcgExecutor::Generate(asset->config, repository, surface);
		if (!Check(bool(repeated) && repeated.layers.size() == result.layers.size(), "Project repeat failed")) return false;
		for (std::size_t l = 0; l < result.layers.size(); ++l)
		{
			const auto& output = result.layers[l];
			const auto& repeat = repeated.layers[l];
			if (!Check(output.points.size() == repeat.points.size(), "Project count is unstable")) return false;
			for (std::size_t p = 0; p < output.points.size(); ++p)
			{
				const auto& a = output.points[p]; const auto& b = repeat.points[p];
				if (!Check(a.id == b.id && a.variantIndex == b.variantIndex && a.position == b.position &&
					a.rotation == b.rotation && a.scale == b.scale, "Project instance transform is unstable")) return false;
			}
			const auto region = std::find_if(asset->config.regions.begin(), asset->config.regions.end(),
				[&](const auto& value) { return value.id == output.regionId; });
			const auto layer = std::find_if(region->layers.begin(), region->layers.end(),
				[&](const auto& value) { return value.id == output.layerId; });
			if (layer->source == VansPcgSourceMode::Fixed && layer->overrides.empty() && layer->addedInstances.empty())
			{
				const auto density = repository.ResolveLatest<VansPcgMaskAsset>(layer->densityMask);
				const auto exclusion = repository.ResolveLatest<VansPcgMaskAsset>(layer->exclusionMask);
				std::size_t expected = 0;
				for (const auto& original : layer->fixedInstances)
				{
					const auto id = VansPcgPointGenerator::AuthoredInstanceId(region->id, layer->id, original.id);
					if (layer->category == VansPlantCategory::Tree && !VansPcgPointGenerator::PassesMask(layer->placement,
						density->mask, exclusion ? &exclusion->mask : nullptr, original.position[0], original.position[2], id)) continue;
					++expected;
					const auto point = std::lower_bound(output.points.begin(), output.points.end(), id,
						[](const auto& value, auto target) { return value.id < target; });
					if (!Check(point != output.points.end() && point->id == id && point->position == original.position &&
						point->rotation == original.rotation && point->scale == original.scale &&
						output.variantIds[point->variantIndex] == original.variant, "Fixed transform or model changed")) return false;
				}
				if (!Check(output.points.size() == expected, "Fixed tree output does not match its Mask-filtered authored set")) return false;
			}
			if (layer->source == VansPcgSourceMode::Count &&
				!Check(output.points.size() == layer->targetCount, "Project target count could not be filled")) return false;
			std::cout << "[PcgProject] " << project.filename().u8string() << "/" << output.layerId <<
				" points=" << output.points.size() << " variants=" << output.variantIds.size() <<
				" candidates=" << output.stats.candidates << " resources=" << plan.resources.size() << '\n';
		}
	}
	return Check(recipes > 0, "No project PCG recipes were indexed");
}
