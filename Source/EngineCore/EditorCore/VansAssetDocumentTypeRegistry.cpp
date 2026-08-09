#include "VansAssetDocumentTypeRegistry.h"

#include "VansEditorPropertyDescriptorRegistry.h"

#include "../TimelineCore/VansTimelineSerialization.h"
#include "../TimelineCore/VansTimelineValidator.h"

namespace Vans
{
VansAssetDocumentTypeRegistry::VansAssetDocumentTypeRegistry()
{
	VansAssetDocumentTypeDescriptor timeline;
	timeline.validateBeforeSave = [](const std::filesystem::path&, const VansSerializedValue& root)
	{
		std::vector<VansAssetDocumentDiagnostic> result;
		VansTimelineAsset asset;
		std::string error;
		if (!VansTimelineSerialization::DecodeSerialized(root, asset, error))
		{
			result.push_back({ VansAssetDocumentDiagnosticSeverity::Error, {}, std::move(error) });
			return result;
		}
		VansTimelineValidationContext context;
		context.runtimeValidation = false;
		context.supportsPropertyDescriptor = [](
			std::uint16_t componentTypeId,
			const std::string& descriptorId,
			VansTimelineChannelType valueType)
		{
			const EditorAnimatablePropertyDescriptor* descriptor =
				VansEditorPropertyDescriptorRegistry::FindAnimatable(descriptorId);
			return descriptor && descriptor->componentTypeId == componentTypeId &&
				descriptor->valueType == valueType;
		};
		for (const VansTimelineDiagnostic& diagnostic : VansTimelineValidator::Validate(asset, context))
		{
			VansAssetDocumentDiagnosticSeverity severity = VansAssetDocumentDiagnosticSeverity::Info;
			if (diagnostic.severity == VansTimelineDiagnosticSeverity::Warning)
				severity = VansAssetDocumentDiagnosticSeverity::Warning;
			else if (diagnostic.severity == VansTimelineDiagnosticSeverity::Error)
				severity = VansAssetDocumentDiagnosticSeverity::Error;
			result.push_back({ severity, diagnostic.propertyPath, diagnostic.message });
		}
		return result;
	};
	std::string ignored;
	Register(VansAssetType::Timeline, std::move(timeline), ignored);
}

VansAssetDocumentTypeRegistry& VansAssetDocumentTypeRegistry::Get()
{
	static VansAssetDocumentTypeRegistry registry;
	return registry;
}

bool VansAssetDocumentTypeRegistry::Register(
	VansAssetType type,
	VansAssetDocumentTypeDescriptor descriptor,
	std::string& error)
{
	error.clear();
	if (type == VansAssetType::Unknown || !descriptor.validateBeforeSave)
	{
		error = "Asset document type registration requires a concrete type and save validator";
		return false;
	}
	if (!m_Descriptors.emplace(type, std::move(descriptor)).second)
	{
		error = "Asset document type is already registered";
		return false;
	}
	return true;
}

const VansAssetDocumentTypeDescriptor* VansAssetDocumentTypeRegistry::Find(VansAssetType type) const
{
	const auto found = m_Descriptors.find(type);
	return found == m_Descriptors.end() ? nullptr : &found->second;
}

std::vector<VansAssetDocumentDiagnostic> VansAssetDocumentTypeRegistry::ValidateBeforeSave(
	VansAssetType type,
	const std::filesystem::path& path,
	const VansSerializedValue& root) const
{
	const VansAssetDocumentTypeDescriptor* descriptor = Find(type);
	return descriptor && descriptor->validateBeforeSave
		? descriptor->validateBeforeSave(path, root)
		: std::vector<VansAssetDocumentDiagnostic>{};
}
}
