#pragma once

#include <Windows.h>
#include <unordered_map>
#include <cstdint>
#include <ctime>
#include "../include/mysql.h"

struct MonitorDataAccum {
	int64_t sum;
	int count;
	int minVal;
	int maxVal;
};

class DBConnecter {
public:
	DBConnecter();
	~DBConnecter();

	bool Connect(const char* host, const char* user, const char* password, const char* db, int port = 3306);
	void Disconnect();

	// 스레드 전용 - 락 없이 호출
	void AddData(int serverNo, int dataType, int dataValue);
	void Flush();

private:
	MYSQL* mConn;

	// Key: (serverNo << 16) | dataType
	std::unordered_map<int, MonitorDataAccum> mAccumMap;

	int mTotalInsertCount = 0;

	int MakeKey(int serverNo, int dataType) { return (serverNo << 16) | dataType; }
	bool EnsureTable(const char* tableName);

public:
	bool IsConnected() const { return mConn != nullptr; }
	int GetTotalInsertCount() const { return mTotalInsertCount; }
	MYSQL* GetConnection() const { return mConn; }
	MYSQL_RES* Query(const char* query);
};
