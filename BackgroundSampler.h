#pragma once
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

// A single worker with a coalescing request slot. Collection never holds the
// publication lock; readers keep an immutable snapshot while the next is built.
// Request/read/stop are called by the UI thread. The collector owns its state.
template<class Snapshot>
class BackgroundSampler
{
public:
    using Collector = std::function<Snapshot(unsigned)>;
    explicit BackgroundSampler(Collector collector) : collect_(std::move(collector)) {}
    ~BackgroundSampler() { stop(); }
    BackgroundSampler(const BackgroundSampler&) = delete;
    BackgroundSampler& operator=(const BackgroundSampler&) = delete;

    void request(unsigned flags = 1)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        pending_ |= flags;
        if (!worker_.joinable()) worker_ = std::thread([this] { run(); });
        ready_.notify_one();
    }

    std::shared_ptr<const Snapshot> latest()
    {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        return lock.owns_lock() ? latest_ : nullptr;
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            ready_.notify_one();
        }
        if (worker_.joinable()) worker_.join();
    }

private:
    void run()
    {
        for (;;)
        {
            unsigned flags;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || pending_ != 0; });
                if (stopping_) return;
                flags = pending_;
                pending_ = 0;
            }
            try
            {
                auto next = std::make_shared<const Snapshot>(collect_(flags));
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    latest_.swap(next);
                } // Destroy the old snapshot outside the publication lock.
            }
            catch (...)
            {
                // Keep the last complete sample if collection fails. A later
                // request retries without unwinding across the worker boundary.
            }
        }
    }

    Collector collect_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::thread worker_;
    unsigned pending_ = 0;
    bool stopping_ = false;
    std::shared_ptr<const Snapshot> latest_;
};
