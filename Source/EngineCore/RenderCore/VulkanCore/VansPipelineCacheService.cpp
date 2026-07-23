#include "VansPipelineCacheService.h"

#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace VansGraphics
{
	namespace
	{
		constexpr std::array<char, 8> kCacheMagic = { 'F', 'E', 'P', 'C', 'C', '0', '1', '\0' };
		constexpr uint32_t kCacheSchemaVersion = 1;
		constexpr uint32_t kPipelineAbiVersion = 1;
		constexpr uint64_t kMaxPayloadSize = 128ull * 1024ull * 1024ull;
		constexpr uint64_t kFlushCreateThreshold = 8;
		constexpr auto kFlushDebounce = std::chrono::seconds(2);

		struct CacheFileHeader
		{
			char magic[8] = {};
			uint32_t schemaVersion = 0;
			uint32_t headerSize = 0;
			uint64_t payloadSize = 0;
			uint64_t payloadHash = 0;
			uint32_t vendorId = 0;
			uint32_t deviceId = 0;
			uint32_t driverVersion = 0;
			uint32_t pipelineAbiVersion = 0;
			uint32_t buildConfiguration = 0;
			uint8_t pipelineCacheUUID[VK_UUID_SIZE] = {};
			uint8_t reserved[12] = {};
		};
		static_assert(sizeof(CacheFileHeader) == 80, "Pipeline cache file header layout changed");

		std::mutex g_ServiceRegistryMutex;
		std::unordered_map<VkDevice, VansPipelineCacheService*> g_ServiceRegistry;

		uint32_t CurrentBuildConfiguration()
		{
#if defined(_DEBUG)
			return 1;
#else
			return 2;
#endif
		}

		std::string SanitizePathComponent(std::string value)
		{
			for (char& c : value)
			{
				const unsigned char byte = static_cast<unsigned char>(c);
				if (!std::isalnum(byte) && c != '-' && c != '_')
					c = '_';
			}
			return value.empty() ? "Editor" : value;
		}

		std::string UUIDToString(const uint8_t* uuid)
		{
			std::ostringstream stream;
			stream << std::hex << std::setfill('0');
			for (uint32_t i = 0; i < VK_UUID_SIZE; ++i)
				stream << std::setw(2) << static_cast<uint32_t>(uuid[i]);
			return stream.str();
		}

#if defined(_WIN32)
		class ScopedFileLock
		{
		public:
			explicit ScopedFileLock(const std::filesystem::path& path)
			{
				m_Handle = CreateFileW(
					path.c_str(),
					GENERIC_READ | GENERIC_WRITE,
					0,
					nullptr,
					OPEN_ALWAYS,
					FILE_ATTRIBUTE_NORMAL,
					nullptr);
			}

			~ScopedFileLock()
			{
				if (m_Handle != INVALID_HANDLE_VALUE)
					CloseHandle(m_Handle);
			}

			bool IsLocked() const { return m_Handle != INVALID_HANDLE_VALUE; }

		private:
			HANDLE m_Handle = INVALID_HANDLE_VALUE;
		};
#endif
	}

	VansPipelineCacheService::ScopedAccess::ScopedAccess(VansPipelineCacheService* owner)
		: m_Owner(owner)
		, m_Lock(owner ? std::unique_lock<std::mutex>(owner->m_Mutex) : std::unique_lock<std::mutex>())
	{
	}

	VkPipelineCache VansPipelineCacheService::ScopedAccess::GetHandle() const
	{
		return m_Owner ? m_Owner->m_Cache : VK_NULL_HANDLE;
	}

	void VansPipelineCacheService::ScopedAccess::NotifyPipelineCreated(VansPipelineCachePipelineKind kind)
	{
		if (m_Owner)
			m_Owner->MarkPipelineCreatedLocked(kind);
	}

	VansPipelineCacheService::~VansPipelineCacheService()
	{
		Shutdown();
	}

	bool VansPipelineCacheService::Initialize(VkPhysicalDevice physicalDevice, VkDevice device)
	{
		if (m_Initialized)
			return true;
		if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
			return false;

		m_PhysicalDevice = physicalDevice;
		m_Device = device;

		VkPhysicalDeviceProperties properties{};
		VansGraphics::vkGetPhysicalDeviceProperties(physicalDevice, &properties);
		m_Identity.vendorId = properties.vendorID;
		m_Identity.deviceId = properties.deviceID;
		m_Identity.driverVersion = properties.driverVersion;
		std::memcpy(m_Identity.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE);
		m_CacheFilePath = ResolveCacheFilePath(m_Identity);

		std::error_code ec;
		std::filesystem::create_directories(m_CacheFilePath.parent_path(), ec);
		if (ec)
		{
			m_WriteEnabled = false;
			VANS_LOG_WARN("[PipelineCache] Cache directory is not writable: " << m_CacheFilePath.parent_path().string());
		}

		std::vector<uint8_t> initialData;
		const bool cacheFileExists = std::filesystem::is_regular_file(m_CacheFilePath, ec) && !ec;
		const bool loaded = cacheFileExists && ReadCacheFile(initialData, true);
		if (cacheFileExists && !loaded)
			QuarantineInvalidCacheFile();

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (!CreateCacheLocked(initialData, m_Cache))
			{
				if (!initialData.empty())
				{
					VANS_LOG_WARN("[PipelineCache] Driver rejected cached data; retrying with an empty cache");
					QuarantineInvalidCacheFile();
					initialData.clear();
				}
				if (!CreateCacheLocked(initialData, m_Cache))
				{
					VANS_LOG_ERROR("[PipelineCache] Failed to create Vulkan pipeline cache");
					m_Device = VK_NULL_HANDLE;
					m_PhysicalDevice = VK_NULL_HANDLE;
					return false;
				}
			}
			m_LastDiskPayloadHash = initialData.empty() ? 0 : HashBytes(initialData.data(), initialData.size());
			m_Initialized = true;
		}

		RegisterDevice(device, this);
		if (loaded)
		{
			VANS_LOG("[PipelineCache] Loaded " << initialData.size() << " bytes from " << m_CacheFilePath.string());
		}
		else
		{
			VANS_LOG("[PipelineCache] Starting with an empty cache: " << m_CacheFilePath.string());
		}
		return true;
	}

	void VansPipelineCacheService::Shutdown()
	{
		if (!m_Initialized)
			return;

		UnregisterDevice(m_Device, this);
		Flush(VansPipelineCacheFlushReason::Shutdown);
		VANS_LOG("[PipelineCache] Session creates: graphics=" << m_GraphicsPipelineCreates
			<< " compute=" << m_ComputePipelineCreates
			<< " rayTracing=" << m_RayTracingPipelineCreates
			<< " total=" << m_TotalPipelineCreates);

		std::lock_guard<std::mutex> lock(m_Mutex);
		MergeAndDestroyChildCachesLocked();
		if (m_Cache != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE)
		{
			VansGraphics::vkDestroyPipelineCache(m_Device, m_Cache, nullptr);
			m_Cache = VK_NULL_HANDLE;
		}
		m_Initialized = false;
		m_Device = VK_NULL_HANDLE;
		m_PhysicalDevice = VK_NULL_HANDLE;
	}

	VansPipelineCacheService::ScopedAccess VansPipelineCacheService::Acquire()
	{
		return ScopedAccess(m_Initialized ? this : nullptr);
	}

	VansPipelineCacheService::ScopedAccess VansPipelineCacheService::AcquireForDevice(VkDevice device)
	{
		VansPipelineCacheService* service = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_ServiceRegistryMutex);
			auto it = g_ServiceRegistry.find(device);
			if (it != g_ServiceRegistry.end())
				service = it->second;
		}
		return ScopedAccess(service);
	}

	VkPipelineCache VansPipelineCacheService::GetOrCreateChildCache(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (!m_Initialized || name.empty())
			return VK_NULL_HANDLE;

		auto existing = m_ChildCaches.find(name);
		if (existing != m_ChildCaches.end())
			return existing->second;

		std::vector<uint8_t> seedData;
		ReadCacheDataLocked(m_Cache, seedData);
		VkPipelineCache child = VK_NULL_HANDLE;
		if (!CreateCacheLocked(seedData, child))
		{
			seedData.clear();
			if (!CreateCacheLocked(seedData, child))
				return VK_NULL_HANDLE;
		}

		m_ChildCaches.emplace(name, child);
		VANS_LOG("[PipelineCache] Created isolated child cache '" << name << "'");
		return child;
	}

	void VansPipelineCacheService::TickPersistence()
	{
		bool shouldFlush = false;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_Initialized && m_Dirty)
			{
				const auto now = std::chrono::steady_clock::now();
				shouldFlush = m_CreatesSinceFlush >= kFlushCreateThreshold ||
					(now - m_LastMutation) >= kFlushDebounce;
			}
		}
		if (shouldFlush)
			Flush(VansPipelineCacheFlushReason::Periodic);
	}

	bool VansPipelineCacheService::Flush(VansPipelineCacheFlushReason reason)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return FlushLocked(reason);
	}

	bool VansPipelineCacheService::CreateCacheLocked(const std::vector<uint8_t>& initialData, VkPipelineCache& outCache)
	{
		VkPipelineCacheCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		createInfo.initialDataSize = initialData.size();
		createInfo.pInitialData = initialData.empty() ? nullptr : initialData.data();
		return VansGraphics::vkCreatePipelineCache(m_Device, &createInfo, nullptr, &outCache) == VK_SUCCESS;
	}

	bool VansPipelineCacheService::ReadCacheDataLocked(VkPipelineCache cache, std::vector<uint8_t>& outData) const
	{
		outData.clear();
		if (cache == VK_NULL_HANDLE || m_Device == VK_NULL_HANDLE)
			return false;

		for (uint32_t attempt = 0; attempt < 3; ++attempt)
		{
			size_t dataSize = 0;
			VkResult result = VansGraphics::vkGetPipelineCacheData(m_Device, cache, &dataSize, nullptr);
			if (result != VK_SUCCESS || dataSize == 0 || dataSize > kMaxPayloadSize)
				return false;

			outData.resize(dataSize);
			result = VansGraphics::vkGetPipelineCacheData(m_Device, cache, &dataSize, outData.data());
			if (result == VK_SUCCESS)
			{
				outData.resize(dataSize);
				return true;
			}
			if (result != VK_INCOMPLETE)
				break;
		}
		outData.clear();
		return false;
	}

	bool VansPipelineCacheService::ReadCacheFile(std::vector<uint8_t>& outPayload, bool logFailures) const
	{
		outPayload.clear();
		std::ifstream input(m_CacheFilePath, std::ios::binary | std::ios::ate);
		if (!input)
			return false;

		const std::streamsize totalSize = input.tellg();
		if (totalSize < static_cast<std::streamsize>(sizeof(CacheFileHeader)) ||
			totalSize > static_cast<std::streamsize>(sizeof(CacheFileHeader) + kMaxPayloadSize))
		{
			if (logFailures) VANS_LOG_WARN("[PipelineCache] Invalid cache file size");
			return false;
		}

		input.seekg(0, std::ios::beg);
		CacheFileHeader header{};
		if (!input.read(reinterpret_cast<char*>(&header), sizeof(header)))
			return false;

		const bool headerValid =
			std::memcmp(header.magic, kCacheMagic.data(), kCacheMagic.size()) == 0 &&
			header.schemaVersion == kCacheSchemaVersion &&
			header.headerSize == sizeof(CacheFileHeader) &&
			header.payloadSize > 0 && header.payloadSize <= kMaxPayloadSize &&
			header.vendorId == m_Identity.vendorId &&
			header.deviceId == m_Identity.deviceId &&
			header.driverVersion == m_Identity.driverVersion &&
			header.pipelineAbiVersion == kPipelineAbiVersion &&
			header.buildConfiguration == CurrentBuildConfiguration() &&
			std::memcmp(header.pipelineCacheUUID, m_Identity.pipelineCacheUUID, VK_UUID_SIZE) == 0 &&
			static_cast<uint64_t>(totalSize) == sizeof(CacheFileHeader) + header.payloadSize;
		if (!headerValid)
		{
			if (logFailures) VANS_LOG_WARN("[PipelineCache] Cache identity/header validation failed");
			return false;
		}

		outPayload.resize(static_cast<size_t>(header.payloadSize));
		if (!input.read(reinterpret_cast<char*>(outPayload.data()), static_cast<std::streamsize>(outPayload.size())))
		{
			outPayload.clear();
			return false;
		}

		if (HashBytes(outPayload.data(), outPayload.size()) != header.payloadHash || !ValidateVulkanPayload(outPayload))
		{
			if (logFailures) VANS_LOG_WARN("[PipelineCache] Cache payload validation failed");
			outPayload.clear();
			return false;
		}
		return true;
	}

	bool VansPipelineCacheService::ValidateVulkanPayload(const std::vector<uint8_t>& payload) const
	{
		constexpr size_t kHeaderSize = sizeof(uint32_t) * 4 + VK_UUID_SIZE;
		if (payload.size() < kHeaderSize)
			return false;

		uint32_t headerLength = 0;
		uint32_t headerVersion = 0;
		uint32_t vendorId = 0;
		uint32_t deviceId = 0;
		std::memcpy(&headerLength, payload.data() + 0, sizeof(uint32_t));
		std::memcpy(&headerVersion, payload.data() + 4, sizeof(uint32_t));
		std::memcpy(&vendorId, payload.data() + 8, sizeof(uint32_t));
		std::memcpy(&deviceId, payload.data() + 12, sizeof(uint32_t));
		return headerLength >= kHeaderSize && headerLength <= payload.size() &&
			headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
			vendorId == m_Identity.vendorId && deviceId == m_Identity.deviceId &&
			std::memcmp(payload.data() + 16, m_Identity.pipelineCacheUUID, VK_UUID_SIZE) == 0;
	}

	bool VansPipelineCacheService::MergeDiskCacheLocked()
	{
		std::vector<uint8_t> diskData;
		if (!ReadCacheFile(diskData, false))
			return true;

		const uint64_t diskHash = HashBytes(diskData.data(), diskData.size());
		if (diskHash == m_LastDiskPayloadHash)
			return true;

		VkPipelineCache diskCache = VK_NULL_HANDLE;
		if (!CreateCacheLocked(diskData, diskCache))
			return false;

		const VkResult result = VansGraphics::vkMergePipelineCaches(m_Device, m_Cache, 1, &diskCache);
		VansGraphics::vkDestroyPipelineCache(m_Device, diskCache, nullptr);
		if (result != VK_SUCCESS)
			return false;
		m_LastDiskPayloadHash = diskHash;
		return true;
	}

	void VansPipelineCacheService::MergeAndDestroyChildCachesLocked()
	{
		if (m_ChildCaches.empty() || m_Device == VK_NULL_HANDLE)
			return;

		std::vector<VkPipelineCache> children;
		children.reserve(m_ChildCaches.size());
		for (const auto& pair : m_ChildCaches)
		{
			if (pair.second != VK_NULL_HANDLE)
				children.push_back(pair.second);
		}

		if (!children.empty() && m_Cache != VK_NULL_HANDLE)
		{
			const VkResult result = VansGraphics::vkMergePipelineCaches(
				m_Device, m_Cache, static_cast<uint32_t>(children.size()), children.data());
			if (result == VK_SUCCESS)
				m_Dirty = true;
			else
				VANS_LOG_WARN("[PipelineCache] Failed to merge child caches. VkResult=" << result);
		}

		for (VkPipelineCache child : children)
			VansGraphics::vkDestroyPipelineCache(m_Device, child, nullptr);
		m_ChildCaches.clear();
	}

	bool VansPipelineCacheService::FlushLocked(VansPipelineCacheFlushReason reason)
	{
		if (!m_Initialized || m_Cache == VK_NULL_HANDLE)
			return false;
		if (reason == VansPipelineCacheFlushReason::Shutdown)
			MergeAndDestroyChildCachesLocked();
		if (!m_Dirty)
			return true;
		if (!m_WriteEnabled)
			return false;

		std::error_code ec;
		std::filesystem::create_directories(m_CacheFilePath.parent_path(), ec);
		if (ec)
			return false;

#if defined(_WIN32)
		ScopedFileLock fileLock(m_CacheFilePath.wstring() + L".lock");
		if (!fileLock.IsLocked())
		{
			VANS_LOG_WARN("[PipelineCache] Another process is writing the cache; skipping this flush");
			return false;
		}
#endif

		if (!MergeDiskCacheLocked())
			VANS_LOG_WARN("[PipelineCache] Failed to merge a newer on-disk cache; saving local data");

		std::vector<uint8_t> payload;
		if (!ReadCacheDataLocked(m_Cache, payload) || !ValidateVulkanPayload(payload))
		{
			VANS_LOG_WARN("[PipelineCache] Driver returned invalid/empty cache data");
			return false;
		}

		CacheFileHeader header{};
		std::memcpy(header.magic, kCacheMagic.data(), kCacheMagic.size());
		header.schemaVersion = kCacheSchemaVersion;
		header.headerSize = sizeof(CacheFileHeader);
		header.payloadSize = payload.size();
		header.payloadHash = HashBytes(payload.data(), payload.size());
		header.vendorId = m_Identity.vendorId;
		header.deviceId = m_Identity.deviceId;
		header.driverVersion = m_Identity.driverVersion;
		header.pipelineAbiVersion = kPipelineAbiVersion;
		header.buildConfiguration = CurrentBuildConfiguration();
		std::memcpy(header.pipelineCacheUUID, m_Identity.pipelineCacheUUID, VK_UUID_SIZE);

#if defined(_WIN32)
		const std::filesystem::path temporaryPath =
			m_CacheFilePath.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId());
#else
		const std::filesystem::path temporaryPath = m_CacheFilePath.string() + ".tmp";
#endif
		{
			std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
			if (!output)
				return false;
			output.write(reinterpret_cast<const char*>(&header), sizeof(header));
			output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
			output.flush();
			if (!output.good())
				return false;
		}

#if defined(_WIN32)
		if (!MoveFileExW(
			temporaryPath.c_str(),
			m_CacheFilePath.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			std::filesystem::remove(temporaryPath, ec);
			VANS_LOG_WARN("[PipelineCache] Atomic cache replacement failed. Win32=" << GetLastError());
			return false;
		}
#else
		std::filesystem::rename(temporaryPath, m_CacheFilePath, ec);
		if (ec)
			return false;
#endif

		m_LastDiskPayloadHash = header.payloadHash;
		m_Dirty = false;
		m_CreatesSinceFlush = 0;
		VANS_LOG("[PipelineCache] Flushed " << payload.size() << " bytes ("
			<< FlushReasonName(reason) << ") to " << m_CacheFilePath.string());
		return true;
	}

	void VansPipelineCacheService::MarkPipelineCreatedLocked(VansPipelineCachePipelineKind kind)
	{
		m_Dirty = true;
		++m_TotalPipelineCreates;
		++m_CreatesSinceFlush;
		switch (kind)
		{
		case VansPipelineCachePipelineKind::Graphics: ++m_GraphicsPipelineCreates; break;
		case VansPipelineCachePipelineKind::Compute: ++m_ComputePipelineCreates; break;
		case VansPipelineCachePipelineKind::RayTracing: ++m_RayTracingPipelineCreates; break;
		}
		m_LastMutation = std::chrono::steady_clock::now();
	}

	void VansPipelineCacheService::QuarantineInvalidCacheFile() const
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(m_CacheFilePath, ec) || ec)
			return;
		const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
		const std::filesystem::path quarantinePath =
			m_CacheFilePath.string() + ".invalid-" + std::to_string(timestamp);
		std::filesystem::rename(m_CacheFilePath, quarantinePath, ec);
		if (!ec)
			VANS_LOG_WARN("[PipelineCache] Quarantined invalid cache as " << quarantinePath.string());
	}

	std::filesystem::path VansPipelineCacheService::ResolveCacheFilePath(const CacheIdentity& identity)
	{
		std::filesystem::path root;
		if (const char* overrideRoot = std::getenv("FORESTENGINE_PIPELINE_CACHE_DIR"))
		{
			root = overrideRoot;
		}
		else if (const char* localAppData = std::getenv("LOCALAPPDATA"))
		{
			const char* productEnv = std::getenv("FORESTENGINE_PRODUCT_ID");
			const std::string product = SanitizePathComponent(productEnv ? productEnv : "Editor");
			root = std::filesystem::path(localAppData) / "ForestEngine" / product / "PipelineCache" / "Vulkan";
		}
		else
		{
			std::error_code ec;
			root = std::filesystem::temp_directory_path(ec) / "ForestEngine" / "PipelineCache" / "Vulkan";
		}

		std::ostringstream deviceFolder;
		deviceFolder << std::hex << identity.vendorId << '-' << identity.deviceId << '-'
			<< identity.driverVersion << '-' << UUIDToString(identity.pipelineCacheUUID);
#if defined(_DEBUG)
		const char* configuration = "debug";
#else
		const char* configuration = "release";
#endif
		return root / deviceFolder.str() /
			(std::string(configuration) + "-abi" + std::to_string(kPipelineAbiVersion) + ".bin");
	}

	uint64_t VansPipelineCacheService::HashBytes(const uint8_t* data, size_t size)
	{
		uint64_t hash = 14695981039346656037ull;
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= data[i];
			hash *= 1099511628211ull;
		}
		return hash;
	}

	const char* VansPipelineCacheService::FlushReasonName(VansPipelineCacheFlushReason reason)
	{
		switch (reason)
		{
		case VansPipelineCacheFlushReason::Periodic: return "periodic";
		case VansPipelineCacheFlushReason::Shutdown: return "shutdown";
		case VansPipelineCacheFlushReason::Manual: return "manual";
		}
		return "unknown";
	}

	void VansPipelineCacheService::RegisterDevice(VkDevice device, VansPipelineCacheService* service)
	{
		std::lock_guard<std::mutex> lock(g_ServiceRegistryMutex);
		g_ServiceRegistry[device] = service;
	}

	void VansPipelineCacheService::UnregisterDevice(VkDevice device, VansPipelineCacheService* service)
	{
		std::lock_guard<std::mutex> lock(g_ServiceRegistryMutex);
		auto it = g_ServiceRegistry.find(device);
		if (it != g_ServiceRegistry.end() && it->second == service)
			g_ServiceRegistry.erase(it);
	}
}
