#pragma once

#include <string>
#include <vector>

namespace Vans
{
enum class SceneDiagnosticSeverity
{
    Warning,
    Error
};

struct SceneDiagnostic
{
    SceneDiagnosticSeverity severity = SceneDiagnosticSeverity::Error;
    std::string jsonPointer;
    std::string message;
};

using SceneDiagnostics = std::vector<SceneDiagnostic>;
}
