#pragma once

#include <atomic>
#include <unordered_map>
#include <queue>
#include <cstdint>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <Windows.h>

#include "../NetLib/Packet.h"
#include "Player.h"

class GroupServer;

enum class JobType : uint8_t {
    JOIN,         // OnClientJoin → 첫 그룹 입장
    LEAVE,        // OnClientLeave → 실접속 해제 (Player 해제 책임)
    PACKET,       // OnRecv → 패킷 처리 (packet refcount 1 전달)
    GROUP_ENTER,  // 다른 그룹에서 이동해 들어옴 (Player 소유권 인계)
};

struct Job {
    JobType  type;
    Player*  player;
    Packet*  packet; // PACKET 타입에서만 유효, 그 외는 nullptr
};

// 단순 MPSC 큐: CRITICAL_SECTION + std::queue.
//   - Multi-producer (워커들/다른 그룹) → Enqueue
//   - Single-consumer (이 그룹의 컨텐츠 스레드) → DrainTo
// LockFreeQueue 의 __debugbreak / ABA / spin starvation 위험을 회피.
class JobMailbox {
public:
    JobMailbox() { InitializeCriticalSection(&mCs); }
    ~JobMailbox() { DeleteCriticalSection(&mCs); }

    JobMailbox(const JobMailbox&) = delete;
    JobMailbox& operator=(const JobMailbox&) = delete;

    void Enqueue(const Job& j) {
        EnterCriticalSection(&mCs);
        mQueue.push(j);
        mApproxSize.store((long)mQueue.size(), std::memory_order_relaxed);
        LeaveCriticalSection(&mCs);
    }

    // 한 번에 모든 잡을 꺼내 out 으로 옮긴다 (out 은 비어있어야 함)
    // 컨텐츠 스레드만 호출.
    void DrainTo(std::queue<Job>& out) {
        EnterCriticalSection(&mCs);
        if (!mQueue.empty()) std::swap(mQueue, out);
        mApproxSize.store(0, std::memory_order_relaxed);
        LeaveCriticalSection(&mCs);
    }

    // 모니터링용 (외부 스레드에서 락 없이 근사값 조회)
    long ApproxSize() const { return mApproxSize.load(std::memory_order_relaxed); }

private:
    CRITICAL_SECTION mCs;
    std::queue<Job> mQueue;
    std::atomic<long> mApproxSize{ 0 };
};

class Group {
public:
    Group(GroupServer* server) : mServer(server) {}
    virtual ~Group() = default;

    // 워커/다른 그룹 → 메일박스에 잡 투입
    void Enqueue(const Job& job) {
        mMailbox.Enqueue(job);
    }

    // 컨텐츠 스레드 1회 처리 — drain → dispatch
    int Update() {
        std::queue<Job> local;
        mMailbox.DrainTo(local);

        int processed = 0;
        while (!local.empty()) {
            const Job& job = local.front();
            switch (job.type) {
            case JobType::JOIN:
            case JobType::GROUP_ENTER:
                onEnter(job.player);
                break;
            case JobType::LEAVE:
                onLeave(job.player);
                break;
            case JobType::PACKET:
                if (job.player && job.packet) {
                    OnRecv(job.player, job.packet);
                }
                if (job.packet) job.packet->SubRef();
                break;
            }
            local.pop();
            processed++;
        }
        mLoopCount.fetch_add(1, std::memory_order_relaxed);
        return processed;
    }

    // 모니터링용 (다른 스레드에서 읽기 안전)
    int  GetPlayerCount() const { return mPlayerCount.load(std::memory_order_relaxed); }
    long GetPendingJobCount() const { return mMailbox.ApproxSize(); }

    // 1초 주기 스냅샷: SnapshotLoopFPS() 1회 호출 후 GetLoopFPS()로 반복 read.
    // 콘솔/모니터 양쪽이 같은 값을 보도록 분리.
    void SnapshotLoopFPS() { mLastLoopFPS = mLoopCount.exchange(0, std::memory_order_relaxed); }
    long GetLoopFPS() const { return mLastLoopFPS; }

protected:
    // 파생 클래스가 구현
    virtual void OnRecv(Player* player, Packet* packet) = 0;

    // 기본 입장: mPlayers 등록. 필요 시 override.
    virtual void onEnter(Player* player) {
        if (!player) return;
        mPlayers[player->GetSessionID()] = player;
        mPlayerCount.fetch_add(1, std::memory_order_relaxed);
    }

    // 기본 퇴장(실접속 해제): mPlayers 제거 + Player 풀에 반납.
    // 그룹 이동 race 시 — LEAVE 가 옛 그룹에 enqueue된 사이 player 가 새 그룹으로 이동했을 수 있음.
    // 이때 옛 그룹이 Free 하면 새 그룹의 GROUP_ENTER 가 use-after-free 가 되므로
    // mGroup 이 자기 자신이 아니면 새 그룹으로 LEAVE 를 forward 한다.
    virtual void onLeave(Player* player) {
        if (!player) return;
        Group* cur = player->mGroup.load(std::memory_order_acquire);
        if (cur != this) {
            if (cur) cur->Enqueue({ JobType::LEAVE, player, nullptr });
            else Player::playerMemoryPool.Free(player); // 안전 폴백
            return;
        }
        auto it = mPlayers.find(player->GetSessionID());
        if (it != mPlayers.end()) {
            mPlayers.erase(it);
            mPlayerCount.fetch_sub(1, std::memory_order_relaxed);
        }
        Player::playerMemoryPool.Free(player);
    }

    GroupServer* mServer;
    JobMailbox mMailbox;
    std::unordered_map<uint64_t, Player*> mPlayers; // 컨텐츠 스레드만 접근 → 락 없음

    std::atomic<int>  mPlayerCount{ 0 };
    std::atomic<long> mLoopCount{ 0 };
    long              mLastLoopFPS{ 0 }; // 1초 주기 메인 루프만 갱신
};
