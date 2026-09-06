#pragma once
#include <cwchar>
#include "../NetLib/Packet.h" // Packet, WORD 등 정의 포함
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

    // 1. 로그인 응답 (Server -> Client)
    inline void MakeChatResLogin(Packet* pPacket, BYTE status, INT64 accountNo) {
        InitPacket(pPacket, en_PACKET_CS_CHAT_RES_LOGIN);

        *pPacket << status;
        *pPacket << accountNo;
    }

    // 2. 섹터 이동 결과
    inline void MakeChatSectorMove(Packet* pPacket, INT64 accountNo, WORD sectorX, WORD sectorY) {
        InitPacket(pPacket, en_PACKET_CS_CHAT_RES_SECTOR_MOVE);

        *pPacket << accountNo;
        *pPacket << sectorX;
        *pPacket << sectorY;
    }

    // 3. 채팅 보내기 응답 / 브로드캐스트 (Server -> Client)
    inline void MakeChatResMessage(Packet* pPacket, INT64 accountNo, const WCHAR* id, const WCHAR* nickname, WORD messageLen, const WCHAR* message) {
        // 1. 패킷 초기화
        InitPacket(pPacket, en_PACKET_CS_CHAT_RES_MESSAGE);

        // 2. 고정 크기 데이터 넣기
        *pPacket << accountNo;

        // 3. ID 및 Nickname 처리 (항상 40바이트 고정 크기 확보)
        WCHAR tempId[20] = { 0, };
        WCHAR tempNick[20] = { 0, };

        // 안전하게 최대 20자까지만 복사 (나머지는 0으로 채워짐)
        wcsncpy_s(tempId, id, 20);
        wcsncpy_s(tempNick, nickname, 20);

        pPacket->PutData(tempId, sizeof(tempId));     // 40바이트
        pPacket->PutData(tempNick, sizeof(tempNick)); // 40바이트

        // 4. 가변 길이 메시지 처리
        *pPacket << messageLen;
        if (messageLen > 0 && message != nullptr) {
            pPacket->PutData(message, messageLen);
        }

    }

    // 4. 하트비트 요청
    inline void MakeChatReqHeartbeat(Packet* pPacket) {
        InitPacket(pPacket, en_PACKET_CS_CHAT_REQ_HEARTBEAT);
    }
}