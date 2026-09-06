#include "LoginGroup.h"
#include "GroupManager.h"
#include "GroupServer.h"
#include "GroupPacketGenerator.h"

void LoginGroup::OnRecv(Player* player, Packet* packet)
{
    // 이미 EchoGroup으로 이동한 뒤 늦게 도착한 패킷 → 드롭
    if (player->mGroup.load(std::memory_order_acquire) != this) {
        return;
    }

    WORD type;
    *packet >> type;

    // 로그인 전엔 LOGIN 만 허용
    if (type != en_PACKET_CS_GAME_REQ_LOGIN) {
        LOG(L"[ATTACK] Non-LOGIN packet before login. type=0x%x sid=%llu", type, player->GetSessionID());
        mServer->ReserveDisconnect(player);
        return;
    }

    // type(2) 읽은 뒤 남은 payload: AccountNo(8) + SessionKey(64).
    int payloadSize = packet->GetDataSize() - 5 - (int)sizeof(WORD);
    if (payloadSize != 72) {
        LOG(L"[ATTACK] LOGIN invalid payload sid=%llu size=%d", player->GetSessionID(), payloadSize);
        mServer->ReserveDisconnect(player);
        return;
    }

    if (!handleLogin(player, packet)) {
        mServer->ReserveDisconnect(player);
    }
}

bool LoginGroup::handleLogin(Player* player, Packet* packet)
{
    uint64_t accountNo;
    char sessionKey[64]{};

    *packet >> accountNo;
    packet->GetData(sessionKey, sizeof(sessionKey));

    Packet* resPacket = Packet::packetMemoryPool.Alloc();
    if (resPacket == nullptr) {
        mServer->OnErrorPublic(-1, L"PacketMemoryPool Error");
        return false;
    }
    resPacket->Init();

    player->SetAccountNo(accountNo);
    player->SetSessionKey(sessionKey);
    player->OnLogin();

    GroupPacket::MakeGameResLogin(resPacket, true, player->GetAccountNo());

    // === 그룹 이동 (응답 송신 전) ===
    EchoGroup* target = mManager->GetEchoGroupFor(player->GetSessionID());

    // 1) target 큐에 GROUP_ENTER 먼저 enqueue — 이래야 race로 forward된 LEAVE가
    //    GROUP_ENTER 보다 뒤에 도착하도록 순서 보장됨.
    target->Enqueue({ JobType::GROUP_ENTER, player, nullptr });

    // 2) mGroup 포인터 교체 — 이후 워커가 보내는 패킷은 EchoGroup 큐로 들어감.
    //    동시에 옛 LoginGroup 큐에 남아있는 LEAVE/PACKET은 forward되거나 드롭됨.
    player->mGroup.store(target, std::memory_order_release);

    // 3) LoginGroup 맵에서 제거 (수동: onLeave를 거치지 않음 — Free 안 함)
    auto it = mPlayers.find(player->GetSessionID());
    if (it != mPlayers.end()) {
        mPlayers.erase(it);
        mPlayerCount.fetch_sub(1, std::memory_order_relaxed);
    }

    // 4) 응답 송신 (LoginGroup 컨텐츠 스레드 컨텍스트)
    bool sent = mServer->SendPacket(player->GetSessionID(), resPacket);
    if (!sent) {
        LOG(L"[LoginGroup] SendPacket Fail accountNo=%llu sid=%llu", accountNo, player->GetSessionID());
    }
    resPacket->SubRef();
    return true; // 송신 실패 시에도 disconnect는 하지 않음 — OnClientLeave 가 LEAVE 잡을 EchoGroup으로 보냄
}
