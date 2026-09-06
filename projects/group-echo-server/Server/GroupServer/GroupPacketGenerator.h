#pragma once
#include "../NetLib/Packet.h"
#include "../Utils/CommonProtocol.h"
#include <basetsd.h>
#include <wtypes.h>

namespace GroupPacket {

    // 공통 초기화 루틴 (헤더 5바이트 예약 및 타입 기록)
    inline void InitPacket(Packet* pPacket, WORD type) {
        PACKET_HEADER hdr{};
        hdr.Code = 0;
        hdr.Len = 0;
        hdr.RandKey = 0;
        hdr.CheckSum = 0;

        pPacket->PutData(&hdr, sizeof(hdr));
        *pPacket << type;
    }

    // 1. 로그인 응답 (Server -> Client)
    inline void MakeGameResLogin(Packet* pPacket, BYTE status, INT64 accountNo) {
        InitPacket(pPacket, en_PACKET_CS_GAME_RES_LOGIN);

        *pPacket << status;
        *pPacket << accountNo;
    }

    // 2. 에코 응답 (Server -> Client) - REQ를 그대로 돌려줌
    inline void MakeGameResEcho(Packet* pPacket, INT64 accountNo, LONGLONG sendTick) {
        InitPacket(pPacket, en_PACKET_CS_GAME_RES_ECHO);

        *pPacket << accountNo;
        *pPacket << sendTick;
    }
}
