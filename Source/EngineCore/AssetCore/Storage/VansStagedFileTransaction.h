#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
struct VansStagedFile
{
    std::filesystem::path targetPath;
    std::filesystem::path temporaryPath;
    bool requireAbsent = false;
};

class VansStagedFileTransaction
{
public:
    VansStagedFileTransaction() = default;
    ~VansStagedFileTransaction();

    VansStagedFileTransaction(const VansStagedFileTransaction&) = delete;
    VansStagedFileTransaction& operator=(const VansStagedFileTransaction&) = delete;

    void Add(VansStagedFile file);
    bool Empty() const { return m_Files.empty(); }
    bool Publish(std::string& error);
    bool PreparePublish(std::string& error);
    void Commit();
    void Cleanup();

private:
    struct FileState
    {
        VansStagedFile file;
        std::filesystem::path backupPath;
        bool targetExisted = false;
        bool backupCreated = false;
        bool published = false;
    };

    std::vector<FileState> m_Files;
    enum class Phase { Staging, Prepared, Committed, RolledBack };
    Phase m_Phase = Phase::Staging;

    void Rollback(std::string& error);
};
}
