#include "zio/jobs/JobSystem.h"

#include <cstdio>
#include <exception>

namespace zio::jobs
{

JobSystem::JobSystem(unsigned threadCount)
{
    if (threadCount == 0)
        threadCount = 1;

    workers_.reserve(threadCount);
    for (unsigned i = 0; i < threadCount; ++i)
        workers_.emplace_back([this] { workerLoop(); });
}

JobSystem::~JobSystem()
{
    shutdown();
}

JobSystem& JobSystem::instance()
{
    static JobSystem global;
    return global;
}

void JobSystem::enqueue(std::function<void()> task, std::function<void()> done)
{
    {
        std::lock_guard lock(taskMutex_);
        if (stopping_)
            return;
        taskQueue_.emplace_back(
            [this, task = std::move(task), done = std::move(done)]() mutable
            {
                try
                {
                    task();
                }
                catch (const std::exception& e)
                {
                    std::fprintf(stderr, "[zio::jobs] worker task threw std::exception: %s - completion dropped\n",
                                 e.what());
                    return;
                }
                catch (...)
                {
                    std::fprintf(stderr, "[zio::jobs] worker task threw a non-std::exception - completion dropped\n");
                    return;
                }
                std::lock_guard completionLock(completionMutex_);
                completionQueue_.emplace_back(std::move(done));
            });
    }
    taskCv_.notify_one();
}

void JobSystem::pumpMainThread()
{
    std::deque<std::function<void()>> ready;
    {
        std::lock_guard lock(completionMutex_);
        ready.swap(completionQueue_);
    }
    for (auto& fn : ready)
        fn();
}

std::size_t JobSystem::pendingCompletions() const
{
    std::lock_guard lock(completionMutex_);
    return completionQueue_.size();
}

std::size_t JobSystem::queuedTasks() const
{
    std::lock_guard lock(taskMutex_);
    return taskQueue_.size();
}

unsigned JobSystem::workerCount() const
{
    std::lock_guard lock(taskMutex_);
    return static_cast<unsigned>(workers_.size());
}

void JobSystem::shutdown()
{
    {
        std::lock_guard lock(taskMutex_);
        if (stopping_)
            return;
        stopping_ = true;
    }
    taskCv_.notify_all();

    for (auto& worker : workers_)
        if (worker.joinable())
            worker.join();
    {
        std::lock_guard lock(taskMutex_);
        workers_.clear();
    }
}

void JobSystem::workerLoop()
{
    for (;;)
    {
        std::function<void()> task;
        {
            std::unique_lock lock(taskMutex_);
            taskCv_.wait(lock, [this] { return stopping_ || !taskQueue_.empty(); });
            if (stopping_ && taskQueue_.empty())
                return;
            task = std::move(taskQueue_.front());
            taskQueue_.pop_front();
        }
        task();
    }
}

}
