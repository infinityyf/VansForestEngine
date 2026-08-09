#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace VansGraphics
{
struct VansVideoThumbnail
{
	int width = 0;
	int height = 0;
	double durationSeconds = 0.0;
	std::vector<std::uint8_t> rgba;
};

class VansVideoThumbnailBuilder
{
public:
	static bool Build(const std::filesystem::path& sourcePath, int maximumWidth, int maximumHeight,
		VansVideoThumbnail& thumbnail, std::string& error);
};
}
