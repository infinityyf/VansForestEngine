#pragma once

#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansAssetDocumentDiagnosticSeverity { Info, Warning, Error };

struct VansAssetDocumentDiagnostic
{
	VansAssetDocumentDiagnosticSeverity severity = VansAssetDocumentDiagnosticSeverity::Info;
	std::string propertyPath;
	std::string message;
};

using VansAssetDocumentValidateCallback = std::function<std::vector<VansAssetDocumentDiagnostic>(
	const std::filesystem::path&, const VansSerializedValue&)>;
using VansAssetDocumentDependencyCallback = std::function<std::vector<std::string>(
	const std::filesystem::path&, const VansSerializedValue&)>;

struct VansAssetDocumentTypeDescriptor
{
	VansAssetDocumentValidateCallback validateBeforeSave;
	VansAssetDocumentDependencyCallback collectDependencies;
};

class VansAssetDocumentTypeRegistry
{
public:
	static VansAssetDocumentTypeRegistry& Get();

	bool Register(VansAssetType type, VansAssetDocumentTypeDescriptor descriptor, std::string& error);
	const VansAssetDocumentTypeDescriptor* Find(VansAssetType type) const;
	std::vector<VansAssetDocumentDiagnostic> ValidateBeforeSave(
		VansAssetType type,
		const std::filesystem::path& path,
		const VansSerializedValue& root) const;

private:
	VansAssetDocumentTypeRegistry();
	std::unordered_map<VansAssetType, VansAssetDocumentTypeDescriptor> m_Descriptors;
};
}
