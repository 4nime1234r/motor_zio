#pragma once

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

#include "zio/core/Platform.h"

namespace zio::jobs
{

class ZIO_API JobSystem
{
public:
    explicit JobSystem(unsigned threadCount = std::thread::hardware_concurrency());
    ~JobSystem();

    JobSystem(const JobSystem&)            = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    static JobSystem& instance();

    void enqueue(std::function<void()> task, std::function<void()> done);

    void pumpMainThread();

    std::size_t pendingCompletions() const;

    std::size_t queuedTasks() const;

    unsigned workerCount() const;

    void shutdown();

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> taskQueue_;
    mutable std::mutex taskMutex_;
    std::condition_variable taskCv_;

    std::deque<std::function<void()>> completionQueue_;
    mutable std::mutex completionMutex_;

    bool stopping_ = false;
};

template <typename F, typename Done>
void run(JobSystem& jobs, F work, Done done)
{
    using Result = std::invoke_result_t<F>;
    if constexpr (std::is_void_v<Result>)
    {
        jobs.enqueue(std::move(work), std::move(done));
    }
    else
    {
        auto result = std::make_shared<std::optional<Result>>();
        jobs.enqueue([result, work = std::move(work)]() mutable { result->emplace(work()); },
                      [result, done = std::move(done)]() mutable { done(std::move(**result)); });
    }
}

template <typename F, typename Done>
void run(F work, Done done)
{
    run(JobSystem::instance(), std::move(work), std::move(done));
}

template <typename T>
void whenAll(JobSystem& jobs, std::vector<std::function<T()>> works,
             std::function<void(std::vector<std::optional<T>>)> done)
{
    if (works.empty())
    {
        done({});
        return;
    }

    auto results   = std::make_shared<std::vector<std::optional<T>>>(works.size());
    auto remaining = std::make_shared<std::size_t>(works.size());

    for (std::size_t i = 0; i < works.size(); ++i)
    {
        jobs.enqueue(
            [results, i, work = std::move(works[i])]() mutable
            {
                try
                {
                    (*results)[i].emplace(work());
                }
                catch (const std::exception& e)
                {
                    std::fprintf(stderr, "[zio::jobs] whenAll() work item %zu threw: %s\n", i, e.what());
                }
                catch (...)
                {
                    std::fprintf(stderr, "[zio::jobs] whenAll() work item %zu threw a non-std::exception\n", i);
                }
            },
            [results, remaining, done]() mutable
            {
                if (--*remaining == 0)
                    done(std::move(*results));
            });
    }
}

template <typename T>
void whenAll(std::vector<std::function<T()>> works, std::function<void(std::vector<std::optional<T>>)> done)
{
    whenAll(JobSystem::instance(), std::move(works), std::move(done));
}

template <typename T>
void whenAllWithProgress(JobSystem& jobs, std::vector<std::function<T()>> works,
                          std::function<void(std::size_t completed, std::size_t total)> onProgress,
                          std::function<void(std::vector<std::optional<T>>)> done)
{
    const std::size_t total = works.size();
    if (total == 0)
    {
        done({});
        return;
    }

    auto results   = std::make_shared<std::vector<std::optional<T>>>(total);
    auto remaining = std::make_shared<std::size_t>(total);
    auto completed = std::make_shared<std::size_t>(0);

    for (std::size_t i = 0; i < total; ++i)
    {
        jobs.enqueue(
            [results, i, work = std::move(works[i])]() mutable
            {
                try
                {
                    (*results)[i].emplace(work());
                }
                catch (const std::exception& e)
                {
                    std::fprintf(stderr, "[zio::jobs] whenAllWithProgress() work item %zu threw: %s\n", i, e.what());
                }
                catch (...)
                {
                    std::fprintf(stderr, "[zio::jobs] whenAllWithProgress() work item %zu threw a non-std::exception\n",
                                 i);
                }
            },
            [results, remaining, completed, total, onProgress, done]() mutable
            {
                onProgress(++(*completed), total);
                if (--*remaining == 0)
                    done(std::move(*results));
            });
    }
}

template <typename T>
void whenAllWithProgress(std::vector<std::function<T()>> works,
                          std::function<void(std::size_t completed, std::size_t total)> onProgress,
                          std::function<void(std::vector<std::optional<T>>)> done)
{
    whenAllWithProgress(JobSystem::instance(), std::move(works), std::move(onProgress), std::move(done));
}

}
