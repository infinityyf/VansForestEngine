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
    bool m_Finalized = false;

    void Rollback(std::string& error);
};
}
