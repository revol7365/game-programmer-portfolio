#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdlib>

typedef unsigned int uint;

inline const char* EnvText(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? value : fallback;
}

inline int EnvInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? std::atoi(value) : fallback;
}

constexpr int DEFAULT_PORT = 6001;
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
constexpr int MAX_THREADS = 5096;
constexpr int CONTENT_THREAD_CNT = 4;
constexpr const char* LAN_CLIENT_IP = "127.0.0.1";
constexpr int LAN_CLIENT_PORT = 7000;
constexpr int LAN_CLIENT_WORKER_THREAD = 1;
constexpr int LOGIN_SERVER_NO = 1;

inline const char* DB_HOST = EnvText("PORTFOLIO_DB_HOST", "127.0.0.1");
inline const char* DB_USER = EnvText("PORTFOLIO_DB_USER", "root");
inline const char* DB_PASSWORD = EnvText("PORTFOLIO_DB_PASSWORD", "");
inline const char* DB_NAME = EnvText("PORTFOLIO_DB_NAME", "portfolio_game");
inline int DB_PORT = EnvInt("PORTFOLIO_DB_PORT", 3306);

inline const char* REDIS_HOST = EnvText("PORTFOLIO_REDIS_HOST", "127.0.0.1");
inline int REDIS_PORT = EnvInt("PORTFOLIO_REDIS_PORT", 6379);

inline const WCHAR GAME_SERVER_IP[16] = L"127.0.0.1";
constexpr USHORT GAME_SERVER_PORT = 6003;
inline const WCHAR CHAT_SERVER_IP[16] = L"127.0.0.1";
constexpr USHORT CHAT_SERVER_PORT = 6000;
