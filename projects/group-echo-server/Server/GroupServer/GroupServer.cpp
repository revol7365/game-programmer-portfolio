#include "GroupServer.h"
#include "Group.h"

GroupServer::GroupServer()
    : NetServer(), mMonitorClient(this)
{
    InitializeSRWLock(&mPlayerMapLock);

    // 그룹 매니저 생성 (LoginGroup 1개 + EchoGroup N개)
    mGroupManager = std::make_unique<GroupManager>(this, ECHO_SHARD_COUNT);

    // 컨텐츠 스레드 가동 — accept 시작 전에 먼저 띄워둠
    int groupCount = mGroupManager->GetTotalGroupCount();
    for (int i = 0; i < groupCount; ++i) {
        mThreads.emplace_back([this, i]() { this->contentThread(i); });
    }

    // 타임아웃 감지 스레드
    mThreads.emplace_back(&GroupServer::timeoutThread, this);

    // 워커/Accept 스레드 가동
    Start();
}

GroupServer::~GroupServer()
{
}

bool GroupServer::OnConnectionRequest(int /*ip*/, int /*port*/)
{
    return true;
}

void GroupServer::OnClientJoin(uint64_t sessionID)
{
    mJobCount.fetch_add(1);

    Player* player = Player::playerMemoryPool.Alloc();
    player->Clear();
    player->SetSessionID(sessionID);
    player->OnConnect();

    // 첫 그룹 = LoginGroup
    LoginGroup* loginGroup = mGroupManager->GetLoginGroup();
    player->mGroup.store(loginGroup, std::memory_order_release);

    bool inserted = false;
    AcquireSRWLockExclusive(&mPlayerMapLock);
    auto [it, ins] = mPlayerMap.insert({ sessionID, player });
    inserted = ins;
    if (inserted) {
        // JOIN 을 락 안에서 enqueue — OnClientLeave 의 LEAVE 와 순서 보장
        loginGroup->Enqueue({ JobType::JOIN, player, nullptr });
    }
    ReleaseSRWLockExclusive(&mPlayerMapLock);

    if (!inserted) {
        LOG(L"[CLIENT_JOIN] DUPLICATE sessionID=%llu newPlayer=%p", sessionID, player);
        Player::playerMemoryPool.Free(player);
    }
}

void GroupServer::OnClientLeave(uint64_t sessionID)
{
    Player* player = nullptr;

    AcquireSRWLockExclusive(&mPlayerMapLock);
    auto it = mPlayerMap.find(sessionID);
    if (it != mPlayerMap.end()) {
        player = it->second;
        mPlayerMap.erase(it);
        // LEAVE 를 락 안에서 enqueue — JOIN/PACKET 다음 순서로 보장
        Group* g = player->mGroup.load(std::memory_order_acquire);
        if (g) g->Enqueue({ JobType::LEAVE, player, nullptr });
    }
    ReleaseSRWLockExclusive(&mPlayerMapLock);

    if (player && player->mGroup.load(std::memory_order_acquire) == nullptr) {
        // 그룹이 nullptr 이라면 어디로도 보낼 수 없음 — 직접 Free (예외 경로)
        Player::playerMemoryPool.Free(player);
    }
}

void GroupServer::OnRecv(uint64_t sessionID, Packet* packet)
{
    mRecvCount++;
    mJobCount.fetch_add(1);

    AcquireSRWLockShared(&mPlayerMapLock);
    Player* player = findPlayerLocked(sessionID);
    if (player != nullptr) {
        Group* g = player->mGroup.load(std::memory_order_acquire);
        if (g) {
            // packet의 refcount 1을 그룹 큐로 인계 — 그룹이 처리 후 SubRef
            g->Enqueue({ JobType::PACKET, player, packet });
        }
        else {
            packet->SubRef();
        }
    }
    else {
        WORD headerType = 0;
        *packet >> headerType;
        LOG(L"[PACKET] Player not found! sessionID: %llu, packetType: %d", sessionID, headerType);
        packet->SubRef();
    }
    ReleaseSRWLockShared(&mPlayerMapLock);
}

void GroupServer::OnSend(uint64_t /*sessionID*/, int /*sendsize*/)
{
    mSendCount++;
}

void GroupServer::OnError(int /*errorCode*/, const wchar_t* /*errorMessage*/)
{
}

// 그룹의 컨텐츠 스레드 루프
void GroupServer::contentThread(int threadIdx)
{
    Group* group = mGroupManager->GetGroupByThreadIdx(threadIdx);
    if (!group) {
        LOG(L"[contentThread] No group for idx %d", threadIdx);
        return;
    }

    while (!GetTerminate()) {
        int processed = group->Update();
        if (processed == 0) {
            Sleep(1); // 빈 큐면 양보
        }
    }

    // 종료 시 잔여 잡 정리 (Player 등 누수 방지)
    group->Update();
}

void GroupServer::timeoutThread()
{
    while (!GetTerminate()) {
        checkTimeOut();
        Sleep(5000); // 5초 주기로 검사
    }
}

void GroupServer::checkTimeOut()
{
    AcquireSRWLockShared(&mPlayerMapLock);
    for (auto& kv : mPlayerMap) {
        Player* player = kv.second;
        if (player->GetIsWillDelete()) continue;
        auto snap = player->CheckTimeOutSnapshot();
        if (snap.expired && snap.firstReport) {
            // 002 정책: 감지/로깅만 수행, disconnect 는 하지 않음.
            // heartbeat/OnLogin/OnConnect 에서 mTimeoutLogged 가 리셋되므로
            // 유휴 상태가 재시작되면 다음 진입 시 다시 1회 보고됨.
            LOG(L"[Timeout] sid=%llu accountNo=%llu loggedIn=%d idleMs=%llu",
                player->GetSessionID(), player->GetAccountNo(),
                snap.loggedIn ? 1 : 0, snap.idleMs);
        }
    }
    ReleaseSRWLockShared(&mPlayerMapLock);
}

Player* GroupServer::findPlayerLocked(uint64_t sessionId)
{
    auto it = mPlayerMap.find(sessionId);
    if (it != mPlayerMap.end()) return it->second;
    return nullptr;
}

void GroupServer::ReserveDisconnect(Player* player)
{
    if (!player) return;
    if (player->GetIsWillDelete()) return;
    player->SetWillDelete();
    DisConnect(player->GetSessionID());
}

void GroupServer::PrintContentMonitor()
{
    // 1초 주기 LoopFPS 스냅샷 — 콘솔/모니터 송신 양쪽이 같은 값을 본다
    mGroupManager->SnapshotAllLoopFPS();

    int loginCount = GetLoginPlayerCount();
    int echoCount = GetEchoPlayerCount();
    ConsolePrintLine(L"  Player Total : %-6d  Login : %-6d  Echo : %-6d",
        loginCount + echoCount, loginCount, echoCount);

    // LoginGroup 진단: 대기 중인 잡 수 + 초당 Update 루프 수
    Group* lg = mGroupManager->GetLoginGroup();
    ConsolePrintLine(L"  LoginGroup   : Pending=%-6ld  LoopFPS=%-6ld",
        lg->GetPendingJobCount(), lg->GetLoopFPS());

    // 샤드별 인원 + 진단
    int shardCount = mGroupManager->GetEchoShardCount();
    for (int i = 0; i < shardCount; ++i) {
        EchoGroup* eg = mGroupManager->GetEchoShard(i);
        ConsolePrintLine(L"    EchoShard[%d] : Player=%-6d  Pending=%-6ld  LoopFPS=%-6ld",
            i, eg->GetPlayerCount(), eg->GetPendingJobCount(), eg->GetLoopFPS());
    }

    ConsolePrintLine(L"  Monitor Server  : %-14s Send TPS : %-10ld",
        mMonitorClient.IsConnected() ? L"Connected" : L"Disconnected",
        mMonitorClient.GetSendTPS());
}

void GroupServer::LogContentMonitor()
{
    int loginCount = GetLoginPlayerCount();
    int echoCount = GetEchoPlayerCount();
    LOG(L"[Monitor] Login: %d, Echo: %d, Total: %d, MonitorServer: %s",
        loginCount, echoCount, loginCount + echoCount,
        mMonitorClient.IsConnected() ? L"Connected" : L"Disconnected");
}
