#pragma once

#include <cstdlib>

inline const char* DbEnvText(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? value : fallback;
}

inline int DbEnvInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? std::atoi(value) : fallback;
}

inline const char* DB_HOST = DbEnvText("PORTFOLIO_DB_HOST", "127.0.0.1");
inline const char* DB_USER = DbEnvText("PORTFOLIO_DB_USER", "root");
inline const char* DB_PASSWORD = DbEnvText("PORTFOLIO_DB_PASSWORD", "");
inline const char* DB_NAME = DbEnvText("PORTFOLIO_DB_NAME", "portfolio_game");
inline int DB_PORT = DbEnvInt("PORTFOLIO_DB_PORT", 3306);
