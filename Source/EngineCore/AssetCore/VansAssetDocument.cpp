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
void VansAssetDocument::Reset()
{
    m_Path.clear();
    m_Root = Json();
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
#ifdef _WIN32
        if (MoveFileExW(temporary.c_str(), m_Path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
            throw std::runtime_error("Cannot atomically replace asset document");
#else
        std::error_code ec;
        std::filesystem::rename(temporary, m_Path, ec);
        if (ec) throw std::runtime_error(ec.message());
#endif
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
