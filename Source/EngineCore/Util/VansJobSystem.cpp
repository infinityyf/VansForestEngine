#include "VansJobSystem.h"
#include "VansProfiler.h"
#include <exception>

namespace Vans
{
    static VansJobSystem* s_Instance = nullptr;

    VansJobSystem& VansJobSystem::Get()
    {
        if (!s_Instance)
        {
            s_Instance = new VansJobSystem();
        }
        return *s_Instance;
    }

    VansJobGroup::VansJobGroup(uint32_t jobCount)
        : m_Remaining(jobCount)
    {
    }

    void VansJobGroup::Done()
    {
        const uint32_t previous = m_Remaining.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 1)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Condition.notify_all();
        }
    }

    void VansJobGroup::Wait()
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_Condition.wait(lock, [this]
        {
            return m_Remaining.load(std::memory_order_acquire) == 0;
        });
    }

    void VansJobGroup::SetError(const std::string& error)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_HasError)
        {
            m_HasError = true;
            m_Error = error;
        }
    }

    bool VansJobGroup::HasErrors() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_HasError;
    }

    std::string VansJobGroup::GetError() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Error;
    }

    VansJobSystem::VansJobSystem()
        : m_Running(false)
    {
    }

    VansJobSystem::~VansJobSystem()
    {
        Shutdown();
    }

    void VansJobSystem::Initialize(uint32_t threadCount)
    {
        if (m_Running) return;

        m_Running = true;

        if (threadCount == 0)
        {
            threadCount = std::thread::hardware_concurrency();
            if (threadCount < 2) threadCount = 2; // At least one worker + main
            threadCount -= 1; // Reserve one for main thread
        }

        for (uint32_t i = 0; i < threadCount; ++i)
        {
            m_Workers.emplace_back(&VansJobSystem::WorkerLoop, this);
        }
    }

    void VansJobSystem::Shutdown()
    {
        if (!m_Running) return;

        m_Running = false;
        m_WorkerCondition.notify_all();

        for (std::thread& worker : m_Workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        m_Workers.clear();
    }

    void VansJobSystem::QueueJob(Job job)
    {
        {
            std::lock_guard<std::mutex> lock(m_WorkerQueueMutex);
            m_WorkerQueue.push(job);
        }
        m_WorkerCondition.notify_one();
    }

    std::shared_ptr<VansJobGroup> VansJobSystem::QueueJobGroup(const std::vector<Job>& jobs)
    {
        auto group = std::make_shared<VansJobGroup>(static_cast<uint32_t>(jobs.size()));
        if (jobs.empty())
        {
            return group;
        }

        if (!m_Running || m_Workers.empty())
        {
            for (const Job& job : jobs)
            {
                try
                {
                    if (job)
                    {
                        job();
                    }
                }
                catch (const std::exception& e)
                {
                    group->SetError(e.what());
                }
                catch (...)
                {
                    group->SetError("unknown worker exception");
                }
                group->Done();
            }
            return group;
        }

        for (const Job& job : jobs)
        {
            QueueJob([job, group]()
            {
                try
                {
                    if (job)
                    {
                        job();
                    }
                }
                catch (const std::exception& e)
                {
                    group->SetError(e.what());
                }
                catch (...)
                {
                    group->SetError("unknown worker exception");
                }
                group->Done();
            });
        }
        return group;
    }

    void VansJobSystem::QueueMainThreadJob(Job job)
    {
        std::lock_guard<std::mutex> lock(m_MainThreadQueueMutex);
        m_MainThreadQueue.push(job);
    }

    void VansJobSystem::ProcessMainThreadJobs()
    {
        VANS_PROFILE_SCOPE("JobSystem::ProcessMainThreadJobsInternal", Vans::ProfileCategory::JobSystem);

        // Copy the queue to avoid blocking for too long
        std::queue<Job> jobsToRun;
        {
            std::lock_guard<std::mutex> lock(m_MainThreadQueueMutex);
            if (m_MainThreadQueue.empty()) return;
            jobsToRun.swap(m_MainThreadQueue);
        }

        while (!jobsToRun.empty())
        {
            Job job = jobsToRun.front();
            jobsToRun.pop();
            if (job)
            {
                VANS_PROFILE_SCOPE("JobSystem::RunMainThreadJob", Vans::ProfileCategory::JobSystem);
                job();
            }
        }
    }

    void VansJobSystem::WorkerLoop()
    {
        VANS_PROFILE_THREAD("Worker Thread");

        while (m_Running)
        {
            Job job;
            {
                VANS_PROFILE_SCOPE("JobSystem::WaitJob", Vans::ProfileCategory::Wait);
                std::unique_lock<std::mutex> lock(m_WorkerQueueMutex);
                m_WorkerCondition.wait(lock, [this] {
                    return !m_WorkerQueue.empty() || !m_Running;
                });

                if (!m_Running && m_WorkerQueue.empty())
                {
                    return;
                }

                if (!m_WorkerQueue.empty())
                {
                    job = m_WorkerQueue.front();
                    m_WorkerQueue.pop();
                }
            }

            if (job)
            {
                VANS_PROFILE_SCOPE("JobSystem::RunJob", Vans::ProfileCategory::JobSystem);
                job();
            }
        }
    }
}
