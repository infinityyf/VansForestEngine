#include "VansAssetsFileWatcher.h"

#include "../../EventCore/VansEventBus.h"

#include <algorithm>
#include <cwctype>
#include <system_error>

namespace Vans
{
	namespace
	{
		bool IsSameOrChildPath(const std::filesystem::path& candidate, const std::filesystem::path& root)
		{
			auto candidateIt = candidate.begin();
			auto rootIt = root.begin();
			for (; rootIt != root.end(); ++rootIt, ++candidateIt)
			{
				if (candidateIt == candidate.end())
					return false;

				std::wstring candidatePart = candidateIt->wstring();
				std::wstring rootPart = rootIt->wstring();
#if defined(_WIN32)
				std::transform(candidatePart.begin(), candidatePart.end(), candidatePart.begin(), [](wchar_t c) { return std::towlower(c); });
				std::transform(rootPart.begin(), rootPart.end(), rootPart.begin(), [](wchar_t c) { return std::towlower(c); });
#endif
				if (candidatePart != rootPart)
					return false;
			}
			return true;
		}
	}

	VansAssetsFileWatcher::~VansAssetsFileWatcher()
	{
		Stop();
	}

	std::filesystem::path VansAssetsFileWatcher::NormalizeExistingPath(const std::filesystem::path& path)
	{
		std::error_code ec;
		std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
		if (ec)
		{
			ec.clear();
			normalized = std::filesystem::absolute(path, ec);
		}
		return normalized.lexically_normal();
	}

	std::wstring VansAssetsFileWatcher::MakePathKey(const std::filesystem::path& path)
	{
		std::wstring key = NormalizeExistingPath(path).wstring();
#if defined(_WIN32)
		std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return std::towlower(c); });
#endif
		return key;
	}

	VansAssetsFileWatcher::Snapshot VansAssetsFileWatcher::SnapshotTree(const std::filesystem::path& root)
	{
		Snapshot snapshot;
		std::error_code ec;
		if (!std::filesystem::exists(root, ec) || ec)
			return snapshot;

		std::filesystem::recursive_directory_iterator it(
			root,
			std::filesystem::directory_options::skip_permission_denied,
			ec);
		const std::filesystem::recursive_directory_iterator end;
		while (!ec && it != end)
		{
			const auto& entry = *it;
			std::error_code entryEc;
			if (entry.is_regular_file(entryEc) && !entryEc)
			{
				const std::filesystem::path path = NormalizeExistingPath(entry.path());
				const FileTime writeTime = entry.last_write_time(entryEc);
				if (!entryEc)
					snapshot.emplace(MakePathKey(path), writeTime);
			}
			it.increment(ec);
		}
		return snapshot;
	}

	void VansAssetsFileWatcher::WatchTree(const std::filesystem::path& root)
	{
		if (root.empty())
			return;

		const std::filesystem::path normalized = NormalizeExistingPath(root);
		std::error_code ec;
		if (!std::filesystem::is_directory(normalized, ec) || ec)
			return;

		std::lock_guard<std::mutex> lock(m_Mutex);
		for (const auto& existing : m_Roots)
		{
			if (IsSameOrChildPath(normalized, existing))
				return;
		}

		for (auto it = m_Roots.begin(); it != m_Roots.end();)
		{
			if (IsSameOrChildPath(*it, normalized))
			{
				m_Snapshots.erase(MakePathKey(*it));
				it = m_Roots.erase(it);
			}
			else
			{
				++it;
			}
		}

		m_Roots.push_back(normalized);
		m_Snapshots[MakePathKey(normalized)] = SnapshotTree(normalized);
	}

	void VansAssetsFileWatcher::ClearWatches()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Roots.clear();
		m_Snapshots.clear();
	}

	void VansAssetsFileWatcher::Start(std::chrono::milliseconds pollInterval)
	{
		if (m_Watching.exchange(true, std::memory_order_acq_rel))
			return;

		m_WatchThread = std::thread([this, pollInterval]()
		{
			while (m_Watching.load(std::memory_order_acquire))
			{
				std::this_thread::sleep_for(pollInterval);
				if (m_Watching.load(std::memory_order_acquire))
					PollOnce();
			}
		});
	}

	void VansAssetsFileWatcher::Stop()
	{
		m_Watching.store(false, std::memory_order_release);
		if (m_WatchThread.joinable())
			m_WatchThread.join();
	}

	void VansAssetsFileWatcher::PollOnce()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (const std::filesystem::path& root : m_Roots)
		{
			const std::wstring rootKey = MakePathKey(root);
			Snapshot current = SnapshotTree(root);
			Snapshot& previous = m_Snapshots[rootKey];

			for (const auto& [pathKey, writeTime] : current)
			{
				const auto previousIt = previous.find(pathKey);
				if (previousIt == previous.end())
					QueueChange(std::filesystem::path(pathKey), VansFileChangeKind::Added);
				else if (previousIt->second != writeTime)
					QueueChange(std::filesystem::path(pathKey), VansFileChangeKind::Modified);
			}

			for (const auto& [pathKey, ignoredWriteTime] : previous)
			{
				(void)ignoredWriteTime;
				if (current.find(pathKey) == current.end())
					QueueChange(std::filesystem::path(pathKey), VansFileChangeKind::Removed);
			}

			previous = std::move(current);
		}
	}

	void VansAssetsFileWatcher::QueueChange(const std::filesystem::path& path, VansFileChangeKind kind)
	{
		VansFileChange change;
		change.path = path;
		change.kind = kind;
		change.sequence = m_NextSequence.fetch_add(1, std::memory_order_relaxed);
		change.observedAt = std::chrono::steady_clock::now();
		VansEventBus::Get().Enqueue(
			VansAssetFileChangedEvent{ std::move(change) },
			VansEventLane::Editor);
	}
}
