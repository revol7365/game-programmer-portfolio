#include "EchoGroup.h"
#include "GroupServer.h"
#include "GroupPacketGenerator.h"

void EchoGroup::OnRecv(Player* player, Packet* packet)
{
    // 이미 다른 그룹으로 이동한 뒤 늦게 도착한 패킷이 있을 수 있으나,
    // 현재 설계상 EchoGroup 이후의 그룹은 없으므로 추가 가드 불필요.
    WORD type;
    *packet >> type;

    player->UpdateHeartbeat();

    // type(2) 읽은 뒤 남은 payload 크기
    // (LOGIN 처럼 클라 실제 사이즈가 프로토콜 주석과 다를 수 있으므로
    //  실측 후 정확 매칭으로 좁힐 예정. 우선은 처음 mismatch 만 진단 로그)
    int payloadSize = packet->GetDataSize() - 5 - (int)sizeof(WORD);

    switch (type) {
    case en_PACKET_CS_GAME_REQ_ECHO:
        if (payloadSize != 16) {
            static std::atomic<int> s_log{0};
            if (s_log.fetch_add(1) < 5) {
                LOG(L"[DIAG] ECHO payload mismatch sid=%llu size=%d (expected=16)",
                    player->GetSessionID(), payloadSize);
            }
        }
        if (!handleEcho(player, packet)) mServer->ReserveDisconnect(player);
        break;
    case en_PACKET_CS_GAME_REQ_HEARTBEAT:
        if (payloadSize != 0) {
            static std::atomic<int> s_log{0};
            if (s_log.fetch_add(1) < 5) {
                LOG(L"[DIAG] HEARTBEAT payload mismatch sid=%llu size=%d (expected=0)",
                    player->GetSessionID(), payloadSize);
            }
        }
        if (!handleHeartbeat(player, packet)) mServer->ReserveDisconnect(player);
        break;
    case en_PACKET_CS_GAME_REQ_LOGIN:
        // 이미 로그인된 상태에서 또 LOGIN — 프로토콜 위반
        LOG(L"[ATTACK] Duplicate LOGIN sid=%llu", player->GetSessionID());
        mServer->ReserveDisconnect(player);
        break;
    default:
        LOG(L"[ATTACK] Unknown packet type 0x%x sid=%llu", type, player->GetSessionID());
        mServer->ReserveDisconnect(player);
        break;
    }
}

bool EchoGroup::handleEcho(Player* player, Packet* packet)
{
    uint64_t accountNo;
    LONGLONG sendTick;

    *packet >> accountNo;
    *packet >> sendTick;

    // [Note] 004 echo 는 "받은 데이터 그대로 돌려주는 단순 반사 테스트" 이므로
    // packet 의 accountNo 는 player 인증과 무관한 echo 페이로드에 불과하다.
    // (002 chat server 의 sectorMove/sendChatMessage 와 달리 비즈니스 행위 식별에
    //  쓰이지 않음 → mismatch 검증 불필요)

    Packet* resPacket = Packet::packetMemoryPool.Alloc();
    if (resPacket == nullptr) {
        mServer->OnErrorPublic(-1, L"PacketMemoryPool Error");
        return false;
    }
    resPacket->Init();

    GroupPacket::MakeGameResEcho(resPacket, accountNo, sendTick);

    bool sent = mServer->SendPacket(player->GetSessionID(), resPacket);
    resPacket->SubRef();
    return sent;
}

bool EchoGroup::handleHeartbeat(Player* /*player*/, Packet* /*packet*/)
{
    return true;
}
