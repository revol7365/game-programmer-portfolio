#pragma once

#include <cstdlib>

typedef unsigned int uint;

inline const char* ChatEnvText(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? value : fallback;
}

inline int ChatEnvInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? std::atoi(value) : fallback;
}

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

inline const char* REDIS_HOST = ChatEnvText("PORTFOLIO_REDIS_HOST", "127.0.0.1");
inline int REDIS_PORT = ChatEnvInt("PORTFOLIO_REDIS_PORT", 6379);
constexpr bool USE_REDIS_AUTH = true;

constexpr const char* LAN_CLIENT_IP = "127.0.0.1";
constexpr int LAN_CLIENT_PORT = 7000;
constexpr int LAN_CLIENT_WORKER_THREAD = 1;
constexpr int CHAT_SERVER_NO = 2;
constexpr int SECTOR_CNT_X = 50;
constexpr int SECTOR_CNT_Y = 50;
