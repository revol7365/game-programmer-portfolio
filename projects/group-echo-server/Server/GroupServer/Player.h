#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>

#include <Windows.h>

class Group;

class Player {
public:
    Player()
    {
        Clear();
    }

    ~Player() {}

    inline static MemoryPoolTLS<Player> playerMemoryPool{ 1024 * 10 };

    void Clear() {
        mSessionID = 0;
        mAccountNo = 0;
        memset(mSessionKey, 0, sizeof(mSessionKey));
        mWillDelete.store(false);
        mIsLoggedIn.store(false);
        mConnectTime.store(0, std::memory_order_relaxed);
        mLastActiveTime.store(0, std::memory_order_relaxed);
        mTimeoutLogged.store(false, std::memory_order_relaxed);
        mGroup.store(nullptr, std::memory_order_relaxed);
    }

    // 현재 소속 그룹. 워커가 read(acquire), 현 소속 그룹의 컨텐츠 스레드만 write(release).
    std::atomic<Group*> mGroup{ nullptr };

    // Getters
    uint64_t GetSessionID() const { return mSessionID; }
    uint64_t GetAccountNo() const { return mAccountNo; }
    const char* GetSessionKey() const { return mSessionKey; }
    bool GetIsWillDelete() const { return mWillDelete.load(std::memory_order_relaxed); }
    bool GetIsLoginedIn() const { return mIsLoggedIn.load(std::memory_order_relaxed); }

    // Setters
    void SetSessionID(uint64_t sessionId) { mSessionID = sessionId; }
    void SetAccountNo(uint64_t accountNo) { mAccountNo = accountNo; }

    void SetSessionKey(const char* sessionKey) {
        strncpy_s(mSessionKey, sizeof(mSessionKey), sessionKey, _TRUNCATE);
    }

    void SetWillDelete() { mWillDelete.store(true, std::memory_order_relaxed); }
    void SetIsLoggedout() { mIsLoggedIn.store(false, std::memory_order_relaxed); }

    // TimeOut
    //   expired:     타임아웃 상태인가
    //   firstReport: 이번에 "최초로" 타임아웃 감지된 호출인가 (중복 로깅/카운팅 방지용)
    //   loggedIn:    판정 시점의 로그인 여부
    //   idleMs:      판정 시점의 유휴 시간
    struct TimeoutInfo { bool expired; bool firstReport; bool loggedIn; DWORD64 idleMs; };

    void OnConnect() {
        mConnectTime.store(GetTickCount64());
        mTimeoutLogged.store(false);
    }

    void OnLogin() {
        // 시간 먼저 기록, 플래그는 나중에 → 타임아웃 스레드가
        // mIsLoggedIn=true 를 본 시점에 mLastActiveTime 이 반드시 유효.
        // mTimeoutLogged 는 세션 생명주기 동안 유지 (세션당 1회 정책)
        mLastActiveTime.store(GetTickCount64());
        mIsLoggedIn.store(true);
    }

    void UpdateHeartbeat() {
        // mTimeoutLogged 는 리셋하지 않음 (세션당 1회 정책)
        mLastActiveTime.store(GetTickCount64());
    }

    // 판정과 idle 값을 한 번의 snapshot 으로 반환 (분리된 두 번 읽기 금지)
    // expired 진입 시 CAS 로 최초 1회만 firstReport=true 반환 → 로그/카운트 스팸 방지
    // 사양: 클라가 30초마다 하트비트, 서버는 40초 이상 무수신 시 끊음
    TimeoutInfo CheckTimeOutSnapshot() {
        DWORD64 now  = GetTickCount64();
        bool    isIn = mIsLoggedIn.load();
        DWORD64 base = isIn
            ? mLastActiveTime.load()
            : mConnectTime.load();
        DWORD64 idle  = now - base;
        DWORD64 limit = isIn ? 40000ULL : 60000ULL; // 로그인 40s / 미로그인 60s
        bool    expired = idle > limit;
        bool    firstReport = false;
        if (expired) {
            bool expected = false;
            firstReport = mTimeoutLogged.compare_exchange_strong(expected, true);
        }
        return { expired, firstReport, isIn, idle };
    }

private:
    uint64_t mSessionID;
    uint64_t mAccountNo;
    char mSessionKey[64];
    std::atomic<bool> mWillDelete{ false };
    std::atomic<bool> mIsLoggedIn{ false };
    std::atomic<DWORD64> mConnectTime{ 0 };
    std::atomic<DWORD64> mLastActiveTime{ 0 };
    std::atomic<bool>    mTimeoutLogged{ false };
};
