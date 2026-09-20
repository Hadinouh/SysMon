#include "BackgroundSampler.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;
int main()
{
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::atomic<int> calls{0};
    BackgroundSampler<unsigned> sampler([&](unsigned flags) {
        if (++calls == 1) { entered.set_value(); gate.wait(); }
        if (flags == 8) throw std::runtime_error("simulated collector failure");
        return flags;
    });
    sampler.request(1);
    assert(entered.get_future().wait_for(2s) == std::future_status::ready);
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) { sampler.request(2); sampler.request(4); assert(!sampler.latest()); }
    assert(std::chrono::steady_clock::now() - start < 100ms);
    release.set_value();
    std::shared_ptr<const unsigned> first;
    for (int i = 0; i < 2000; ++i) {
        first = sampler.latest();
        if (first && *first == 6) break;
        std::this_thread::sleep_for(1ms);
    }
    assert(first && *first == 6 && calls == 2);
    sampler.request(8);
    while (calls < 3) std::this_thread::sleep_for(1ms);
    sampler.request(16);
    std::shared_ptr<const unsigned> next;
    for (int i = 0; i < 2000; ++i) {
        next = sampler.latest();
        if (next && *next == 16) break;
        std::this_thread::sleep_for(1ms);
    }
    assert(next && *next == 16 && *first == 6);
    sampler.stop();
    sampler.request(32);
    assert(calls == 4);
    std::cout << "PASS: nonblocking reads/requests, coalescing, immutable snapshots, failure recovery, shutdown\n";
}
