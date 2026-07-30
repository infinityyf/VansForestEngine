#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Vans
{
	enum class VansFileChangeKind
	{
		Added,
		Modified,
		Removed,
		Renamed
	};

	struct VansFileChange
	{
		std::filesystem::path path;
		std::filesystem::path previousPath;
		VansFileChangeKind kind = VansFileChangeKind::Modified;
		std::uint64_t sequence = 0;
		std::chrono::steady_clock::time_point observedAt;
	};

	struct VansAssetFileChangedEvent
	{
		VansFileChange change;
	};

	// Editor-owned recursive file event source. It intentionally has no
	// dependency on RenderCore and carries no shader-specific policy.
	class VansAssetsFileWatcher
	{
	public:
		VansAssetsFileWatcher() = default;
		~VansAssetsFileWatcher();

		VansAssetsFileWatcher(const VansAssetsFileWatcher&) = delete;
		VansAssetsFileWatcher& operator=(const VansAssetsFileWatcher&) = delete;

		void WatchTree(const std::filesystem::path& root);
		void ClearWatches();
		void Start(std::chrono::milliseconds pollInterval = std::chrono::milliseconds(200));
		void Stop();

		bool IsWatching() const { return m_Watching.load(std::memory_order_acquire); }

	private:
		using FileTime = std::filesystem::file_time_type;
		using Snapshot = std::unordered_map<std::wstring, FileTime>;

		static std::filesystem::path NormalizeExistingPath(const std::filesystem::path& path);
		static std::wstring MakePathKey(const std::filesystem::path& path);
		static Snapshot SnapshotTree(const std::filesystem::path& root);

		void PollOnce();
		void QueueChange(const std::filesystem::path& path, VansFileChangeKind kind);

		std::mutex m_Mutex;
		std::vector<std::filesystem::path> m_Roots;
		std::unordered_map<std::wstring, Snapshot> m_Snapshots;
		std::thread m_WatchThread;
		std::atomic<bool> m_Watching{ false };
		std::atomic<std::uint64_t> m_NextSequence{ 1 };
	};
}
