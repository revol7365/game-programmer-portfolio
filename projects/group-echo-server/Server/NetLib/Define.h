#pragma once

// chrono <-> Windows.h min/max macro 충돌 방지
#ifndef NOMINMAX
#define NOMINMAX
#endif

typedef unsigned int uint;

constexpr int DEFAULT_PORT = 16003;
constexpr int LAN_PORT     = 7000;
constexpr int DEFAULT_WORKER_THREAD = 4; // 0 = 실행 환경 CPU에 맞게 자동 설정
constexpr int DEFAULT_CONCURRENT = 0;   // 0 = 실행 환경 CPU에 맞게 자동 설정
constexpr bool DEFAULT_NAGLE = true;
constexpr int DEFAULT_MAX_CONNECTION = 128; // 최대 동접 6000 + 버스트/재접속 여유 1000

constexpr int RECV_RINGBUFFER_SIZE = 1024;
constexpr int SEND_RINGBUFFER_SIZE = 256;

constexpr unsigned char PACKET_CODE = 0x77; // 헤더키
constexpr unsigned char PACKET_KEY = 0x32; // 암호화 고정키

// OverLapped
constexpr int MAX_SEND_PACKET = 32;

// MemoryPool 설정
constexpr int INITNODESIZE = 400;
constexpr int MOVENODESIZE = 200;
constexpr int MAX_THREADS = 256;

// LanClient (모니터링 서버 접속용)
constexpr const char* LAN_CLIENT_IP = "127.0.0.1";
constexpr int LAN_CLIENT_PORT = 7000;
constexpr int LAN_CLIENT_WORKER_THREAD = 1;

// 서버 고유 번호 (모니터링 서버 로그인용)
constexpr int GAME_SERVER_NO = 3;

// 액터(그룹) 패턴 설정
constexpr int ECHO_SHARD_COUNT = 4; // EchoGroup 샤드 개수 (sessionID 해시로 분배)
