#pragma once

#include <vector>
#include <memory>
#include <cstdint>

#include "LoginGroup.h"
#include "EchoGroup.h"

class GroupServer;

class GroupManager {
public:
    GroupManager(GroupServer* server, int echoShardCount);
    ~GroupManager() = default;

    LoginGroup* GetLoginGroup() { return mLoginGroup.get(); }
    const LoginGroup* GetLoginGroup() const { return mLoginGroup.get(); }

    // sessionID 해시로 EchoGroup 샤드 결정
    EchoGroup* GetEchoGroupFor(uint64_t sessionID) {
        return mEchoShards[sessionID % mEchoShards.size()].get();
    }

    // 컨텐츠 스레드 idx → 담당 그룹
    //   idx 0      : LoginGroup
    //   idx 1..N   : EchoGroup 샤드
    Group* GetGroupByThreadIdx(int idx) {
        if (idx == 0) return mLoginGroup.get();
        int shardIdx = idx - 1;
        if (shardIdx < 0 || shardIdx >= (int)mEchoShards.size()) return nullptr;
        return mEchoShards[shardIdx].get();
    }

    int GetTotalGroupCount() const { return 1 + (int)mEchoShards.size(); }

    // 모니터링
    int  GetLoginPlayerCount() const { return mLoginGroup->GetPlayerCount(); }
    int  GetEchoPlayerCount() const {
        int sum = 0;
        for (auto& s : mEchoShards) sum += s->GetPlayerCount();
        return sum;
    }
    int  GetEchoShardCount() const { return (int)mEchoShards.size(); }
    EchoGroup* GetEchoShard(int idx) { return mEchoShards[idx].get(); }

    // 1초마다 메인 루프에서 1회 호출 — 모든 그룹의 LoopFPS 캐시 갱신
    void SnapshotAllLoopFPS() {
        mLoginGroup->SnapshotLoopFPS();
        for (auto& s : mEchoShards) s->SnapshotLoopFPS();
    }
    // 모든 EchoShard LoopFPS 합산
    long GetEchoThreadFPSTotal() const {
        long sum = 0;
        for (auto& s : mEchoShards) sum += s->GetLoopFPS();
        return sum;
    }

private:
    GroupServer* mServer;
    std::unique_ptr<LoginGroup> mLoginGroup;
    std::vector<std::unique_ptr<EchoGroup>> mEchoShards;
};
