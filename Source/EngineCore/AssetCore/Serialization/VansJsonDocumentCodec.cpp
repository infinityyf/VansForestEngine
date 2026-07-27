#include "VansJsonDocumentCodec.h"

#include <nlohmann/json.hpp>

#include <exception>

namespace Vans
{
namespace
{
template<typename JsonT>
bool EmitJson(const JsonT& root, std::string& bytes, std::string& error)
{
    bytes.clear();
    error.clear();
    try
    {
        bytes = root.dump(4);
        bytes.push_back('\n');
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

template<typename JsonT>
bool ParseJson(const std::string& bytes, JsonT& root, std::string& error)
{
    root = JsonT();
    error.clear();
    try
    {
        root = JsonT::parse(bytes);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}
}

bool VansJsonDocumentCodec::Emit(const OrderedJson& root, std::string& bytes, std::string& error)
{
    return EmitJson(root, bytes, error);
}

bool VansJsonDocumentCodec::Emit(const BasicJson& root, std::string& bytes, std::string& error)
{
    return EmitJson(root, bytes, error);
}

bool VansJsonDocumentCodec::Parse(const std::string& bytes, OrderedJson& root, std::string& error)
{
    return ParseJson(bytes, root, error);
}

bool VansJsonDocumentCodec::Parse(const std::string& bytes, BasicJson& root, std::string& error)
{
    return ParseJson(bytes, root, error);
}
}
