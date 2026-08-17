#include "VansTimelineCompiledDataWriter.h"

namespace Vans
{
VansTimelineCompiledDataView VansTimelineCompiledDataWriter::WriteValues(
	std::vector<VansTimelineValue> values)
{
	VansTimelineCompiledDataView view;
	view.valueOffset = static_cast<std::uint32_t>(m_Values.size());
	view.valueCount = static_cast<std::uint32_t>(values.size());
	for (auto& value : values) m_Values.push_back(std::move(value));
	return view;
}

bool VansTimelineCompiledDataWriter::WriteSchema(
	const VansTimelineSourceSchema& schema,
	const VansSerializedValue& extensionData,
	VansTimelineCompiledDataView& view,
	VansTimelineDiagnostics& diagnostics,
	const VansTimelineId& objectId)
{
	std::vector<VansTimelineValue> values;
	values.reserve(schema.fields.size());
	bool valid = true;
	for (const VansTimelineSourceField& field : schema.fields)
	{
		const VansSerializedValue* encoded = VansTimelineFindSourceField(extensionData, field.name);
		if (!encoded)
		{
			if (field.required)
			{
				diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
					"Timeline.SourceFieldMissing", {}, objectId, field.name,
					"Required Timeline extension field is missing" });
				valid = false;
			}
			values.push_back(field.defaultValue);
			continue;
		}
		VansTimelineValue value;
		if (!VansTimelineDecodeSourceValue(*encoded, field.type, value))
		{
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.SourceFieldTypeMismatch", {}, objectId, field.name,
				"Timeline extension field has the wrong value type" });
			valid = false;
			values.push_back(field.defaultValue);
			continue;
		}
		if (!field.enumValues.empty())
		{
			const auto* enumValue = std::get_if<std::string>(&value);
			bool found = false;
			if (enumValue)
				for (const std::string& candidate : field.enumValues)
					if (*enumValue == candidate) { found = true; break; }
			if (!found)
			{
				diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
					"Timeline.SourceEnumValueInvalid", {}, objectId, field.name,
					"Timeline extension enum value is not declared by its schema" });
				valid = false;
			}
		}
		values.push_back(std::move(value));
	}
	view = WriteValues(std::move(values));
	return valid;
}
}
