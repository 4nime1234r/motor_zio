#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace zio_editor
{

enum class UpdateState
{
    Idle,
    Checking,
    Available,
    Downloading,
    Downloaded,
    Error
};

struct UpdateStatus
{
    UpdateState state = UpdateState::Idle;
    std::string version;
    float progress = 0.f;
    std::string error;
    std::string notes;
    bool checked  = false;
    bool upToDate = false;
};

class Updater
{
public:
    Updater() = default;
    ~Updater();

    Updater(const Updater&)            = delete;
    Updater& operator=(const Updater&) = delete;

    static bool enabled();
    static void cleanupLeftovers();

    void start();
    UpdateStatus status() const;
    void download();
    void retry();
    bool checkNow();
    void finish(bool restart);

private:
    void run();
    void check();
    void loadNotes(const std::string& version);
    void fetch();
    void setState(UpdateState state, std::string error = {});

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    UpdateStatus status_;
    std::atomic<bool> cancel_{false};
    std::string expectedHash_;
    bool stop_             = false;
    bool downloadRequested_ = false;
    bool recheckRequested_  = false;
};

}
