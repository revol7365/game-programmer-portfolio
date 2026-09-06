#pragma once
#include <cwchar>
#include "../NetLib/Packet.h" 
#include <basetsd.h>
#include "../Utils/CommonProtocol.h"
#include <wtypes.h>

namespace ChatPacket {

    // 공통 초기화 루틴 (5바이트 예약 및 타입 기록)
    inline void InitPacket(Packet* pPacket, WORD type) {
        // pPacket->Clear();

        PACKET_HEADER hdr{};
        hdr.Code = 0;
        hdr.Len = 0;      // 나중에 최종 payload size로 채움
        hdr.RandKey = 0;  // encoding 시 채움
        hdr.CheckSum = 0; // 나중에 계산

        pPacket->PutData(&hdr, sizeof(hdr)); // 실제로 5바이트 채움
        *pPacket << type;           // 패킷 타입 (Payload 시작)
    }

    // 로그인 응답 (Server -> Client)
    inline void MakeLoginResLogin(Packet* pPacket, INT64 accountNo, BYTE status,
        const WCHAR* id, const WCHAR* nickname,
        const WCHAR* gameServerIP, USHORT gameServerPort,
        const WCHAR* chatServerIP, USHORT chatServerPort)
    {
        InitPacket(pPacket, en_PACKET_CS_LOGIN_RES_LOGIN);
        *pPacket << accountNo;
        *pPacket << status;
        pPacket->PutData((char*)id, sizeof(WCHAR) * 20);
        pPacket->PutData((char*)nickname, sizeof(WCHAR) * 20);
        pPacket->PutData((char*)gameServerIP, sizeof(WCHAR) * 16);
        *pPacket << gameServerPort;
        pPacket->PutData((char*)chatServerIP, sizeof(WCHAR) * 16);
        *pPacket << chatServerPort;
    }

    // 하트비트 요청
    inline void MakeChatReqHeartbeat(Packet* pPacket) {
        InitPacket(pPacket, en_PACKET_CS_CHAT_REQ_HEARTBEAT);
    }
}