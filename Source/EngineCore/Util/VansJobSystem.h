#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace Vans
{
    class VansJobSystem
    {
    public:
        using Job = std::function<void()>;

        VansJobSystem();
        ~VansJobSystem();

        // Initialize the worker threads
        void Initialize(uint32_t threadCount = 0);
        void Shutdown();

        // Add a job to run on a background worker thread
        void QueueJob(Job job);

        // Add a job to run on the main thread (thread safe)
        void QueueMainThreadJob(Job job);

        // Process all currently queued main thread jobs. Call this in your main loop.
        void ProcessMainThreadJobs();

        static VansJobSystem& Get();

    private:
        void WorkerLoop();

        std::vector<std::thread> m_Workers;
        
        std::queue<Job> m_WorkerQueue;
        std::mutex m_WorkerQueueMutex;
        std::condition_variable m_WorkerCondition;

        std::queue<Job> m_MainThreadQueue;
        std::mutex m_MainThreadQueueMutex;

        std::atomic<bool> m_Running;
    };
}
