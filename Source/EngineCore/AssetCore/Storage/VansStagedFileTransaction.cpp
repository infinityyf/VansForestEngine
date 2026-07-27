#include "VansStagedFileTransaction.h"

#include <chrono>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace Vans
{
namespace
{
std::filesystem::path MakeBackupPath(const std::filesystem::path& target)
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return target.parent_path() / (target.filename().string() + ".bak." + std::to_string(nonce));
}

bool MovePath(const std::filesystem::path& from, const std::filesystem::path& to, bool replace, std::string& error)
{
#ifdef _WIN32
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (replace)
        flags |= MOVEFILE_REPLACE_EXISTING;
    if (MoveFileExW(from.c_str(), to.c_str(), flags) != FALSE)
        return true;
    error = "Move failed from '" + from.string() + "' to '" + to.string() +
        "' (Win32 error " + std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
    return false;
#else
    std::error_code ec;
    if (!replace && std::filesystem::exists(to, ec))
    {
        error = "Move target already exists: " + to.string();
        return false;
    }
    if (replace)
        std::filesystem::remove(to, ec);
    std::filesystem::rename(from, to, ec);
    if (!ec)
        return true;
    error = ec.message();
    return false;
#endif
}

void RemoveQuietly(const std::filesystem::path& path)
{
    if (path.empty())
        return;
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}
}

VansStagedFileTransaction::~VansStagedFileTransaction()
{
    Cleanup();
}

void VansStagedFileTransaction::Add(VansStagedFile file)
{
    FileState state;
    state.file = std::move(file);
    m_Files.push_back(std::move(state));
}

bool VansStagedFileTransaction::Publish(std::string& error)
{
    if (m_Finalized)
        return true;

    for (FileState& state : m_Files)
    {
        if (state.file.targetPath.empty() || state.file.temporaryPath.empty())
        {
            error = "Invalid staged file path";
            Rollback(error);
            return false;
        }

        std::error_code ec;
        state.targetExisted = std::filesystem::exists(state.file.targetPath, ec);
        if (ec)
        {
            error = ec.message();
            Rollback(error);
            return false;
        }

        if (state.targetExisted)
        {
            state.backupPath = MakeBackupPath(state.file.targetPath);
            if (!MovePath(state.file.targetPath, state.backupPath, false, error))
            {
                Rollback(error);
                return false;
            }
            state.backupCreated = true;
        }

        if (!MovePath(state.file.temporaryPath, state.file.targetPath, false, error))
        {
            Rollback(error);
            return false;
        }
        state.published = true;
    }

    for (FileState& state : m_Files)
    {
        RemoveQuietly(state.backupPath);
        state.backupCreated = false;
    }
    m_Finalized = true;
    return true;
}

void VansStagedFileTransaction::Cleanup()
{
    if (m_Finalized)
        return;

    for (FileState& state : m_Files)
    {
        if (!state.published)
            RemoveQuietly(state.file.temporaryPath);
        if (state.backupCreated && !state.published)
            RemoveQuietly(state.backupPath);
    }
    m_Finalized = true;
}

void VansStagedFileTransaction::Rollback(std::string& error)
{
    std::string rollbackError;
    for (auto it = m_Files.rbegin(); it != m_Files.rend(); ++it)
    {
        FileState& state = *it;
        if (state.published)
            RemoveQuietly(state.file.targetPath);

        if (state.backupCreated)
        {
            std::string restoreError;
            if (!MovePath(state.backupPath, state.file.targetPath, false, restoreError) &&
                rollbackError.empty())
            {
                rollbackError = restoreError;
            }
            state.backupCreated = false;
        }

        if (!state.published)
            RemoveQuietly(state.file.temporaryPath);
    }

    if (!rollbackError.empty())
        error += " Rollback failed: " + rollbackError;
    m_Finalized = true;
}
}
