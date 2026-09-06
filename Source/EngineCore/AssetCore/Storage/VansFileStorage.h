#pragma once

#include "VansStagedFileTransaction.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
enum class VansIODomain
{
    Unclassified,
    Authoring,
    SourceResource,
    Derived,
    SessionDiagnostics,
    PackageOutput,
    UserPreference
};

enum class VansIOOperation
{
    Read,
    ReadRange,
    StageWrite
};

struct VansIOContext
{
    VansIODomain domain = VansIODomain::Unclassified;
    std::string callerTag;
    bool allowAuthoringWrite = false;
};

struct VansIOEvent
{
    VansIODomain domain = VansIODomain::Unclassified;
    VansIOOperation operation = VansIOOperation::Read;
    std::filesystem::path path;
    std::string callerTag;
    bool success = false;
};

class VansIOAudit
{
public:
    static void Reset();
    static std::vector<VansIOEvent> Snapshot();
    static VansIOContext CurrentContext();

private:
    friend class VansScopedIOContext;
    friend class VansFileStorage;
    static void PushContext(VansIOContext context);
    static void PopContext();
    static void Record(
        VansIOOperation operation,
        const std::filesystem::path& path,
        bool success);
};

class VansScopedIOContext
{
public:
    VansScopedIOContext(
        VansIODomain domain,
        std::string callerTag,
        bool allowAuthoringWrite = false);
    ~VansScopedIOContext();

    VansScopedIOContext(const VansScopedIOContext&) = delete;
    VansScopedIOContext& operator=(const VansScopedIOContext&) = delete;
};

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
