#include "VansPythonSerializedFieldInjector.h"

namespace Vans
{
namespace py = pybind11;

py::object BuildPythonSerializedFieldObject(
    const VansPythonSerializedFieldValue& field,
    const PythonObjectReferenceResolver& resolveObjectReference)
{
    switch (field.type)
    {
    case VansPythonSerializedFieldType::Bool:
        return py::bool_(field.boolValue);
    case VansPythonSerializedFieldType::Int:
        return py::int_(field.intValue);
    case VansPythonSerializedFieldType::Float:
        return py::float_(field.floatValue);
    case VansPythonSerializedFieldType::String:
        return py::str(field.stringValue);
    case VansPythonSerializedFieldType::ObjectReference:
        return resolveObjectReference ? resolveObjectReference(field.objectReference) : py::none();
    default:
        return py::none();
    }
}

void ApplyPythonSerializedFields(
    const std::unordered_map<std::string, VansPythonSerializedFieldValue>& fields,
    py::object& instance,
    const PythonObjectReferenceResolver& resolveObjectReference)
{
    for (const auto& [name, value] : fields)
    {
        if (name.empty())
            continue;
        instance.attr(name.c_str()) =
            BuildPythonSerializedFieldObject(value, resolveObjectReference);
    }
}
}
