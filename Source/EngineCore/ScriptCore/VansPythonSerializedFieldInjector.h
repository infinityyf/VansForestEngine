#pragma once

#include "VansPythonSerializedField.h"

#include <functional>
#include <pybind11/pybind11.h>
#include <string>
#include <unordered_map>

namespace Vans
{
using PythonObjectReferenceResolver =
    std::function<pybind11::object(const VansPythonSerializedObjectReference&)>;

pybind11::object BuildPythonSerializedFieldObject(
    const VansPythonSerializedFieldValue& field,
    const PythonObjectReferenceResolver& resolveObjectReference = {});

void ApplyPythonSerializedFields(
    const std::unordered_map<std::string, VansPythonSerializedFieldValue>& fields,
    pybind11::object& instance,
    const PythonObjectReferenceResolver& resolveObjectReference = {});
}
