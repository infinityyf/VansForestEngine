#include "VansTimelineApplierRegistry.h"

#include "../Util/VansLog.h"

#include "VansTimelinePreAnimatedState.h"
#include "VansTimelineWriterRegistry.h"

#include <algorithm>

namespace Vans
{
bool VansTimelineApplierRegistry::Register(
	std::shared_ptr<IVansTimelineOutputApplier> applier,
	std::string& error)
{
	error.clear();
	if (m_Sealed) { error = "Timeline.ApplierRegistrySealed"; return false; }
	if (!applier || !applier->OutputType() || applier->StableName().empty() ||
		applier->PayloadSize() == 0 || applier->PayloadAlignment() == 0)
	{
		error = "Timeline.ApplierRegistrationInvalid";
		return false;
	}
	if (m_ByType.find(applier->OutputType()) != m_ByType.end())
	{
		error = "Timeline.ApplierDuplicate";
		return false;
	}
	const VansTimelineApplierSlot slot = static_cast<VansTimelineApplierSlot>(m_Appliers.size());
	m_ByType.emplace(applier->OutputType(), slot);
	m_Appliers.push_back(std::move(applier));
	return true;
}

bool VansTimelineApplierRegistry::Seal(bool allowEmpty, std::string& error)
{
	error.clear();
	if (m_Appliers.empty() && !allowEmpty)
	{
		error = "Timeline.ApplierRegistryEmpty";
		return false;
	}
	m_Sealed = true;
	return true;
}

std::uint64_t VansTimelineApplierRegistry::ManifestHash() const
{
	if (!m_Sealed) return 0;
	std::vector<const IVansTimelineOutputApplier*> ordered;
	ordered.reserve(m_Appliers.size());
	for (const auto& applier : m_Appliers) ordered.push_back(applier.get());
	std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right)
	{
		return left->OutputType().value < right->OutputType().value;
	});
	std::uint64_t hash = VansStableHash64("Timeline.ApplierManifest");
	for (const auto* applier : ordered)
	{
		hash ^= applier->OutputType().value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
		hash ^= static_cast<std::uint64_t>(applier->PayloadSize()) << 32;
		hash ^= static_cast<std::uint64_t>(applier->PayloadAlignment());
	}
	return hash;
}

std::vector<VansTimelineOutputTypeId> VansTimelineApplierRegistry::OutputTypes() const
{
	std::vector<VansTimelineOutputTypeId> types;
	types.reserve(m_Appliers.size());
	for (const auto& applier : m_Appliers) types.push_back(applier->OutputType());
	return types;
}

const IVansTimelineOutputApplier* VansTimelineApplierRegistry::Resolve(
	VansTimelineOutputTypeId type) const
{
	const VansTimelineApplierSlot slot = SlotOf(type);
	return slot < m_Appliers.size() ? m_Appliers[slot].get() : nullptr;
}

VansTimelineApplierSlot VansTimelineApplierRegistry::SlotOf(VansTimelineOutputTypeId type) const
{
	const auto found = m_ByType.find(type);
	return found == m_ByType.end() ? VansInvalidTimelineApplierSlot : found->second;
}

IVansTimelineOutputApplier* VansTimelineApplierRegistry::At(VansTimelineApplierSlot slot) const
{
	return slot < m_Appliers.size() ? m_Appliers[slot].get() : nullptr;
}

void VansTimelineApplierRegistry::Apply(
	const VansCompiledTimeline& timeline,
	std::vector<VansTimelineEvaluationOutput>& outputs,
	VansTimelineBindingResolver& bindings,
	const VansTimelineSessionScope& scope,
	VansTimelineWriterRegistry& writers,
	VansTimelinePreAnimatedState& preAnimated,
	VansTimelineDiagnostics& diagnostics) const
{
	for (VansTimelineEvaluationOutput& output : outputs)
	{
		const VansTimelineApplierSlot slot = SlotOf(output.typeId);
		IVansTimelineOutputApplier* applier = At(slot);
		if (!applier)
		{
			const auto& phaseTracks = timeline.Tracks(output.phase);
			if (output.trackIndex < phaseTracks.size() && !phaseTracks[output.trackIndex].outputApplierRequired)
				continue;
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.ApplierMissing", {}, output.sourceTrackId, "outputType",
				"Timeline output has no registered applier", output.session });
			continue;
		}
		if (output.payload.size != applier->PayloadSize() || output.payload.alignment != applier->PayloadAlignment())
		{
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.OutputPayloadMismatch", {}, output.sourceTrackId, "payload",
				"Timeline output payload does not match its applier contract", output.session });
			continue;
		}
		const auto& phaseTracks = timeline.Tracks(output.phase);
		if (output.trackIndex >= phaseTracks.size()) continue;
		const VansCompiledTimelineTrack* track = &phaseTracks[output.trackIndex];
		const VansCompiledTimelineSection* section = output.sectionIndex < track->sections.size()
			? &track->sections[output.sectionIndex] : nullptr;
		VansTimelineWriterIdentity writerIdentity;
		writerIdentity.session = output.session;
		writerIdentity.root = output.root;
		writerIdentity.outputType = output.typeId;
		writerIdentity.trackIndex = output.trackIndex;
		writerIdentity.sectionIndex = output.sectionIndex;
		writerIdentity.completion = output.completion;
		output.writer = writers.Acquire(writerIdentity);
		VansTimelineApplyContext context{ timeline, *track, section, output.session, output.root,
			output.sessionKind, output.writer, output.order, output.blendMode, output.completion };
		context.scope = scope ? &scope : nullptr;
		context.bindings = &bindings;
		context.diagnostics = &diagnostics;
		VansTimelineApplyResult result = applier->Apply(context, output.target, output.payload);
		if (result.status == VansTimelineApplyStatus::Failed)
		{
			VANS_LOG_ERROR("[Timeline] Apply failed track=" << output.sourceTrackId <<
				" session=" << output.session.index << ": " << result.error);
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.ApplyFailed", {}, output.sourceTrackId, "apply",
				result.error.empty() ? "Timeline applier failed" : result.error, output.session });
		}
		else if (result.restore.handle.IsValid())
		{
			result.restore.applier = slot;
			result.restore.writer = output.writer;
			if (output.retainsPreAnimatedState)
			{
				const VansTimelinePreAnimatedStoreResult stored = preAnimated.Store(result.restore);
				if (stored.overlappingWriter.IsValid())
				{
					std::string message = "A second active Timeline writer targets the same resource";
					if (const VansTimelineWriterIdentity* active = writers.Resolve(stored.overlappingWriter))
					{
						message += "; existing session=" + std::to_string(active->session.index) +
							", track=" + std::to_string(active->trackIndex) +
							", section=" + std::to_string(active->sectionIndex);
					}
					diagnostics.push_back({ VansTimelineDiagnosticSeverity::Warning,
						"Timeline.ResourceWriterOverlap", {}, output.sourceTrackId, "resource",
						std::move(message), output.session });
				}
			}
		}
		if (!output.retainsPreAnimatedState)
		{
			if (result.restore.handle.IsValid())
			{
				applier->Restore(result.restore);
				diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
					"Timeline.EdgeOutputReturnedRestoreToken", {}, output.sourceTrackId, "restore",
					"A non-retained Timeline edge output must not capture pre-animated state", output.session });
			}
			applier->ReleaseWriter(output.writer);
			writers.Release(output.writer);
		}
	}
}

void VansTimelineApplierRegistry::ReleaseWriter(VansTimelineWriterHandle writer) const
{
	for (const auto& applier : m_Appliers) applier->ReleaseWriter(writer);
}

void VansTimelineApplierRegistry::ReleaseAll() const
{
	for (const auto& applier : m_Appliers) applier->ReleaseAll();
}
}
