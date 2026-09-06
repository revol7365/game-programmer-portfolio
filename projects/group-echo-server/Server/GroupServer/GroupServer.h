#pragma once

#include <unordered_map>
#include <memory>

#include "../Utils/CommonProtocol.h"
#include "../NetLib/NetServer.h"
#include "../NetLib/Define.h"
#include "Player.h"
#include "MonitorClient.h"
#include "GroupManager.h"

class GroupServer : public NetServer
{
    // sessionID → Player 빠른 조회용. 워커가 다중 read, OnClientJoin/Leave 가 write.
    std::unordered_map<uint64_t, Player*> mPlayerMap;
    SRWLOCK mPlayerMapLock;

    std::unique_ptr<GroupManager> mGroupManager;

    // 모니터링 서버 클라이언트
    MonitorClient mMonitorClient;

    bool OnConnectionRequest(int ip, int port) override;
    void OnClientJoin(uint64_t sessionID) override;
    void OnClientLeave(uint64_t sessionID) override;
    void OnRecv(uint64_t sessionID, Packet* packet) override;
    void OnSend(uint64_t sessionID, int sendsize) override;
    void OnError(int errorCode, const wchar_t* errorMessage) override;

    // 컨텐츠 스레드 — idx 별로 담당 그룹의 Update() 루프
    void contentThread(int threadIdx) override;

    // 5초 주기 타임아웃 검사 (감지/로깅만, disconnect 는 하지 않음 — 002 정책)
    void timeoutThread();
    void checkTimeOut();

    Player* findPlayerLocked(uint64_t sessionId);

public:
    GroupServer();
    virtual ~GroupServer();

    // 그룹 내부에서 호출
    void ReserveDisconnect(Player* player);
    void OnErrorPublic(int errorCode, const wchar_t* errorMessage) { OnError(errorCode, errorMessage); }

    int GetPlayerCount() const {
        return mGroupManager
            ? mGroupManager->GetLoginPlayerCount() + mGroupManager->GetEchoPlayerCount()
            : 0;
    }
    int GetLoginPlayerCount() const { return mGroupManager ? mGroupManager->GetLoginPlayerCount() : 0; }
    int GetEchoPlayerCount() const  { return mGroupManager ? mGroupManager->GetEchoPlayerCount() : 0; }

    void SendMonitorData() { mMonitorClient.SendMonitorData(); }
    void UpdateMonitorTPS() { mMonitorClient.UpdateTPS(); }

    // 그룹 LoopFPS 조회 (스냅샷은 PrintContentMonitor에서 1초 주기로 갱신)
    long GetLoginThreadFPS() const { return mGroupManager ? mGroupManager->GetLoginGroup()->GetLoopFPS() : 0; }
    long GetEchoThreadFPSTotal() const { return mGroupManager ? mGroupManager->GetEchoThreadFPSTotal() : 0; }

    void PrintContentMonitor() override;
    void LogContentMonitor() override;
};
