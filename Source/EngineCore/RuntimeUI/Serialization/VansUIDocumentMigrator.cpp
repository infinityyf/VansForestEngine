#include "VansUIDocumentMigrator.h"

#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

namespace VansRuntime
{
	namespace
	{
		const char* KindName(VansUIDocumentKind kind)
		{
			switch (kind)
			{
			case VansUIDocumentKind::Screen: return "Screen";
			case VansUIDocumentKind::Component: return "Component";
			case VansUIDocumentKind::ThemeTokens: return "ThemeTokens";
			case VansUIDocumentKind::Localization: return "Localization";
			default: return "";
			}
		}
	}

	bool VansUIDocumentMigrator::MigrateToCurrent(
		VansUIAssetDocument& document,
		VansUIDocumentKind kind,
		std::vector<std::string>& diagnostics)
	{
		if (document.root.kind != Vans::VansSerializedValue::Kind::Object)
		{
			diagnostics.push_back("UI document root must be an object before migration.");
			return false;
		}

		const std::int64_t schemaVersion = Vans::ReadSerializedIntField(document.root, "schemaVersion", 0);
		if (schemaVersion <= 0)
		{
			Vans::SetSerializedObjectField(
				document.root,
				"schemaVersion",
				Vans::VansSerializedValue::Int(CurrentSchemaVersion));
			diagnostics.push_back("UI document migration added schemaVersion=1.");
		}
		else if (schemaVersion > CurrentSchemaVersion)
		{
			diagnostics.push_back("UI document schemaVersion is newer than this runtime supports.");
			return false;
		}

		const std::string expectedType = KindName(kind);
		const std::string actualType = Vans::ReadSerializedStringField(document.root, "type");
		if (actualType.empty())
		{
			Vans::SetSerializedObjectField(
				document.root,
				"type",
				Vans::VansSerializedValue::String(expectedType));
			diagnostics.push_back("UI document migration added type=" + expectedType + ".");
		}
		else if (actualType != expectedType)
		{
			diagnostics.push_back("UI document type mismatch. Expected " + expectedType + ", got " + actualType + ".");
			return false;
		}

		return true;
	}
}
