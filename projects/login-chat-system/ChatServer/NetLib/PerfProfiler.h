#pragma once
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>

#include "../Utils/ConsoleLog.h"

struct PerfSection {
    alignas(64) std::atomic<long long> totalUs{ 0 };
    alignas(64) std::atomic<long>      callCount{ 0 };
    alignas(64) std::atomic<long long> maxUs{ 0 };

    void Add(long long us) {
        totalUs.fetch_add(us, std::memory_order_relaxed);
        callCount.fetch_add(1, std::memory_order_relaxed);

        long long prev = maxUs.load(std::memory_order_relaxed);
        while (us > prev) {
            if (maxUs.compare_exchange_weak(prev, us, std::memory_order_relaxed))
                break;
        }
    }

    void Snapshot(long long& outTotalUs, long& outCount, long long& outMaxUs) {
        outTotalUs = totalUs.exchange(0, std::memory_order_relaxed);
        outCount = callCount.exchange(0, std::memory_order_relaxed);
        outMaxUs = maxUs.exchange(0, std::memory_order_relaxed);
    }
};

class PerfTimer {
    LARGE_INTEGER mStart;
    PerfSection& mSection;

    static long long QpcToUs(LARGE_INTEGER start, LARGE_INTEGER end) {
        static LARGE_INTEGER freq = []() {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return f;
            }();
        return (end.QuadPart - start.QuadPart) * 1000000 / freq.QuadPart;
    }

public:
    PerfTimer(PerfSection& section) : mSection(section) {
        QueryPerformanceCounter(&mStart);
    }

    ~PerfTimer() {
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        mSection.Add(QpcToUs(mStart, end));
    }
};

struct PerfMonitor {
    // 네트워크
    PerfSection recvParse;
    PerfSection sendPacket;
    PerfSection postSend;
    PerfSection encrypt;

    // 로직
    PerfSection broadcast;
    PerfSection sectorLock;     // 순수 Lock 획득 대기
    PerfSection sendLoop;       // Lock 내부 SendPacket 루프

    void PrintAll() {
        ConsolePrintLine(L"  --- Perf (avg / max us, count/s) ---");

        auto print = [](const wchar_t* name, PerfSection& s) {
            long long t; long c; long long m;
            s.Snapshot(t, c, m);
            long long avg = c > 0 ? t / c : 0;
            ConsolePrintLine(L"  %-16s avg:%-8lld max:%-8lld cnt:%-10ld", name, avg, m, c);
            };

        print(L"RecvParse", recvParse);
        print(L"Encrypt", encrypt);
        print(L"SendPacket", sendPacket);
        print(L"PostSend", postSend);
        ConsolePrintLine(L"  --");
        print(L"Broadcast", broadcast);
        print(L"  SectorLock", sectorLock);
        print(L"  SendLoop", sendLoop);
    }

    void LogAll() {
        auto log = [](const wchar_t* name, PerfSection& s) {
            long long t; long c; long long m;
            s.Snapshot(t, c, m);
            long long avg = c > 0 ? t / c : 0;
            LOG(L"[Perf] %s avg:%lld us, max:%lld us, cnt:%ld/s", name, avg, m, c);
            };

        log(L"RecvParse", recvParse);
        log(L"Encrypt", encrypt);
        log(L"SendPacket", sendPacket);
        log(L"PostSend", postSend);
        log(L"Broadcast", broadcast);
        log(L"SectorLock", sectorLock);
        log(L"SendLoop", sendLoop);
    }
};

inline PerfMonitor g_perf;

#define ENABLE_PERF 1

#if ENABLE_PERF
#define PERF_TIMER(section)  PerfTimer _pt_##section(g_perf.section)
#else
#define PERF_TIMER(section)  ((void)0)
#endif