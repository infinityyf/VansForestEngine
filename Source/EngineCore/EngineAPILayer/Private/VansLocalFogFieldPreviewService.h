#pragma once

#include "../Public/EngineDTOs.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Vans::EditorAPI
{
	// 供契约测试与服务共享的纯 CPU 采样入口；输入始终是线性 RGBA8。
	LocalFogFieldPreviewSnapshot BuildLocalFogFieldPreviewFromRgba8(
		const std::uint8_t* rgbaPixels,
		std::size_t rgbaByteCount,
		std::uint32_t sourceWidth,
		std::uint32_t sourceHeight,
		const LocalFogFieldPreviewRequest& request);

	class VansLocalFogFieldPreviewService final
	{
	public:
		LocalFogFieldPreviewSnapshot Build(
			const LocalFogFieldPreviewRequest& request,
			const std::string& sourcePath,
			std::uint64_t sourceHash,
			std::uint64_t metaHash,
			std::uint64_t generation) const;
		void Clear();

	private:
		struct CacheKey final
		{
			std::string assetGuid;
			std::string channels;
			std::string sourcePath;
			std::uint64_t sourceHash = 0;
			std::uint64_t metaHash = 0;
			std::uint64_t generation = 0;
			LocalFogFieldPreviewKind kind = LocalFogFieldPreviewKind::Scalar;
			std::uint32_t sampleColumns = 0;
			std::uint32_t sampleRows = 0;

			bool operator<(const CacheKey& other) const;
		};

		mutable std::mutex m_CacheMutex;
		mutable std::map<CacheKey, LocalFogFieldPreviewSnapshot> m_Cache;
	};
}
