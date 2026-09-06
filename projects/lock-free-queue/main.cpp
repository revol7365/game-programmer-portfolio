#include "LockFreeQueue.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

static bool SingleThreadFifoTest() {
    LockFreeQueue<int> queue;
    for (int i = 0; i < 10000; ++i) queue.Enqueue(i);
    for (int i = 0; i < 10000; ++i) {
        int value = -1;
        if (!queue.TryDequeue(value) || value != i) return false;
    }
    int value = 0;
    return !queue.TryDequeue(value) && queue.Size() == 0;
}

static bool MpmcStressRound(int round, int producers, int consumers, int perProducer) {
    LockFreeQueue<std::uint64_t> queue;
    const std::size_t total = static_cast<std::size_t>(producers) * perProducer;
    auto seen = std::make_unique<std::atomic<unsigned char>[]>(total + 1);
    for (std::size_t i = 0; i <= total; ++i) seen[i].store(0, std::memory_order_relaxed);

    std::atomic<int> producersLeft{producers};
    std::atomic<std::size_t> consumed{0};
    std::atomic<std::size_t> duplicates{0};
    std::atomic<std::size_t> corrupt{0};
    std::vector<std::thread> threads;

    for (int producer = 0; producer < producers; ++producer) {
        threads.emplace_back([&, producer] {
            const std::uint64_t first = static_cast<std::uint64_t>(producer) * perProducer + 1;
            for (int i = 0; i < perProducer; ++i) queue.Enqueue(first + i);
            producersLeft.fetch_sub(1, std::memory_order_release);
        });
    }

    for (int consumer = 0; consumer < consumers; ++consumer) {
        threads.emplace_back([&] {
            for (;;) {
                std::uint64_t value = 0;
                if (queue.TryDequeue(value)) {
                    consumed.fetch_add(1, std::memory_order_relaxed);
                    if (value == 0 || value > total) {
                        corrupt.fetch_add(1, std::memory_order_relaxed);
                    } else if (seen[value].fetch_add(1, std::memory_order_relaxed) != 0) {
                        duplicates.fetch_add(1, std::memory_order_relaxed);
                    }
                    continue;
                }
                if (producersLeft.load(std::memory_order_acquire) == 0 && queue.Size() == 0) break;
                std::this_thread::yield();
            }
        });
    }

    for (auto& thread : threads) thread.join();

    std::size_t missing = 0;
    for (std::size_t i = 1; i <= total; ++i) {
        if (seen[i].load(std::memory_order_relaxed) == 0) ++missing;
    }

    const bool passed = consumed.load() == total && duplicates.load() == 0 &&
        corrupt.load() == 0 && missing == 0 && queue.Size() == 0;
    std::printf("Round %d: produced=%zu consumed=%zu missing=%zu duplicate=%zu corrupt=%zu %s\n",
        round, total, consumed.load(), missing, duplicates.load(), corrupt.load(),
        passed ? "PASS" : "FAIL");
    return passed;
}

int main() {
    const auto begin = std::chrono::steady_clock::now();
    bool passed = SingleThreadFifoTest();
    std::printf("SingleThreadFifo: %s\n", passed ? "PASS" : "FAIL");
    for (int round = 1; round <= 10; ++round) {
        passed = MpmcStressRound(round, 8, 8, 200000) && passed;
    }
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - begin).count();
    std::printf("Elapsed: %.3f sec\nRESULT: %s\n", elapsed, passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
