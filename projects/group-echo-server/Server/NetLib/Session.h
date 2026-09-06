#pragma once
#include <winsock2.h>
#include <iostream>
#include <atomic>
#include <vector>
#include <stack>
#include <map>
#include "RingBuffer.h"
#include "Define.h"
#include "Packet.h"

#pragma pack(push, 1)
union SessionControl {
    struct {
        uint32_t  ioCount;      // 하위 32비트
        uint32_t  releaseFlag;  // 상위 32비트
    };
    uint64_t  full;       // 전체 64비트
};
#pragma pack(pop)

static constexpr uint32_t FLAG_RELEASE = 1u << 0; // 종료 요청(0/1)
static constexpr uint32_t FLAG_CLEANUP = 1u << 1; // 정리 담당 획득(0/1)

enum class ESessionState {
    Free,       // 스택에 있는 상태 (사용 가능)
    Connected,  // 실제 유저가 접속 중
    Disconnecting // 접속 해제 중
};

// OVERLAPPED 구조체를 확장하여 I/O 타입을 명시
enum class EOperation : uint8_t
{
    RECV,
    SEND,
    RELEASE
};

// Session 클래스 선언부를 OverlappedBase 정의 이전에 미리 선언
class Session;

struct OverlappedBase {
    OVERLAPPED overlapped;
    EOperation type;
};

// 수신용
struct RecvOverlapped : public OverlappedBase {
    WSABUF      wsaBuf[2];
    RecvOverlapped() : OverlappedBase{ {}, EOperation::RECV }
    {
        wsaBuf[0].buf = nullptr;
        wsaBuf[0].len = 0;

        wsaBuf[1].buf = nullptr;
        wsaBuf[1].len = 0;
    }
};

// 송신용
struct SendOverlapped : public OverlappedBase {
    WSABUF wsaBuf[MAX_SEND_PACKET];
    Packet* sentPackets[MAX_SEND_PACKET];
    int packetCount;
    SendOverlapped() : OverlappedBase{ {}, EOperation::SEND }, packetCount(0) {}

    void Clear() {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
        packetCount = 0;
    }
};

class Session
{
public:
    SOCKET          socket;
    uint64_t   id;
    RecvOverlapped    recvOverlapped;
    SendOverlapped    sendOverlapped;
    OverlappedBase    releaseOverlapped;

    RingBuffer<char>    recvRingBuffer;
    RingBuffer<Packet*>  sendRingBuffer;

    std::atomic<int> sendFlag;      // 0: send 중 아님, 1: send 중
    std::atomic<uint64_t> control{ 0 }; // 하위 ioCount, 상위 releaseFlag
    std::atomic<ESessionState> state = ESessionState::Free;
    CRITICAL_SECTION sessionSendRingBuffer_cs;

public:
    Session()
        : socket(INVALID_SOCKET),
        id(0),
        recvOverlapped(RecvOverlapped()),
        sendOverlapped(SendOverlapped()),
        releaseOverlapped(OverlappedBase{ {}, EOperation::RELEASE }),
        recvRingBuffer(RingBuffer<char>(RECV_RINGBUFFER_SIZE)),
        sendRingBuffer(RingBuffer<Packet*>(SEND_RINGBUFFER_SIZE)),
        control(0)
    {
        //printf("[Session Created] Socket: %d(%d)\n", (int)socket, id);
        InitializeCriticalSection(&sessionSendRingBuffer_cs);
    }

    ~Session()
    {
        //printf("[Session Destroyed] Socket: %d(%d)\n", (int)socket, id);
        DeleteCriticalSection(&sessionSendRingBuffer_cs);
    }

    bool IncreaseIoCount() {
        uint64_t expected = control.load();
        while (true) {
            SessionControl exp{}; exp.full = expected;
            if (exp.releaseFlag & FLAG_RELEASE) return false;  // 종료 중이면 I/O 금지

            SessionControl des = exp;
            des.ioCount++;

            if (control.compare_exchange_weak(expected, des.full))
                return true;
        }
    }

    bool TrySetReleaseFlag(uint32_t& ioCountBefore)
    {
        uint64_t expected = control.load();

        while (true)
        {
            SessionControl exp{};
            exp.full = expected;
            if (exp.releaseFlag & FLAG_RELEASE) return false; // 이미 종료 요청됨

            ioCountBefore = exp.ioCount;

            SessionControl desired = exp;
            desired.releaseFlag |= FLAG_RELEASE; // 종료 요청 비트 켜기

            if (control.compare_exchange_weak(expected, desired.full))
            {
                return true; // 0->1 성공
            }
            // 실패하면 expected가 최신값으로 갱신되어 다시 루프
        }
    }

    bool Init()
    {
        // 소켓 및 ID 초기화
        this->socket = INVALID_SOCKET;
        this->id = 0; // 세션 ID도 반드시 초기화

        // 통합된 control 변수 초기화 (ioCount = 0, releaseFlag = 0)
        // 64비트 전체를 0으로 만들면 두 플래그가 동시에 초기화
        control.store(0);

        // 송신 중 플래그 초기화 (WSASend 중복 호출 방지용)
        sendFlag.store(0);

        // 링 버퍼 초기화
        recvRingBuffer.ClearBuffer();
        sendRingBuffer.ClearBuffer();

        // Overlapped 구조체 및 전송 정보 초기화
        memset(&recvOverlapped.overlapped, 0, sizeof(OVERLAPPED));
        memset(&sendOverlapped.overlapped, 0, sizeof(OVERLAPPED));
        memset(&releaseOverlapped.overlapped, 0, sizeof(OVERLAPPED));
        releaseOverlapped.type = EOperation::RELEASE;

        // 이전에 전송 중이었던 패킷 카운트도 0으로 초기화
        sendOverlapped.packetCount = 0;
        this->state = ESessionState::Free;

        return true;
    }

};