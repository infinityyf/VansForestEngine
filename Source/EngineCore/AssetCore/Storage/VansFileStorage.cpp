#include "VansFileStorage.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Vans
{
namespace
{
thread_local std::vector<VansIOContext> g_IOContextStack;
std::mutex g_IOAuditMutex;
std::vector<VansIOEvent> g_IOEvents;

std::filesystem::path MakeTemporaryPath(const std::filesystem::path& target)
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return target.parent_path() / (target.filename().string() + ".tmp." + std::to_string(nonce));
}

bool FlushFile(const std::filesystem::path& path)
{
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    const bool flushed = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    return flushed;
#else
    (void)path;
    return true;
#endif
}

std::string ReadAllBytesUnchecked(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read file");
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}
}

void VansIOAudit::Reset()
{
    std::lock_guard<std::mutex> lock(g_IOAuditMutex);
    g_IOEvents.clear();
}

std::vector<VansIOEvent> VansIOAudit::Snapshot()
{
    std::lock_guard<std::mutex> lock(g_IOAuditMutex);
    return g_IOEvents;
}

VansIOContext VansIOAudit::CurrentContext()
{
    return g_IOContextStack.empty() ? VansIOContext{} : g_IOContextStack.back();
}

void VansIOAudit::PushContext(VansIOContext context)
{
    g_IOContextStack.push_back(std::move(context));
}

void VansIOAudit::PopContext()
{
    if (!g_IOContextStack.empty())
        g_IOContextStack.pop_back();
}

void VansIOAudit::Record(
    VansIOOperation operation,
    const std::filesystem::path& path,
    bool success)
{
    const VansIOContext context = CurrentContext();
    VansIOEvent event;
    event.domain = context.domain;
    event.operation = operation;
    event.path = path;
    event.callerTag = context.callerTag;
    event.success = success;
    std::lock_guard<std::mutex> lock(g_IOAuditMutex);
    g_IOEvents.push_back(std::move(event));
}

VansScopedIOContext::VansScopedIOContext(
    VansIODomain domain,
    std::string callerTag,
    bool allowAuthoringWrite)
{
    VansIOAudit::PushContext({ domain, std::move(callerTag), allowAuthoringWrite });
}

VansScopedIOContext::~VansScopedIOContext()
{
    VansIOAudit::PopContext();
}

bool VansFileStorage::ReadAllBytes(
    const std::filesystem::path& path,
    std::string& bytes,
    std::string& error)
{
    bytes.clear();
    error.clear();
    try
    {
        bytes = ReadAllBytesUnchecked(path);
        VansIOAudit::Record(VansIOOperation::Read, path, true);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        VansIOAudit::Record(VansIOOperation::Read, path, false);
        return false;
    }
}

bool VansFileStorage::ReadByteRange(
    const std::filesystem::path& path,
    std::uint64_t offset,
    std::uint64_t size,
    std::string& bytes,
    std::string& error)
{
    bytes.clear();
    error.clear();

    if (size > static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)()))
    {
        error = "Requested byte range is too large";
        VansIOAudit::Record(VansIOOperation::ReadRange, path, false);
        return false;
    }
    if (offset > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)()))
    {
        error = "Requested byte range offset is too large";
        VansIOAudit::Record(VansIOOperation::ReadRange, path, false);
        return false;
    }

    std::error_code ec;
    const std::uint64_t fileSize = static_cast<std::uint64_t>(std::filesystem::file_size(path, ec));
    if (ec)
    {
        error = ec.message();
        VansIOAudit::Record(VansIOOperation::ReadRange, path, false);
        return false;
    }
    if (offset > fileSize || size > fileSize - offset)
    {
        error = "Requested byte range is outside the file";
        VansIOAudit::Record(VansIOOperation::ReadRange, path, false);
        return false;
    }

    if (size == 0)
    {
        VansIOAudit::Record(VansIOOperation::ReadRange, path, true);
        return true;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        error = "Cannot read file";
        VansIOAudit::Record(VansIOOperation::ReadRange, path, false);
        return false;
    }
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input)
    {
        error = "Cannot seek file";
        VansIOAudit::Record(VansIOOperation::ReadRange, path, false);
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input)
    {
        error = "Cannot read file range";
        bytes.clear();
        VansIOAudit::Record(VansIOOperation::ReadRange, path, false);
        return false;
    }
    VansIOAudit::Record(VansIOOperation::ReadRange, path, true);
    return true;
}

bool VansFileStorage::StageWriteBytes(
    const std::filesystem::path& path,
    const std::string& bytes,
    VansStagedFile& stage,
    std::string& error)
{
    stage = {};
    error.clear();

    const VansIOContext context = VansIOAudit::CurrentContext();
    if (context.domain == VansIODomain::Authoring && !context.allowAuthoringWrite)
    {
        error = "Authoring write is not allowed in I/O context '" +
            context.callerTag + "'";
        VansIOAudit::Record(VansIOOperation::StageWrite, path, false);
        return false;
    }

    std::error_code ec;
    const std::filesystem::path parentPath = path.parent_path();
    if (!parentPath.empty())
        std::filesystem::create_directories(parentPath, ec);
    if (ec)
    {
        error = ec.message();
        VansIOAudit::Record(VansIOOperation::StageWrite, path, false);
        return false;
    }

    stage.targetPath = path;
    stage.temporaryPath = MakeTemporaryPath(path);
    try
    {
        std::ofstream output(stage.temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output)
            throw std::runtime_error("Cannot create temporary file");
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output)
            throw std::runtime_error("Cannot write temporary file");
        output.close();

        if (ReadAllBytesUnchecked(stage.temporaryPath) != bytes)
            throw std::runtime_error("Temporary file verification failed");
        if (!FlushFile(stage.temporaryPath))
            throw std::runtime_error("Failed flushing temporary file");
        VansIOAudit::Record(VansIOOperation::StageWrite, path, true);
        return true;
    }
    catch (const std::exception& exception)
    {
        std::error_code ignored;
        std::filesystem::remove(stage.temporaryPath, ignored);
        stage = {};
        error = exception.what();
        VansIOAudit::Record(VansIOOperation::StageWrite, path, false);
        return false;
    }
}

bool VansFileStorage::WriteAtomicBytes(
    const std::filesystem::path& path,
    const std::string& bytes,
    std::string& error)
{
    VansStagedFile stage;
    if (!StageWriteBytes(path, bytes, stage, error))
        return false;

    VansStagedFileTransaction transaction;
    transaction.Add(std::move(stage));
    return transaction.Publish(error);
}
}
