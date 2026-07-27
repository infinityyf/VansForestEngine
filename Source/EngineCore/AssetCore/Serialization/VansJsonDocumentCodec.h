#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace Vans
{
class VansJsonDocumentCodec
{
public:
    using OrderedJson = nlohmann::ordered_json;
    using BasicJson = nlohmann::json;

    static bool Emit(const OrderedJson& root, std::string& bytes, std::string& error);
    static bool Emit(const BasicJson& root, std::string& bytes, std::string& error);
    static bool Parse(const std::string& bytes, OrderedJson& root, std::string& error);
    static bool Parse(const std::string& bytes, BasicJson& root, std::string& error);
};
}
