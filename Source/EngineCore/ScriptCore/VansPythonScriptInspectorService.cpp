#include "VansPythonScriptInspectorService.h"

#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "VansScriptContext.h"
#include "VansPythonScriptFieldSchema.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <utility>

namespace Vans
{
namespace
{
using SerializedFieldMap = std::vector<std::pair<std::string, VansSerializedValue>>;

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string AnnotationName(const py::handle& annotation)
{
    try
    {
        if (py::hasattr(annotation, "__name__"))
            return annotation.attr("__name__").cast<std::string>();
        return py::str(annotation).cast<std::string>();
    }
    catch (...)
    {
        return {};
    }
}

VansSerializedValue SceneEntityReferenceDefault()
{
    return MakeSerializedSceneEntityObjectReference("");
}

VansSerializedValue SceneComponentReferenceDefault(const std::string& componentType)
{
    return MakeSerializedSceneComponentObjectReference("", "", componentType);
}

VansSerializedValue ProjectAssetReferenceDefault(const std::string& assetType)
{
    return MakeSerializedProjectAssetObjectReference("", assetType);
}

PythonScriptFieldDescriptor ObjectReferenceDescriptorForAnnotation(const std::string& name, const std::string& annotationName)
{
    PythonScriptFieldDescriptor descriptor;
    descriptor.name = name;
    if (IsPythonSceneEntityReferenceAnnotation(annotationName))
    {
        descriptor.kind = PythonScriptInspectableFieldKind::SceneEntityReference;
        descriptor.defaultValue = SceneEntityReferenceDefault();
        return descriptor;
    }

    const std::string assetType = CanonicalPythonInspectorAssetTypeName(annotationName);
    if (IsPythonProjectAssetReferenceAnnotation(annotationName) || !assetType.empty())
    {
        descriptor.kind = PythonScriptInspectableFieldKind::ProjectAssetReference;
        descriptor.assetType = assetType;
        descriptor.defaultValue = ProjectAssetReferenceDefault(descriptor.assetType);
        return descriptor;
    }

    const std::string componentType = CanonicalPythonInspectorComponentTypeName(annotationName);
    if (!componentType.empty() || IsPythonSceneComponentReferenceAnnotation(annotationName))
    {
        descriptor.kind = PythonScriptInspectableFieldKind::SceneComponentReference;
        descriptor.componentType = componentType;
        descriptor.defaultValue = SceneComponentReferenceDefault(componentType);
        return descriptor;
    }

    return descriptor;
}

PythonScriptFieldDescriptor DescriptorForAnnotation(const std::string& name, const py::handle& annotation)
{
    PythonScriptFieldDescriptor descriptor = ObjectReferenceDescriptorForAnnotation(name, AnnotationName(annotation));
    if (descriptor.kind != PythonScriptInspectableFieldKind::Unknown)
        return descriptor;

    const std::string annotationName = AnnotationName(annotation);
    const std::string type = Lower(annotationName);
    descriptor.name = name;
    if (type == "bool" || type.find("<class 'bool'>") != std::string::npos)
    {
        descriptor.kind = PythonScriptInspectableFieldKind::Bool;
        descriptor.defaultValue = VansSerializedValue::Bool(false);
    }
    else if (type == "int" || type.find("<class 'int'>") != std::string::npos)
    {
        descriptor.kind = PythonScriptInspectableFieldKind::Int;
        descriptor.defaultValue = VansSerializedValue::Int(0);
    }
    else if (type == "float" || type.find("<class 'float'>") != std::string::npos)
    {
        descriptor.kind = PythonScriptInspectableFieldKind::Float;
        descriptor.defaultValue = VansSerializedValue::Float(0.0);
    }
    else if (type == "str" || type.find("<class 'str'>") != std::string::npos)
    {
        descriptor.kind = PythonScriptInspectableFieldKind::String;
        descriptor.defaultValue = VansSerializedValue::String("");
    }
    return descriptor;
}

VansSerializedValue ToSerializableDefault(const py::handle& value)
{
    if (value.is_none())
        return VansSerializedValue::Null();
    if (py::isinstance<py::bool_>(value))
        return VansSerializedValue::Bool(value.cast<bool>());
    if (py::isinstance<py::int_>(value))
        return VansSerializedValue::Int(value.cast<std::int64_t>());
    if (py::isinstance<py::float_>(value))
        return VansSerializedValue::Float(value.cast<double>());
    if (py::isinstance<py::str>(value))
        return VansSerializedValue::String(value.cast<std::string>());
    return VansSerializedValue::Null();
}

double ReadNumberOr(const py::dict& dict, const char* key, double fallback)
{
    if (!dict.contains(key))
        return fallback;
    try
    {
        return py::cast<double>(dict[key]);
    }
    catch (...)
    {
        return fallback;
    }
}

void ReadNumericMetadata(const py::dict& dict, PythonScriptFieldDescriptor& descriptor)
{
    if (dict.contains("min"))
    {
        descriptor.hasMinValue = true;
        descriptor.minValue = ReadNumberOr(dict, "min", 0.0);
    }
    if (dict.contains("max"))
    {
        descriptor.hasMaxValue = true;
        descriptor.maxValue = ReadNumberOr(dict, "max", 0.0);
    }
    if (dict.contains("speed"))
    {
        descriptor.hasSpeed = true;
        descriptor.speed = ReadNumberOr(dict, "speed", descriptor.speed);
    }
}

PythonScriptFieldDescriptor DescriptorForExplicitField(const std::string& name, const py::handle& descriptorValue)
{
    PythonScriptFieldDescriptor descriptor;
    descriptor.name = name;
    if (name.empty() || name.front() == '_')
        return descriptor;

    if (py::isinstance<py::dict>(descriptorValue))
    {
        py::dict dict = py::reinterpret_borrow<py::dict>(descriptorValue);
        const std::string type = dict.contains("type") ? py::str(dict["type"]).cast<std::string>() : std::string();
        const std::string normalizedType = Lower(type);
        const std::string canonicalTypeAsset = CanonicalPythonInspectorAssetTypeName(type);
        const std::string canonicalTypeComponent = CanonicalPythonInspectorComponentTypeName(type);
        const std::string componentType = dict.contains("componentType")
            ? py::str(dict["componentType"]).cast<std::string>() : std::string();
        const std::string assetType = dict.contains("assetType")
            ? py::str(dict["assetType"]).cast<std::string>() : std::string();
        ReadNumericMetadata(dict, descriptor);
        if (normalizedType == "entity" || IsPythonSceneEntityReferenceAnnotation(type))
        {
            descriptor.kind = PythonScriptInspectableFieldKind::SceneEntityReference;
            descriptor.defaultValue = SceneEntityReferenceDefault();
        }
        else if (normalizedType == "component" ||
            IsPythonSceneComponentReferenceAnnotation(type) ||
            !canonicalTypeComponent.empty())
        {
            descriptor.kind = PythonScriptInspectableFieldKind::SceneComponentReference;
            descriptor.componentType = !componentType.empty()
                ? CanonicalPythonInspectorComponentTypeName(componentType)
                : canonicalTypeComponent;
            if (descriptor.componentType.empty() && !componentType.empty())
                descriptor.componentType = componentType;
            descriptor.defaultValue = SceneComponentReferenceDefault(descriptor.componentType);
        }
        else if (normalizedType == "asset" ||
            IsPythonProjectAssetReferenceAnnotation(type) ||
            !canonicalTypeAsset.empty())
        {
            descriptor.kind = PythonScriptInspectableFieldKind::ProjectAssetReference;
            descriptor.assetType = assetType.empty()
                ? canonicalTypeAsset
                : CanonicalPythonInspectorAssetTypeName(assetType);
            if (descriptor.assetType.empty() && !assetType.empty())
                descriptor.assetType = assetType;
            descriptor.defaultValue = ProjectAssetReferenceDefault(descriptor.assetType);
        }
        else if (normalizedType == "bool")
        {
            descriptor.kind = PythonScriptInspectableFieldKind::Bool;
            descriptor.defaultValue = dict.contains("default")
                ? ToSerializableDefault(dict["default"])
                : VansSerializedValue::Bool(false);
        }
        else if (normalizedType == "int")
        {
            descriptor.kind = PythonScriptInspectableFieldKind::Int;
            descriptor.defaultValue = dict.contains("default")
                ? ToSerializableDefault(dict["default"])
                : VansSerializedValue::Int(0);
        }
        else if (normalizedType == "float")
        {
            descriptor.kind = PythonScriptInspectableFieldKind::Float;
            descriptor.defaultValue = dict.contains("default")
                ? ToSerializableDefault(dict["default"])
                : VansSerializedValue::Float(0.0);
        }
        else if (normalizedType == "string" || normalizedType == "str")
        {
            descriptor.kind = PythonScriptInspectableFieldKind::String;
            descriptor.defaultValue = dict.contains("default")
                ? ToSerializableDefault(dict["default"])
                : VansSerializedValue::String("");
        }
        else if (dict.contains("default"))
        {
            descriptor.defaultValue = ToSerializableDefault(dict["default"]);
        }
    }
    else
    {
        descriptor.defaultValue = ToSerializableDefault(descriptorValue);
    }

    if (descriptor.kind == PythonScriptInspectableFieldKind::Unknown && !descriptor.defaultValue.IsNull())
    {
        if (descriptor.defaultValue.kind == VansSerializedValue::Kind::Bool)
            descriptor.kind = PythonScriptInspectableFieldKind::Bool;
        else if (descriptor.defaultValue.kind == VansSerializedValue::Kind::Int)
            descriptor.kind = PythonScriptInspectableFieldKind::Int;
        else if (descriptor.defaultValue.kind == VansSerializedValue::Kind::Float)
            descriptor.kind = PythonScriptInspectableFieldKind::Float;
        else if (descriptor.defaultValue.kind == VansSerializedValue::Kind::String)
            descriptor.kind = PythonScriptInspectableFieldKind::String;
    }
    return descriptor;
}

void UpsertDescriptor(std::vector<PythonScriptFieldDescriptor>& descriptors, PythonScriptFieldDescriptor descriptor)
{
    if (descriptor.name.empty() || descriptor.defaultValue.IsNull())
        return;
    for (PythonScriptFieldDescriptor& existing : descriptors)
    {
        if (existing.name == descriptor.name)
        {
            existing = std::move(descriptor);
            return;
        }
    }
    descriptors.push_back(std::move(descriptor));
}

SerializedFieldMap BuildDefaultFields(const std::vector<PythonScriptFieldDescriptor>& descriptors)
{
    SerializedFieldMap fields;
    for (const PythonScriptFieldDescriptor& descriptor : descriptors)
        if (!descriptor.name.empty() && !descriptor.defaultValue.IsNull())
            fields.emplace_back(descriptor.name, descriptor.defaultValue);
    return fields;
}

void UpsertExplicitFieldDictionary(
    py::object explicitFields,
    std::vector<PythonScriptFieldDescriptor>& descriptors)
{
    if (!py::isinstance<py::dict>(explicitFields))
        return;

    py::dict dict = py::reinterpret_borrow<py::dict>(explicitFields);
    for (const auto& item : dict)
    {
        UpsertDescriptor(descriptors, DescriptorForExplicitField(
            py::str(item.first).cast<std::string>(),
            item.second));
    }
}
}

const char* ToString(PythonScriptInspectableFieldKind kind)
{
    switch (kind)
    {
    case PythonScriptInspectableFieldKind::Bool: return "Bool";
    case PythonScriptInspectableFieldKind::Int: return "Int";
    case PythonScriptInspectableFieldKind::Float: return "Float";
    case PythonScriptInspectableFieldKind::String: return "String";
    case PythonScriptInspectableFieldKind::SceneEntityReference: return "SceneEntityReference";
    case PythonScriptInspectableFieldKind::SceneComponentReference: return "SceneComponentReference";
    case PythonScriptInspectableFieldKind::ProjectAssetReference: return "ProjectAssetReference";
    default: return "Unknown";
    }
}

bool HasPythonScriptFieldDefault(const PythonScriptFieldDescriptor& descriptor)
{
    return !descriptor.defaultValue.IsNull();
}

bool NormalizePythonScriptFieldValue(
    VansSerializedValue& value,
    const PythonScriptFieldDescriptor& descriptor)
{
    switch (descriptor.kind)
    {
    case PythonScriptInspectableFieldKind::Bool:
        if (value.kind != VansSerializedValue::Kind::Bool)
        {
            value = descriptor.defaultValue.kind == VansSerializedValue::Kind::Bool
                ? descriptor.defaultValue
                : VansSerializedValue::Bool(false);
            return true;
        }
        return false;
    case PythonScriptInspectableFieldKind::Int:
        if (value.kind != VansSerializedValue::Kind::Int)
        {
            value = descriptor.defaultValue.kind == VansSerializedValue::Kind::Int
                ? descriptor.defaultValue
                : VansSerializedValue::Int(0);
            return true;
        }
        return false;
    case PythonScriptInspectableFieldKind::Float:
        if (value.kind != VansSerializedValue::Kind::Float &&
            value.kind != VansSerializedValue::Kind::Int)
        {
            value = descriptor.defaultValue.kind == VansSerializedValue::Kind::Float ||
                descriptor.defaultValue.kind == VansSerializedValue::Kind::Int
                ? descriptor.defaultValue
                : VansSerializedValue::Float(0.0);
            return true;
        }
        return false;
    case PythonScriptInspectableFieldKind::String:
        if (value.kind != VansSerializedValue::Kind::String)
        {
            value = descriptor.defaultValue.kind == VansSerializedValue::Kind::String
                ? descriptor.defaultValue
                : VansSerializedValue::String("");
            return true;
        }
        return false;
    case PythonScriptInspectableFieldKind::ProjectAssetReference:
    {
        SerializedObjectReferenceValue expected;
        expected.domain = "ProjectAsset";
        expected.assetType = descriptor.assetType;
        return NormalizeSerializedObjectReference(value, expected);
    }
    case PythonScriptInspectableFieldKind::SceneEntityReference:
    {
        SerializedObjectReferenceValue expected;
        expected.domain = "SceneEntity";
        return NormalizeSerializedObjectReference(value, expected);
    }
    case PythonScriptInspectableFieldKind::SceneComponentReference:
    {
        SerializedObjectReferenceValue expected;
        expected.domain = "SceneComponent";
        expected.componentType = descriptor.componentType;
        return NormalizeSerializedObjectReference(value, expected);
    }
    default:
        return false;
    }
}

PythonScriptFieldDefaultsResult VansPythonScriptInspectorService::BuildDefaultFieldData(
    const std::filesystem::path& projectRoot,
    const std::string& scriptPath,
    const std::string& className)
{
    if (scriptPath.empty() || className.empty())
        return { false, "Script path or class is empty" };
    if (!Py_IsInitialized())
        return { false, "Python runtime is not initialized" };

    VansScriptContext* context = VansScriptContext::GetInstance();
    if (!context)
        return { false, "Script context is not available" };

    try
    {
        context->ConfigureProjectPythonPaths(projectRoot.string());
        py::object scriptClass;
        std::string moduleName;
        const std::filesystem::path absPath = projectRoot / scriptPath;
        if (!context->ResolveScriptClass(scriptPath, className, absPath, moduleName, scriptClass))
            return { false, "Cannot resolve script class" };

        std::vector<PythonScriptFieldDescriptor> descriptors;

        if (py::hasattr(scriptClass, "__annotations__"))
        {
            py::dict annotations = py::reinterpret_borrow<py::dict>(
                scriptClass.attr("__annotations__"));
            for (const auto& item : annotations)
            {
                const std::string name = py::str(item.first).cast<std::string>();
                if (name.empty() || name.front() == '_')
                    continue;
                UpsertDescriptor(descriptors, DescriptorForAnnotation(name, item.second));
            }
        }

        if (py::hasattr(scriptClass, "__dict__"))
        {
            py::dict members = py::reinterpret_borrow<py::dict>(
                scriptClass.attr("__dict__"));
            for (const auto& item : members)
            {
                const std::string name = py::str(item.first).cast<std::string>();
                if (name.empty() || name.front() == '_' ||
                    name == "__vans_inspector__" ||
                    name == "__vans_fields__")
                {
                    continue;
                }
                if (PyCallable_Check(item.second.ptr()))
                    continue;
                UpsertDescriptor(descriptors, DescriptorForExplicitField(name, item.second));
            }
        }

        if (py::hasattr(scriptClass, "__vans_inspector__"))
            UpsertExplicitFieldDictionary(scriptClass.attr("__vans_inspector__"), descriptors);

        if (py::hasattr(scriptClass, "__vans_fields__"))
            UpsertExplicitFieldDictionary(scriptClass.attr("__vans_fields__"), descriptors);

        SerializedFieldMap fields = BuildDefaultFields(descriptors);
        return { true, {}, std::move(fields), std::move(descriptors) };
    }
    catch (const py::error_already_set& error)
    {
        return { false, error.what() };
    }
    catch (const std::exception& error)
    {
        return { false, error.what() };
    }
}
}
