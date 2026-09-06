#include "DBConnecter.h"
#include "../NetLib/Log.h"
#include <cstdio>

DBConnecter::DBConnecter()
	: mConn(nullptr)
{
}

DBConnecter::~DBConnecter()
{
	Disconnect();
}

bool DBConnecter::Connect(const char* host, const char* user, const char* password, const char* db, int port)
{
	mConn = mysql_init(nullptr);
	if (mConn == nullptr)
	{
		LOG(L"[DBLogger] mysql_init failed");
		return false;
	}

	if (mysql_real_connect(mConn, host, user, password, db, port, nullptr, 0) == nullptr)
	{
		LOG(L"[DBLogger] mysql_real_connect failed: %S", mysql_error(mConn));
		mysql_close(mConn);
		mConn = nullptr;
		return false;
	}

	LOG(L"[DBLogger] DB connected. host=%S, db=%S", host, db);
	return true;
}

void DBConnecter::Disconnect()
{
	if (mConn != nullptr)
	{
		mysql_close(mConn);
		mConn = nullptr;
		LOG(L"[DBLogger] DB disconnected");
	}
}

void DBConnecter::AddData(int serverNo, int dataType, int dataValue)
{
	int key = MakeKey(serverNo, dataType);

	auto it = mAccumMap.find(key);
	if (it == mAccumMap.end())
	{
		MonitorDataAccum accum;
		accum.sum = dataValue;
		accum.count = 1;
		accum.minVal = dataValue;
		accum.maxVal = dataValue;
		mAccumMap.insert({ key, accum });
	}
	else
	{
		it->second.sum += dataValue;
		it->second.count++;
		if (dataValue < it->second.minVal) it->second.minVal = dataValue;
		if (dataValue > it->second.maxVal) it->second.maxVal = dataValue;
	}
}

void DBConnecter::Flush()
{
	if (mConn == nullptr)
	{
		LOG(L"[DBLogger] Flush skipped: DB not connected");
		return;
	}

	// 축적 데이터를 swap으로 빠르게 가져옴
	std::unordered_map<int, MonitorDataAccum> snapshot;
	snapshot.swap(mAccumMap);

	if (snapshot.empty())
	{
		LOG(L"[DBLogger] Flush skipped: no data accumulated");
		return;
	}

	// 현재 월별 테이블 이름 생성
	time_t now = time(nullptr);
	struct tm t;
	localtime_s(&t, &now);

	char tableName[64];
	sprintf_s(tableName, "monitorlog_%04d%02d", t.tm_year + 1900, t.tm_mon + 1);

	// 각 축적 데이터를 INSERT
	for (auto& [key, accum] : snapshot)
	{
		int serverNo = (key >> 16) & 0xFFFF;
		int dataType = key & 0xFFFF;
		int64_t avgVal = accum.sum / accum.count;

		char query[512];
		sprintf_s(query,
			"INSERT INTO `%s` (`logtime`, `serverno`, `type`, `avr`, `min`, `max`) "
			"VALUES (NOW(), %d, %d, %lld, %d, %d)",
			tableName, serverNo, dataType, avgVal, accum.minVal, accum.maxVal);

		if (mysql_query(mConn, query) != 0)
		{
			unsigned int errNo = mysql_errno(mConn);

			// 1146 = 테이블이 없을 때 테이블 생성 후 재시도
			if (errNo == 1146)
			{
				if (EnsureTable(tableName))
				{
					if (mysql_query(mConn, query) != 0)
					{
						LOG(L"[DBLogger] INSERT retry failed: %S", mysql_error(mConn));
					}
					else
					{
						mTotalInsertCount++;
					}
				}
			}
			else
			{
				LOG(L"[DBLogger] INSERT failed (err=%u): %S", errNo, mysql_error(mConn));
			}
		}
		else
		{
			mTotalInsertCount++;
		}
	}

	LOG(L"[DBLogger] Flushed %d records to %S", (int)snapshot.size(), tableName);
}

bool DBConnecter::EnsureTable(const char* tableName)
{
	char query[256];
	sprintf_s(query, "CREATE TABLE `%s` LIKE `monitorlog_template`", tableName);

	if (mysql_query(mConn, query) != 0)
	{
		LOG(L"[DBLogger] CREATE TABLE failed: %S", mysql_error(mConn));
		return false;
	}

	LOG(L"[DBLogger] Created table %S from template", tableName);
	return true;
}

MYSQL_RES* DBConnecter::Query(const char* query)
{
	if (mConn == nullptr)
	{
		LOG(L"[DB] Query failed: not connected");
		return nullptr;
	}

	if (mysql_query(mConn, query) != 0)
	{
		LOG(L"[DB] Query failed (err=%u): %S", mysql_errno(mConn), mysql_error(mConn));
		return nullptr;
	}

	return mysql_store_result(mConn);
}
