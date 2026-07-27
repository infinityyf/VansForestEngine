#pragma once

#include "VansStagedFileTransaction.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Vans
{
class VansFileStorage
{
public:
    static bool ReadAllBytes(
        const std::filesystem::path& path,
        std::string& bytes,
        std::string& error);
    static bool ReadByteRange(
        const std::filesystem::path& path,
        std::uint64_t offset,
        std::uint64_t size,
        std::string& bytes,
        std::string& error);
    static bool StageWriteBytes(
        const std::filesystem::path& path,
        const std::string& bytes,
        VansStagedFile& stage,
        std::string& error);
    static bool WriteAtomicBytes(
        const std::filesystem::path& path,
        const std::string& bytes,
        std::string& error);
};
}
