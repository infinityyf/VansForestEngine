#include "VansJsonFileStorage.h"

#include "VansFileStorage.h"
#include "../Serialization/VansJsonDocumentCodec.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace Vans
{
namespace
{
template<typename JsonT>
bool ReadJsonFile(const std::filesystem::path& path, JsonT& root, std::string& error)
{
    root = JsonT();
    error.clear();

    std::string bytes;
    if (!VansFileStorage::ReadAllBytes(path, bytes, error))
        return false;

    return VansJsonDocumentCodec::Parse(bytes, root, error);
}

template<typename JsonT>
bool StageWriteJsonFile(
    const std::filesystem::path& path,
    const JsonT& root,
    VansStagedFile& stage,
    std::string& error)
{
    stage = {};
    error.clear();

    std::string bytes;
    if (!VansJsonDocumentCodec::Emit(root, bytes, error))
        return false;

    JsonT verification;
    if (!VansJsonDocumentCodec::Parse(bytes, verification, error))
        return false;
    if (verification != root)
    {
        error = "Emitted JSON verification failed";
        return false;
    }

    return VansFileStorage::StageWriteBytes(path, bytes, stage, error);
}
}

bool VansJsonFileStorage::Read(const std::filesystem::path& path, OrderedJson& root, std::string& error)
{
    return ReadJsonFile(path, root, error);
}

bool VansJsonFileStorage::Read(const std::filesystem::path& path, BasicJson& root, std::string& error)
{
    return ReadJsonFile(path, root, error);
}

bool VansJsonFileStorage::StageWrite(
    const std::filesystem::path& path,
    const OrderedJson& root,
    VansStagedFile& stage,
    std::string& error)
{
    return StageWriteJsonFile(path, root, stage, error);
}

bool VansJsonFileStorage::StageWrite(
    const std::filesystem::path& path,
    const BasicJson& root,
    VansStagedFile& stage,
    std::string& error)
{
    return StageWriteJsonFile(path, root, stage, error);
}

bool VansJsonFileStorage::WriteAtomic(const std::filesystem::path& path, const OrderedJson& root, std::string& error)
{
    VansStagedFile stage;
    if (!StageWrite(path, root, stage, error))
        return false;

    VansStagedFileTransaction transaction;
    transaction.Add(std::move(stage));
    return transaction.Publish(error);
}

bool VansJsonFileStorage::WriteAtomic(const std::filesystem::path& path, const BasicJson& root, std::string& error)
{
    VansStagedFile stage;
    if (!StageWrite(path, root, stage, error))
        return false;

    VansStagedFileTransaction transaction;
    transaction.Add(std::move(stage));
    return transaction.Publish(error);
}
}
