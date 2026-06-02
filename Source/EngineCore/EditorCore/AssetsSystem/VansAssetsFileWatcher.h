#pragma once
#include <iostream>
#include <thread>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>
#include <functional>
#include <mutex>
#include <atomic>
#include <windows.h>
#include <sys/stat.h>


inline time_t get_last_write_time(const std::string& path) 
{
    struct stat result;
    if (stat(path.c_str(), &result) == 0)
        return result.st_mtime;
    return 0;
}
class VansAssetsFileWatcher
{
	//创建一个线程，可以动态的加入和删除监视路径，当对应文件修改后，触发对应的回调函数

private:
    std::vector<std::string> m_Folders;
    std::unordered_map<std::string, std::unordered_map<std::string, FILETIME> > m_Snapshots;

    // 每个文件夹对应一个可在线程间共享的 updated 标志
    std::unordered_map<std::string, std::atomic<bool>> m_UpdatedFlags;


    std::thread m_WatchThread;
    std::mutex m_Mutex;
    std::atomic<bool> m_Watching{ false };

    std::unordered_map<std::string, FILETIME> SnapshotFolder(const std::string& folder)
    {
        std::unordered_map<std::string, FILETIME> snap;
        std::string searchPath = folder + "\\*";
        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE) return snap;
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                std::string fileName = findData.cFileName;

                // 跳过以 .spv 结尾的文件（不区分大小写）
                if (fileName.size() >= 4) {
                    const char* tail = fileName.c_str() + fileName.size() - 4;
                    if (lstrcmpiA(tail, ".spv") == 0) {
                        continue;
                    }
                }

                snap[fileName] = findData.ftLastWriteTime;
            }
        } 
        while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
        return snap;
    }

public:

    using Callback = std::function<void(const std::string&, const std::string&)>; // (folder, filename)

    void AddWatch(const std::string& folder) 
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (std::find(m_Folders.begin(), m_Folders.end(), folder) != m_Folders.end())
        {
            return;
        }
        m_Folders.push_back(folder);
        m_Snapshots[folder] = SnapshotFolder(folder);
        // 初始化该文件夹的 updated 标志为 false
        m_UpdatedFlags[folder].store(false, std::memory_order_relaxed);
    }

    void RemoveWatch(const std::string& folder) 
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Folders.erase(std::remove(m_Folders.begin(), m_Folders.end(), folder), m_Folders.end());
        m_Snapshots.erase(folder);
        m_UpdatedFlags.erase(folder);
    }

    void Start(Callback callback, int pollIntervalMs = 1000) 
    {
        if (m_Watching.exchange(true))
        {
            return;
        }

        m_WatchThread = std::thread([this, callback, pollIntervalMs] {
            while (m_Watching)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
                std::lock_guard<std::mutex> lock(m_Mutex);
                for (size_t i = 0; i < m_Folders.size(); ++i)
                {
                    const std::string& folder = m_Folders[i];
                    auto current = SnapshotFolder(folder);
                    auto& last = m_Snapshots[folder];
                    bool anyChanged = false;
                    // Check for new or modified files
                    for (std::unordered_map<std::string, FILETIME>::iterator it = current.begin(); it != current.end(); ++it)
                    {
                        if (last.count(it->first) == 0 || CompareFileTime(&last[it->first], &it->second) != 0)
                        {
                            anyChanged = true;
                            callback(folder, it->first);
                        }
                    }
                    last = current;

                    // 若该文件夹有变化，置 updated 标志
                    if (anyChanged)
                    {
                        auto fit = m_UpdatedFlags.find(folder);
                        if (fit != m_UpdatedFlags.end())
                        {
                            fit->second.store(true, std::memory_order_release);
                        }
                    }
                }
            }
        });
    }

    void Stop() 
    {
        m_Watching = false;
        if (m_WatchThread.joinable())
        {
            m_WatchThread.join();
        }
    }


    // 读取并清除（常用于“消费一次更新事件”）
    bool ConsumeUpdated(const std::string& folder)
    {
        bool result = false;
        {
            std::atomic<bool> flag;
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = m_UpdatedFlags.find(folder);
            if (it != m_UpdatedFlags.end())
            {
                result = it->second.exchange(false, std::memory_order_acq_rel);
            }
            
        }
        return result;
    }

    ~VansAssetsFileWatcher()
    { 
        Stop();
    }
};
