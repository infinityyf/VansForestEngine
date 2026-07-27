#pragma once

#include "VansStagedFileTransaction.h"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace Vans
{
class VansJsonFileStorage
{
public:
    using OrderedJson = nlohmann::ordered_json;
    using BasicJson = nlohmann::json;

    static bool Read(const std::filesystem::path& path, OrderedJson& root, std::string& error);
    static bool Read(const std::filesystem::path& path, BasicJson& root, std::string& error);
    static bool StageWrite(const std::filesystem::path& path, const OrderedJson& root, VansStagedFile& stage, std::string& error);
    static bool StageWrite(const std::filesystem::path& path, const BasicJson& root, VansStagedFile& stage, std::string& error);
    static bool WriteAtomic(const std::filesystem::path& path, const OrderedJson& root, std::string& error);
    static bool WriteAtomic(const std::filesystem::path& path, const BasicJson& root, std::string& error);
};
}
