#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>
#include <Windows.h>
#include "../Netlib//MemoryPoolTLS.h"

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
        // memset(mID, 0, sizeof(mID));
        // memset(mNickName, 0, sizeof(mNickName));
        // memset(mSessionKey, 0, sizeof(mSessionKey));
        mSectorX = -1;
        mSectorY = -1;
        mWillDelete.store(false);
        mIsLoggedIn.store(false);
        mConnectTime.store(0, std::memory_order_relaxed);
        mLastActiveTime.store(0, std::memory_order_relaxed);
        mTimeoutLogged.store(false, std::memory_order_relaxed);
    }

    // Getters
    uint64_t GetSessionID() const { return mSessionID; }
    uint64_t GetAccountNo() const { return mAccountNo; }
    const WCHAR* GetID() const { return mID; }
    const WCHAR* GetNickName() const { return mNickName; }
    const char* GetSessionKey() const { return mSessionKey; }
    WORD GetSectorX() const { return mSectorX; }
    WORD GetSectorY() const { return mSectorY; }
    bool GetIsWillDelete() const { return mWillDelete.load(std::memory_order_relaxed); }
    bool GetIsLoginedIn() const { return mIsLoggedIn.load(std::memory_order_relaxed); }

    // Setters
    void SetSessionID(uint64_t sessionId) { mSessionID = sessionId; }
    void SetAccountNo(uint64_t accountNo) { mAccountNo = accountNo; }

    void SetID(const WCHAR* id) {
        wcsncpy_s(mID, 20, id, _TRUNCATE);
    }
    void SetNickName(const WCHAR* nickName) {
        wcsncpy_s(mNickName, 20, nickName, _TRUNCATE);
    }

    void SetSessionKey(const char* sessionKey) {
        strncpy_s(mSessionKey, sizeof(mSessionKey), sessionKey, _TRUNCATE);
    }

    void SetSectorX(WORD sectorX) { mSectorX = sectorX; }
    void SetSectorY(WORD sectorY) { mSectorY = sectorY; }
    void SetSector(WORD sectorX, WORD sectorY) {
        mSectorX = sectorX;
        mSectorY = sectorY;
    }

    void SetWillDelete() { mWillDelete.store(true, std::memory_order_relaxed); }
    // 로그인 진입은 반드시 OnLogin() 을 사용할 것 (mLastActiveTime 동기 갱신 필요)
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
        // 시간 먼저 기록, 플래그는 나중에 → 타임아웃 체크가
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
    TimeoutInfo CheckTimeOutSnapshot() {
        DWORD64 now  = GetTickCount64();
        bool    isIn = mIsLoggedIn.load();
        DWORD64 base = isIn
            ? mLastActiveTime.load()
            : mConnectTime.load();
        DWORD64 idle  = now - base;
        DWORD64 limit = isIn ? 120000ULL : 60000ULL; // 로그인 120s / 미로그인 60s
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
    WCHAR mID[20];
    WCHAR mNickName[20];
    char mSessionKey[64];
    WORD mSectorX;
    WORD mSectorY;
    std::atomic<bool> mWillDelete{ false };
    std::atomic<bool> mIsLoggedIn{ false };
    std::atomic<DWORD64> mConnectTime{ 0 };
    std::atomic<DWORD64> mLastActiveTime{ 0 };
    std::atomic<bool>    mTimeoutLogged{ false };
};