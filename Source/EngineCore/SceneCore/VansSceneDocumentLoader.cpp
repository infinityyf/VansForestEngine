#include "VansSceneDocumentLoader.h"
#include "VansSceneSchemaV2.h"

#include <fstream>
#include <iterator>

namespace Vans
{
namespace
{
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

bool ReadFile(const std::filesystem::path& path, std::string& bytes, std::string& error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        error = "Cannot open scene document: " + path.string();
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad())
    {
        error = "Failed while reading scene document: " + path.string();
        return false;
    }
    return true;
}
}

SceneDocumentLoadResult VansSceneDocumentLoader::Load(const std::filesystem::path& path)
{
    SceneDocumentLoadResult result;
    std::string bytes;
    std::string error;
    if (!ReadFile(path, bytes, error))
    {
        result.diagnostics.push_back({ SceneDiagnosticSeverity::Error, "", error });
        return result;
    }

    try
    {
        auto document = std::make_unique<VansSceneDocument>();
        document->m_Root = SceneJson::parse(bytes);
        document->m_SourcePath = std::filesystem::absolute(path).lexically_normal();
        document->m_LoadedFingerprint = Fingerprint(document->m_SourcePath, &error);

        document->m_Diagnostics = VansSceneSchemaV2::Validate(document->m_Root);

        if (!document->m_LoadedFingerprint.valid)
            document->m_Diagnostics.push_back({ SceneDiagnosticSeverity::Error, "", error });

        result.diagnostics = document->m_Diagnostics;
        result.document = std::move(document);
    }
    catch (const SceneJson::parse_error& parseError)
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
    if (!ReadFile(path, bytes, localError))
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
