#include "VansAssetDocument.h"

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace Vans
{
VansAssetFileFingerprint VansAssetDocument::Fingerprint(const std::filesystem::path& path, std::string& error)
{
    VansAssetFileFingerprint fingerprint;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return fingerprint;
    if (ec)
    {
        error = ec.message();
        return fingerprint;
    }

    fingerprint.exists = true;
    fingerprint.size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        error = ec.message();
        return {};
    }
    fingerprint.lastWriteTime = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        error = ec.message();
        return {};
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        error = "Cannot open file for fingerprint: " + path.string();
        return {};
    }

    std::uint64_t hash = 14695981039346656037ull;
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0)
    {
        const std::streamsize count = input.gcount();
        for (std::streamsize index = 0; index < count; ++index)
        {
            hash ^= static_cast<unsigned char>(buffer[index]);
            hash *= 1099511628211ull;
        }
    }
    fingerprint.contentHash = hash;
    return fingerprint;
}

void VansAssetDocument::Reset()
{
    m_Path.clear();
    m_Root = Json();
    m_LoadedFingerprint = {};
    m_Loaded = false;
    m_Dirty = false;
}

bool VansAssetDocument::Load(const std::filesystem::path& path, std::string& error)
{
    Reset();
    try
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        m_Root = Json::parse(input);
        if (!m_Root.is_object())
        {
            error = "Asset document root must be an object: " + path.string();
            return false;
        }
        m_Path = std::filesystem::absolute(path).lexically_normal();
        m_LoadedFingerprint = Fingerprint(m_Path, error);
        if (!m_LoadedFingerprint.exists)
            return false;
        m_Loaded = true;
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

bool VansAssetDocument::Save(std::string& error)
{
    if (!m_Loaded || !m_Dirty) return true;
    const VansAssetFileFingerprint currentFingerprint = Fingerprint(m_Path, error);
    if (!error.empty())
        return false;
    if (currentFingerprint != m_LoadedFingerprint)
    {
        error = "Asset document changed on disk: " + m_Path.string();
        return false;
    }

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path temporary = m_Path.parent_path() /
        (m_Path.filename().string() + ".tmp." + std::to_string(nonce));
    try
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot create temporary asset document");
        output << m_Root.dump(4) << '\n';
        output.flush();
        if (!output) throw std::runtime_error("Cannot write temporary asset document");
        output.close();

        std::ifstream verificationInput(temporary, std::ios::binary);
        const Json verification = Json::parse(verificationInput);
        if (!verificationInput || verification != m_Root)
            throw std::runtime_error("Asset document verification failed");
        // std::ifstream does not share FILE_SHARE_DELETE on Windows. Keeping
        // this handle open makes MoveFileExW fail while trying to rename the
        // very temporary file we just verified.
        verificationInput.close();
#ifdef _WIN32
        DWORD win32Error = ERROR_SUCCESS;
        bool published = false;
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            if (MoveFileExW(temporary.c_str(), m_Path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE)
            {
                published = true;
                break;
            }
            win32Error = GetLastError();
            if (win32Error != ERROR_SHARING_VIOLATION && win32Error != ERROR_ACCESS_DENIED)
                break;
            Sleep(static_cast<DWORD>(10 * (attempt + 1)));
        }
        if (!published)
        {
            throw std::runtime_error("Cannot atomically replace asset document (Win32 error " +
                std::to_string(static_cast<unsigned long>(win32Error)) + ")");
        }
#else
        std::error_code ec;
        std::filesystem::rename(temporary, m_Path, ec);
        if (ec) throw std::runtime_error(ec.message());
#endif
        m_LoadedFingerprint = Fingerprint(m_Path, error);
        if (!error.empty())
            return false;
        m_Dirty = false;
        return true;
    }
    catch (const std::exception& exception)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = exception.what();
        return false;
    }
}
}
