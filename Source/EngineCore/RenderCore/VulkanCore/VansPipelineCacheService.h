#pragma once

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "vulkan/vulkan.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	enum class VansPipelineCacheFlushReason
	{
		Periodic,
		Shutdown,
		Manual
	};

	enum class VansPipelineCachePipelineKind
	{
		Graphics,
		Compute,
		RayTracing
	};

	// Owns the process-local Vulkan pipeline cache for one VkDevice and persists
	// its opaque driver payload between runs. Pipeline objects borrow the handle;
	// they never own or destroy it.
	class VansPipelineCacheService
	{
	public:
		class ScopedAccess
		{
		public:
			ScopedAccess() = default;
			ScopedAccess(ScopedAccess&&) noexcept = default;
			ScopedAccess& operator=(ScopedAccess&&) noexcept = default;
			ScopedAccess(const ScopedAccess&) = delete;
			ScopedAccess& operator=(const ScopedAccess&) = delete;

			VkPipelineCache GetHandle() const;
			void NotifyPipelineCreated(VansPipelineCachePipelineKind kind);

		private:
			friend class VansPipelineCacheService;
			explicit ScopedAccess(VansPipelineCacheService* owner);

			VansPipelineCacheService* m_Owner = nullptr;
			std::unique_lock<std::mutex> m_Lock;
		};

		VansPipelineCacheService() = default;
		~VansPipelineCacheService();
		VansPipelineCacheService(const VansPipelineCacheService&) = delete;
		VansPipelineCacheService& operator=(const VansPipelineCacheService&) = delete;

		bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device);
		void Shutdown();

		ScopedAccess Acquire();
		static ScopedAccess AcquireForDevice(VkDevice device);

		// External renderers that create pipelines internally must use an isolated
		// child cache. Child caches are merged into the main cache at shutdown.
		VkPipelineCache GetOrCreateChildCache(const std::string& name);

		void TickPersistence();
		bool Flush(VansPipelineCacheFlushReason reason);
		void RefreshPersistencePath();

		bool IsInitialized() const { return m_Initialized; }
		const std::filesystem::path& GetCacheFilePath() const { return m_CacheFilePath; }
		uint64_t GetPipelineCreateCount() const { return m_TotalPipelineCreates; }

	private:
		struct CacheIdentity
		{
			uint32_t vendorId = 0;
			uint32_t deviceId = 0;
			uint32_t driverVersion = 0;
			uint8_t pipelineCacheUUID[VK_UUID_SIZE] = {};
		};

		bool CreateCacheLocked(const std::vector<uint8_t>& initialData, VkPipelineCache& outCache);
		bool ReadCacheDataLocked(VkPipelineCache cache, std::vector<uint8_t>& outData) const;
		bool ReadCacheFile(std::vector<uint8_t>& outPayload, bool logFailures) const;
		bool ValidateVulkanPayload(const std::vector<uint8_t>& payload) const;
		bool MergeDiskCacheLocked();
		bool MergeChildCachesIntoMainLocked(bool destroyChildren);
		void MergeAndDestroyChildCachesLocked();
		bool FlushLocked(VansPipelineCacheFlushReason reason);
		void MarkPipelineCreatedLocked(VansPipelineCachePipelineKind kind);
		void QuarantineInvalidCacheFile() const;

		static std::filesystem::path ResolveCacheFilePath(const CacheIdentity& identity);
		static uint64_t HashBytes(const uint8_t* data, size_t size);
		static const char* FlushReasonName(VansPipelineCacheFlushReason reason);
		static void RegisterDevice(VkDevice device, VansPipelineCacheService* service);
		static void UnregisterDevice(VkDevice device, VansPipelineCacheService* service);

		mutable std::mutex m_Mutex;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineCache m_Cache = VK_NULL_HANDLE;
		std::unordered_map<std::string, VkPipelineCache> m_ChildCaches;
		CacheIdentity m_Identity{};
		std::filesystem::path m_CacheFilePath;
		std::chrono::steady_clock::time_point m_LastMutation{};
		uint64_t m_TotalPipelineCreates = 0;
		uint64_t m_GraphicsPipelineCreates = 0;
		uint64_t m_ComputePipelineCreates = 0;
		uint64_t m_RayTracingPipelineCreates = 0;
		uint64_t m_CreatesSinceFlush = 0;
		uint64_t m_LastDiskPayloadHash = 0;
		bool m_Dirty = false;
		bool m_WriteEnabled = true;
		bool m_Initialized = false;
	};
}
