#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace VansGraphics
{
    struct VansReflectionProbeCacheSurface
    {
        uint32_t face, mip, size;
        uint64_t offset, bytes;
    };

    struct VansReflectionProbeCacheData
    {
        uint32_t resolution = 0, mipCount = 0;
        // DDS 顺序：+X/-X/+Y/-Y/+Z/-Z，每面紧接自己的完整 mip 链。
        std::vector<uint8_t> texels;
    };

    // 标准 DDS DX10 / RGBA16F 单个完整 cubemap。没有引擎 cache.version 或旧格式分支。
    class VansReflectionProbeCache
    {
    public:
        static constexpr const char* FileName = "specular.dds";
        static bool Describe(uint32_t resolution, std::vector<VansReflectionProbeCacheSurface>& surfaces,
            uint64_t& byteCount, uint32_t& mipCount, std::string& error);
        static bool Decode(std::string_view bytes, VansReflectionProbeCacheData& result, std::string& error);
        static bool Encode(const VansReflectionProbeCacheData& data, std::string& bytes, std::string& error);
        static bool Load(const std::filesystem::path& file, VansReflectionProbeCacheData& result, std::string& error);
        static bool Save(const std::filesystem::path& file, const VansReflectionProbeCacheData& data, std::string& error);
    };
}
