#include "LockFreeMemoryPool.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

struct Payload {
    static std::atomic<long long> live;
    std::uint64_t words[8]{};

    Payload() { live.fetch_add(1, std::memory_order_relaxed); }
    Payload(const Payload& other) {
        for (int i = 0; i < 8; ++i) words[i] = other.words[i];
        live.fetch_add(1, std::memory_order_relaxed);
    }
    Payload& operator=(const Payload&) = default;
    ~Payload() { live.fetch_sub(1, std::memory_order_relaxed); }
};

std::atomic<long long> Payload::live{0};

static bool FunctionTest() {
    LockFreeMemoryPool<Payload> pool(128);
    std::vector<Payload*> items;
    items.reserve(4096);

    for (std::size_t i = 0; i < 4096; ++i) {
        Payload* value = pool.Alloc();
        value->words[0] = i;
        items.push_back(value);
    }

    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i]->words[0] != i || !pool.Free(items[i])) return false;
    }

    return pool.GetUsedCount() == 0 && pool.GetCapacity() >= 4096;
}

static bool MultiThreadTest() {
    constexpr int kThreads = 16;
    constexpr int kIterations = 100000;
    LockFreeMemoryPool<Payload> pool(2048);
    std::atomic<bool> ok{true};
    std::vector<std::thread> workers;

    for (int tid = 0; tid < kThreads; ++tid) {
        workers.emplace_back([&, tid] {
            for (int i = 0; i < kIterations; ++i) {
                Payload* value = pool.Alloc();
                value->words[0] = static_cast<std::uint64_t>(tid);
                value->words[1] = static_cast<std::uint64_t>(i);
                if (value->words[0] != static_cast<std::uint64_t>(tid) ||
                    value->words[1] != static_cast<std::uint64_t>(i) ||
                    !pool.Free(value)) {
                    ok.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    for (auto& worker : workers) worker.join();
    return ok.load() && pool.GetUsedCount() == 0;
}

static double BenchmarkPool() {
    constexpr int kThreads = 8;
    constexpr int kIterations = 250000;
    LockFreeMemoryPool<Payload> pool(8192);
    std::vector<std::thread> workers;
    const auto begin = std::chrono::steady_clock::now();

    for (int tid = 0; tid < kThreads; ++tid) {
        workers.emplace_back([&, tid] {
            for (int i = 0; i < kIterations; ++i) {
                Payload* value = pool.Alloc();
                value->words[0] = static_cast<std::uint64_t>(tid + i);
                pool.Free(value);
            }
        });
    }
    for (auto& worker : workers) worker.join();

    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
}

int main() {
    const bool functionPassed = FunctionTest();
    const bool multiThreadPassed = MultiThreadTest();
    const double elapsedMs = BenchmarkPool();
    const bool lifetimePassed = Payload::live.load(std::memory_order_acquire) == 0;

    std::printf("FunctionTest: %s\n", functionPassed ? "PASS" : "FAIL");
    std::printf("MultiThreadTest: %s\n", multiThreadPassed ? "PASS" : "FAIL");
    std::printf("LifetimeTest: %s (live=%lld)\n", lifetimePassed ? "PASS" : "FAIL",
        Payload::live.load());
    std::printf("Benchmark: %d operations, %.3f ms\n", 8 * 250000, elapsedMs);

    const bool passed = functionPassed && multiThreadPassed && lifetimePassed;
    std::printf("RESULT: %s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
