#include "VansSceneDocumentLoader.h"
#include "VansSceneSchema.h"

#include "../AssetCore/Serialization/VansJsonDocumentCodec.h"
#include "../AssetCore/Serialization/VansSerializedValueLegacyJsonAdapter.h"
#include "../AssetCore/Storage/VansFileStorage.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwctype>

namespace Vans
{
namespace
{
std::wstring LowerExtension(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });
    return extension;
}

bool HasSceneDocumentExtension(const std::filesystem::path& path)
{
    const std::wstring extension = LowerExtension(path);
    return extension == L".json" ||
        extension == L".scene" ||
        extension == L".vscene";
}

std::uint64_t HashBytes(const std::string& bytes)
{
    constexpr std::uint64_t offset = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t result = offset;
    for (const unsigned char value : bytes)
    {
        result ^= value;
        result *= prime;
    }
    return result;
}

}

bool VansSceneDocumentLoader::IsSceneDocumentFile(const std::filesystem::path& path, std::string* error)
{
    if (!HasSceneDocumentExtension(path))
        return false;

    std::string bytes;
    std::string localError;
    if (!VansFileStorage::ReadAllBytes(path, bytes, localError))
    {
        if (error) *error = localError;
        return false;
    }

    SceneJson parsedRoot;
    if (!VansJsonDocumentCodec::Parse(bytes, parsedRoot, localError))
    {
        if (error) *error = localError;
        return false;
    }

    const SceneDiagnostics diagnostics = VansSceneSchema::ValidateLegacyJson(parsedRoot);
    const bool hasSchemaError = std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const SceneDiagnostic& diagnostic)
        {
            return diagnostic.severity == SceneDiagnosticSeverity::Error;
        });
    if (hasSchemaError)
    {
        if (error) *error = "File is not a valid Forest scene document";
        return false;
    }
    return true;
}

SceneDocumentLoadResult VansSceneDocumentLoader::Load(const std::filesystem::path& path)
{
    SceneDocumentLoadResult result;
    std::string bytes;
    std::string error;
    if (!VansFileStorage::ReadAllBytes(path, bytes, error))
    {
        result.diagnostics.push_back({ SceneDiagnosticSeverity::Error, "", error });
        return result;
    }

    try
    {
        auto document = std::make_unique<VansSceneDocument>();
        SceneJson parsedRoot;
        if (!VansJsonDocumentCodec::Parse(bytes, parsedRoot, error))
        {
            result.diagnostics.push_back({ SceneDiagnosticSeverity::Error, "", error });
            return result;
        }
        document->m_Diagnostics = VansSceneSchema::ValidateLegacyJson(parsedRoot);
        document->m_Root = std::make_unique<VansSerializedValue>(
            DecodeSerializedValueLegacyJson(parsedRoot));
        document->m_SourcePath = std::filesystem::absolute(path).lexically_normal();
        document->m_LoadedFingerprint = Fingerprint(document->m_SourcePath, &error);

        if (!document->m_LoadedFingerprint.valid)
            document->m_Diagnostics.push_back({ SceneDiagnosticSeverity::Error, "", error });

        result.diagnostics = document->m_Diagnostics;
        result.document = std::move(document);
    }
    catch (const std::exception& parseError)
    {
        result.diagnostics.push_back({ SceneDiagnosticSeverity::Error, "", parseError.what() });
    }
    return result;
}

SceneFileFingerprint VansSceneDocumentLoader::Fingerprint(const std::filesystem::path& path, std::string* error)
{
    SceneFileFingerprint result;
    std::string bytes;
    std::string localError;
    std::error_code ec;
    if (!VansFileStorage::ReadAllBytes(path, bytes, localError))
    {
        if (error) *error = localError;
        return result;
    }
    result.size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        if (error) *error = "Cannot query scene file size: " + ec.message();
        return {};
    }
    result.writeTime = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        if (error) *error = "Cannot query scene write time: " + ec.message();
        return {};
    }
    result.contentHash = HashBytes(bytes);
    result.valid = true;
    return result;
}
}
