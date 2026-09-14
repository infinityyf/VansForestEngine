#include "VansPcgMaskAuthoringSession.h"
#include "../VansAssetDocumentEditService.h"
#include "../../PcgCore/Serialization/VansPcgMaskAssetCodec.h"
#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../SceneCore/VansAssetObjectBootstrapper.h"

namespace Vans
{
std::shared_ptr<VansPcgMaskAuthoringSession> VansPcgMaskAuthoringSession::Open(const VansAssetRecord& record,
	std::shared_ptr<const VansPcgMaskAsset> asset, const std::filesystem::path& pixelPath,
	VansAssetObjectRepository& repository, std::string& error)
{
	error.clear();
	if (record.type != VansAssetType::PcgMask || !asset || !ValidatePcgMaskAsset(*asset, true).empty() ||
		asset->mask.target.maskId != record.guid.ToString() || pixelPath.empty())
	{ error = "PCG brush requires a complete owned Mask asset and its indexed pixel path"; return {}; }
	auto document = VansAssetDocumentRegistry::Get().GetOrOpen(record.sourcePath);
	if (!document || !document->sourceDocument.IsLoaded())
	{ error = document ? document->lastError : "Cannot open PCG Mask document"; return {}; }
	if (auto existing = document->companion.lock())
	{
		auto session = std::dynamic_pointer_cast<VansPcgMaskAuthoringSession>(existing);
		if (!session || session->m_Record.guid != record.guid || session->m_PixelPath != pixelPath || session->m_Repository != &repository)
		{ error = "PCG Mask document already belongs to a different editing target"; return {}; }
		return session;
	}
	auto session = std::shared_ptr<VansPcgMaskAuthoringSession>(new VansPcgMaskAuthoringSession());
	session->m_Record = record;
	session->m_Repository = &repository;
	session->m_Working = *asset;
	session->m_Document = std::move(document);
	session->m_PixelPath = pixelPath;
	session->m_LoadedFingerprint = VansAssetDocument::Fingerprint(pixelPath, error);
	if (!error.empty()) return {};
	if (!session->m_LoadedFingerprint.exists && !record.memoryOnly)
	{ error = "PCG Mask pixel source is missing"; return {}; }
	session->m_SavedPixels = asset->mask.pixels;
	session->m_SavedWidth=asset->mask.width; session->m_SavedHeight=asset->mask.height;
	if (!session->SyncDefinitionFromDocument(error)) return {};
	session->RefreshPixelDirty();
	session->m_Document->companion = session;
	return session;
}

bool VansPcgMaskAuthoringSession::SyncDefinitionFromDocument(std::string& error)
{
	VansPcgMaskAsset definition;
	if (!VansPcgMaskAssetCodec::DecodeDefinition(m_Document->sourceDocument.SerializedRootSnapshot(), definition, error)) return false;
	if (!(definition.mask.target == m_Working.mask.target) || definition.pixelAsset != m_Working.pixelAsset ||
		definition.mask.width != m_Working.mask.width || definition.mask.height != m_Working.mask.height ||
		!(definition.mask.bounds == m_Working.mask.bounds))
	{ error = "PCG Mask ownership or mapping changed outside its authoring transaction"; return false; }
	m_Working.name = std::move(definition.name);
	m_Working.brush = definition.brush;
	return true;
}

bool VansPcgMaskAuthoringSession::SetBrush(const VansPcgBrushSettings& brush, std::string& error)
{
	error.clear();
	if (StrokeActive()) { error = "Finish the current PCG stroke before changing brush settings"; return false; }
	if (!SyncDefinitionFromDocument(error)) return false;
	VansPcgMaskAsset definition;
	definition.name = m_Working.name;
	definition.pixelAsset = m_Working.pixelAsset;
	definition.mask.target = m_Working.mask.target;
	definition.mask.bounds = m_Working.mask.bounds;
	definition.mask.width = m_Working.mask.width;
	definition.mask.height = m_Working.mask.height;
	definition.brush = brush;
	VansSerializedValue root;
	if (!VansPcgMaskAssetCodec::EncodeDefinition(definition, root, error)) return false;
	const auto result = VansAssetDocumentEditService::ReplaceRoot(m_Document->sourceDocument, std::move(root));
	if (!result.success) { error = result.message; return false; }
	return SyncDefinitionFromDocument(error) && PublishWorkingSnapshot(error);
}

bool VansPcgMaskAuthoringSession::BeginStroke(const VansPcgMaskTarget& target, std::string& error)
{
	error.clear();
	if (!(target == m_Working.mask.target)) { error = "PCG stroke target does not own this Mask"; return false; }
	if (StrokeActive()) { error = "A PCG Mask stroke is already active"; return false; }
	if (!SyncDefinitionFromDocument(error) || !m_Stroke.Begin(m_Working.mask, m_Working.brush, error)) return false;
	m_StrokeDirty = {};
	return true;
}

bool VansPcgMaskAuthoringSession::AddPoint(const VansPcgMaskTarget& target, float worldX, float worldZ, bool erase, std::string& error)
{
	error.clear();
	if (!(target == m_Working.mask.target)) { error = "PCG stroke target changed; finish or cancel the original stroke"; return false; }
	VansPcgPixelRect changed;
	if (!m_Stroke.AddPoint(m_Working.mask, worldX, worldZ, erase, changed, error)) return false;
	m_StrokeDirty.Include(changed);
	m_PendingPixels.Include(changed);
	return true;
}

bool VansPcgMaskAuthoringSession::EndStroke(std::string& error)
{
	VansPcgMaskEdit edit;
	if (!m_Stroke.Finish(m_Working.mask, edit, error)) return false;
	m_StrokeDirty = {};
	if (edit.Empty()) return true;
	RefreshPixelDirty();
	if (!PublishWorkingSnapshot(error))
	{
		std::string ignored;
		edit.Apply(m_Working.mask, false, ignored);
		RefreshPixelDirty();
		return false;
	}
	// 历史只保留触及的块。每个闭包绑定原会话，不通过当前选中的草项寻址。
	const std::weak_ptr<VansPcgMaskAuthoringSession> weak = shared_from_this();
	const auto patch = std::make_shared<const VansPcgMaskEdit>(std::move(edit));
	const auto result = VansAssetDocumentEditService::RecordExternalEdit(m_Document->sourceDocument,
		[weak, patch]() { const auto self = weak.lock(); return self && self->ApplyEdit(*patch, false); },
		[weak, patch]() { const auto self = weak.lock(); return self && self->ApplyEdit(*patch, true); });
	if (!result.success)
	{
		ApplyEdit(*patch, false);
		error = result.message;
		return false;
	}
	return true;
}

bool VansPcgMaskAuthoringSession::CancelStroke(std::string& error)
{
	error.clear();
	if (!StrokeActive()) return true;
	if (!m_Stroke.Cancel(m_Working.mask, error)) return false;
	m_PendingPixels.Include(m_StrokeDirty);
	m_StrokeDirty = {};
	RefreshPixelDirty();
	return true;
}

bool VansPcgMaskAuthoringSession::ApplyEdit(const VansPcgMaskEdit& edit, bool redo)
{
	if (StrokeActive()) return false;
	std::string error;
	if (!edit.Apply(m_Working.mask, redo, error)) return false;
	if (!PublishWorkingSnapshot(error))
	{
		edit.Apply(m_Working.mask, !redo, error);
		return false;
	}
	RefreshPixelDirty();
	m_PendingPixels.Include(edit.dirtyRect);
	return true;
}

bool VansPcgMaskAuthoringSession::PublishWorkingSnapshot(std::string& error)
{
	const auto diagnostics = ValidatePcgMaskAsset(m_Working, true);
	if (!diagnostics.empty()) { error = diagnostics.front(); return false; }
	std::uint64_t hash = m_Working.mask.ContentHash();
	hash ^= m_Document->sourceDocument.CurrentStateId() + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
	if (!m_Repository->Publish<VansPcgMaskAsset>(m_Record.guid, VansAssetType::PcgMask, hash,
		std::make_shared<const VansPcgMaskAsset>(m_Working), m_Working.Dependencies(), error).IsValid()) return false;
	// 主对象发布会失效旧视图；同步发布内存 metadata，供后续配置预览解析依赖。
	return VansAssetObjectBootstrapper::PublishMetadataSerialized(m_Record,
		m_Document->metaDocument.SerializedRootSnapshot(),hash,*m_Repository,error);
}

VansPcgPixelRect VansPcgMaskAuthoringSession::TakePendingPixelChange()
{
	const auto result = m_PendingPixels;
	m_PendingPixels = {};
	return result;
}

void VansPcgMaskAuthoringSession::RefreshPixelDirty()
{
	m_PixelDirty = !m_LoadedFingerprint.exists || m_Working.mask.pixels != m_SavedPixels ||
		m_Working.mask.width!=m_SavedWidth || m_Working.mask.height!=m_SavedHeight;
}

bool VansPcgMaskAuthoringSession::IsDirty() const
{
	return m_PixelDirty || !m_StrokeDirty.Empty();
}

bool VansPcgMaskAuthoringSession::StageSave(std::vector<VansStagedFile>& files, std::string& error)
{
	error.clear();
	files.clear();
	m_HasStagedPixels = false;
	if (StrokeActive()) { error = "Finish or cancel the PCG stroke before saving"; return false; }
	if (!m_PixelDirty) return true;
	const auto current = VansAssetDocument::Fingerprint(m_PixelPath, error);
	if (!error.empty()) return false;
	if (current != m_LoadedFingerprint) { error = "PCG Mask pixels changed on disk: " + m_PixelPath.string(); return false; }
	std::string bytes;
	if (!VansPcgMaskAssetCodec::EncodePixels(m_Working, bytes, error)) return false;
	VansScopedIOContext io(VansIODomain::Authoring, "PcgMaskAuthoring.StagePixels", true);
	VansStagedFile stage;
	if (!VansFileStorage::StageWriteBytes(m_PixelPath, bytes, stage, error)) return false;
	files.push_back(std::move(stage));
	m_StagedPixels = m_Working.mask.pixels;
	m_StagedWidth=m_Working.mask.width; m_StagedHeight=m_Working.mask.height;
	m_StagedByteHash = 14695981039346656037ull;
	for (const unsigned char byte : bytes) { m_StagedByteHash ^= byte; m_StagedByteHash *= 1099511628211ull; }
	m_StagedByteCount = bytes.size();
	m_HasStagedPixels = true;
	return true;
}

bool VansPcgMaskAuthoringSession::ObservePublishedSave(std::string& error)
{
	error.clear();
	if (!m_HasStagedPixels) return true;
	m_ObservedFingerprint = VansAssetDocument::Fingerprint(m_PixelPath, error);
	if (!error.empty()) return false;
	if (!m_ObservedFingerprint.exists) { error = "Saved PCG Mask pixels are missing"; return false; }
	if (m_ObservedFingerprint.contentHash != m_StagedByteHash || m_ObservedFingerprint.size != m_StagedByteCount)
	{ error = "Saved PCG Mask pixels do not match the staged edit"; return false; }
	return true;
}

void VansPcgMaskAuthoringSession::AdoptObservedSave()
{
	if (!m_HasStagedPixels) return;
	m_LoadedFingerprint = m_ObservedFingerprint;
	m_SavedPixels = std::move(m_StagedPixels);
	m_SavedWidth=m_StagedWidth; m_SavedHeight=m_StagedHeight;
	m_HasStagedPixels = false;
	RefreshPixelDirty();
}

bool VansPcgMaskAuthoringSession::RestoreContent(const VansPcgMaskAsset& asset)
{
	if (StrokeActive()) return false;
	m_Working=asset;
	RefreshPixelDirty();
	m_FullRefresh=true;
	m_PendingPixels={0,0,m_Working.mask.width,m_Working.mask.height};
	return true;
}

bool VansPcgMaskAuthoringSession::ReplaceContent(VansPcgMaskAsset asset, std::string& error)
{
	error.clear();
	if (StrokeActive() || !SyncDefinitionFromDocument(error))
	{ if (error.empty()) error="Finish the stroke before changing Mask data."; return false; }
	if (!(asset.mask.target==m_Working.mask.target) || asset.pixelAsset!=m_Working.pixelAsset)
	{ error="Mask editing cannot transfer ownership or share pixel assets."; return false; }
	const auto validation=ValidatePcgMaskAsset(asset,true);
	if (!validation.empty()) {error=validation.front();return false;}
	VansSerializedValue root;
	if (!VansPcgMaskAssetCodec::EncodeDefinition(asset,root,error)) return false;
	const auto before=std::make_shared<const VansPcgMaskAsset>(m_Working);
	const auto after=std::make_shared<const VansPcgMaskAsset>(std::move(asset));
	const std::weak_ptr<VansPcgMaskAuthoringSession> weak=shared_from_this();
	RestoreContent(*after);
	const auto edit=VansAssetDocumentEditService::RecordExternalEdit(m_Document->sourceDocument,std::move(root),
		[weak,before] {const auto self=weak.lock();return self && self->RestoreContent(*before);},
		[weak,after] {const auto self=weak.lock();return self && self->RestoreContent(*after);});
	if (!edit) {RestoreContent(*before);error=edit.message;return false;}
	return PublishWorkingSnapshot(error);
}
}
