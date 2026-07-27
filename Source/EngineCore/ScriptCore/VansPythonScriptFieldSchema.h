#pragma once

#include <string>

namespace Vans
{
std::string NormalizePythonInspectorIdentifier(std::string value);
std::string CanonicalPythonInspectorAssetTypeName(const std::string& text);
std::string CanonicalPythonInspectorComponentTypeName(const std::string& text);
bool IsPythonSceneEntityReferenceAnnotation(const std::string& text);
bool IsPythonSceneComponentReferenceAnnotation(const std::string& text);
bool IsPythonProjectAssetReferenceAnnotation(const std::string& text);
}
