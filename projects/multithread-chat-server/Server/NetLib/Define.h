#pragma once

typedef unsigned int uint;

constexpr int DEFAULT_PORT = 6000;
constexpr int LAN_PORT = 7000;
constexpr int DEFAULT_WORKER_THREAD = 4;
constexpr int DEFAULT_CONCURRENT = 0;
constexpr bool DEFAULT_NAGLE = false;
constexpr int DEFAULT_MAX_CONNECTION = 128;
constexpr int RECV_RINGBUFFER_SIZE = 1024 * 4;
constexpr int SEND_RINGBUFFER_SIZE = 128;
constexpr unsigned char PACKET_CODE = 0x77;
constexpr unsigned char PACKET_KEY = 0x32;
constexpr int MAX_SEND_PACKET = 64;
constexpr int INITNODESIZE = 400;
constexpr int MOVENODESIZE = 200;
constexpr int MAX_THREADS = 1024;

// Standalone protocol demo. The integrated project enables Redis auth.
constexpr const char* REDIS_HOST = "127.0.0.1";
constexpr int REDIS_PORT = 6379;
constexpr bool USE_REDIS_AUTH = false;

constexpr const char* LAN_CLIENT_IP = "127.0.0.1";
constexpr int LAN_CLIENT_PORT = 7000;
constexpr int LAN_CLIENT_WORKER_THREAD = 1;
constexpr int CHAT_SERVER_NO = 2;
constexpr int SECTOR_CNT_X = 50;
constexpr int SECTOR_CNT_Y = 50;
