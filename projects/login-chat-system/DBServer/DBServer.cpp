#include "DBServer.h"

DBServer::DBServer(bool useTransaction) : bUseTransaction(useTransaction)
{
	if (!dbConn_.Connect(DB_HOST, DB_USER, DB_PASSWORD, DB_NAME, DB_PORT))
	{
		printf("[DBServer] DB 연결 실패! Error: %s\n", dbConn_.GetLastError());
		throw std::runtime_error("DBServer could not connect to MySQL");
	}
	else
	{
		printf("[DBServer] DB 연결 성공\n");
	}

    InitializeCriticalSection(&DBQueueCS);

    g_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    for (int i = 0; i < 2; i++)
    {
        mThreads.emplace_back(&DBServer::UpdateThread, this);
    }
    mThreads.emplace_back(&DBServer::DBWriterThread, this);
	mThreads.emplace_back(&DBServer::MonitorThread, this);

}

DBServer::~DBServer()
{
    if (g_hEvent != NULL) CloseHandle(g_hEvent);
    DeleteCriticalSection(&DBQueueCS);
}

void DBServer::Enqueue(DBQueryMessage msg)
{
    EnterCriticalSection(&DBQueueCS);
    DBQueue.push(std::move(msg));
    LeaveCriticalSection(&DBQueueCS);

    // DBWriterThread 깨우기
    SetEvent(g_hEvent);
}

void DBServer::UpdateThread()
{
	for (int i = 0; i < 3000; i++)
	{
		int roll = rand() % 10;  // 0~9

		if (roll == 0)       // 10% 레벨업
		{
			DBQueryLevelUp data;
			data.accountNo = rand() % 5 + 10000;
			data.level = rand() % 100 + 1;
			Enqueue(data);
		}
		else if (roll == 1)  // 10% 퀘스트 완료
		{
			DBQueryQuestComplete data;
			data.accountNo = rand() % 5 + 10000;
			data.questId = rand() % 50;
			Enqueue(data);
		}
		else if (roll == 2)  // 10% 아이템 구매
		{
			DBQueryItemBuy data;
			data.accountNo = rand() % 5 + 10000;
			data.itemId = rand() % 200;
			data.price = (rand() % 100 + 1) * 10;
			data.slot = rand() % 40;
			Enqueue(data);
		}
		else if (roll == 3)  // 10% 돈 증가
		{
			DBQueryMoneyAdd data;
			data.accountNo = rand() % 5 + 10000;
			data.money = (rand() % 100 + 1) * 100;
			data.why = rand() % 10;
			Enqueue(data);
		}
		else                 // 60% 거래
		{
			DBQueryItemTrade data;
			data.fromAccountNo = rand() % 5 + 10000;
			data.fromItemSlot = rand() % 40;
			data.toAccountNo = rand() % 5 + 10000 + data.fromAccountNo;
			data.toItemSlot = rand() % 40;
			data.tradeMoney = (rand() % 100 + 1) * 50;
			data.quantity = rand() % 10 + 1;
			Enqueue(data);
		}
	}

	// 이 스레드 완료
	if (mFinishedUpdateThreads.fetch_add(1) + 1 == 2)  // UpdateThread 2개 다 끝남
	{
		// 큐가 빌 때까지 대기 후 종료 플래그
		while (true)
		{
			EnterCriticalSection(&DBQueueCS);
			bool empty = DBQueue.empty();
			LeaveCriticalSection(&DBQueueCS);
			if (empty) break;
			Sleep(100);
		}
		bTerminate.store(true, std::memory_order_release);
		SetEvent(g_hEvent);  // DBWriterThread 깨우기
	}
}

void DBServer::DBWriterThread()
{
	while (1)
	{
		WaitForSingleObject(g_hEvent, INFINITE);

		while (true)
		{
			DBQueryMessage msg;
			bool hasMsg = false;

			EnterCriticalSection(&DBQueueCS);
			if (!DBQueue.empty())
			{
				msg = std::move(DBQueue.front());
				DBQueue.pop();
				hasMsg = true;
			}
			LeaveCriticalSection(&DBQueueCS);

			if (!hasMsg) break;

			std::visit([this](const auto& data)
				{
					using T = std::decay_t<decltype(data)>;

					if constexpr (std::is_same_v<T, DBQueryLevelUp>)
						HandleLevelUp(data);
					else if constexpr (std::is_same_v<T, DBQueryMoneyAdd>)
						HandleMoneyAdd(data);
					else if constexpr (std::is_same_v<T, DBQueryQuestComplete>)
						HandleQuestComplete(data);
					else if constexpr (std::is_same_v<T, DBQueryItemBuy>)
						HandleItemBuy(data);
					else if constexpr (std::is_same_v<T, DBQueryItemTrade>)
					{
						if (bUseTransaction)
							HandleItemTradeTransaction(data);
						else
							HandleItemTradeNoTransaction(data);
					}

					InterlockedIncrement(&lDBWriteCount);
					InterlockedIncrement(&lDBWriteTotal);
				}, msg);
		}

		if (bTerminate.load(std::memory_order_acquire)) break;
	}
}

bool DBServer::ExecuteCount(const char* query)
{
	bool ret = dbConn_.Execute(query);
	if (ret)
	{
		InterlockedIncrement(&lQueryCount);
		InterlockedIncrement(&lQueryTotal);
	}
	return ret;
}

//--------------------------------------------------------------------------------------------
// MonitorThread - 1초마다 TPS 및 큐 사이즈 출력
//--------------------------------------------------------------------------------------------
// MonitorThread

void DBServer::MonitorThread()
{
	DWORD startTime = timeGetTime();
	while (!bTerminate.load(std::memory_order_acquire))
	{
		Sleep(1000);

		long msgTps = InterlockedExchange(&lDBWriteCount, 0);
		long queryTps = InterlockedExchange(&lQueryCount, 0);

		EnterCriticalSection(&DBQueueCS);
		size_t queueSize = DBQueue.size();
		LeaveCriticalSection(&DBQueueCS);

		DWORD elapsed = (timeGetTime() - startTime) / 1000;

		printf("[%s] %3ds | MsgTPS: %4ld | QueryTPS: %4ld | Queue: %4zu | Total: %6ld\n",
			bUseTransaction ? "TXN" : "RAW",
			elapsed, msgTps, queryTps, queueSize, lQueryTotal);
	}

	DWORD totalTime = (timeGetTime() - startTime) / 1000;
	if (totalTime > 0)
	{
		printf("\n========== Result ==========\n");
		printf("  Mode     : %s\n", bUseTransaction ? "Transaction" : "No Transaction");
		printf("  Total    : %ld queries\n", lDBWriteTotal);
		printf("  Time     : %d sec\n", totalTime);
		printf("  Avg TPS  : %.1f\n", (double)lDBWriteTotal / totalTime);
		printf("============================\n\n");
	}
}


//===========================================================================================
// DB 쿼리 처리 함수들
//===========================================================================================
void DBServer::HandleLevelUp(const DBQueryLevelUp& data)
{
	char query[256];
	sprintf_s(query, sizeof(query),
		"UPDATE `player` SET `level` = %d WHERE `account_no` = %lld",
		data.level, data.accountNo);

	if (!ExecuteCount(query))
		printf("[HandleLevelUp] 실패 - AccountNo:%lld, Error: %s\n",
			data.accountNo, mysql_error(dbConn_.GetConnection()));
}

void DBServer::HandleMoneyAdd(const DBQueryMoneyAdd& data)
{
	char query[256];
	sprintf_s(query, sizeof(query),
		"UPDATE `player` SET `money` = `money` + %d WHERE `account_no` = %lld",
		data.money, data.accountNo);

	if (!ExecuteCount(query))
		printf("[HandleMoneyAdd] 실패 - AccountNo:%lld, Error: %s\n",
			data.accountNo, mysql_error(dbConn_.GetConnection()));
}

void DBServer::HandleQuestComplete(const DBQueryQuestComplete& data)
{
	char query[256];
	sprintf_s(query, sizeof(query),
		"INSERT INTO `quest_complete` (`account_no`, `quest_id`, `complete_date`) "
		"VALUES (%lld, %d, NOW())",
		data.accountNo, data.questId);

	if (!ExecuteCount(query))
		printf("[HandleQuestComplete] 실패 - AccountNo:%lld, QuestId:%d, Error: %s\n",
			data.accountNo, data.questId, mysql_error(dbConn_.GetConnection()));
}

void DBServer::HandleItemBuy(const DBQueryItemBuy& data)
{
	char query[256];
	sprintf_s(query, sizeof(query),
		"INSERT INTO `inventory` (`account_no`, `item_id`, `slot`, `price`) "
		"VALUES (%lld, %d, %d, %d)",
		data.accountNo, data.itemId, data.slot, data.price);

	if (!ExecuteCount(query))
		printf("[HandleItemBuy] 실패 - AccountNo:%lld, ItemId:%d, Error: %s\n",
			data.accountNo, data.itemId, mysql_error(dbConn_.GetConnection()));
}

void DBServer::HandleItemTradeTransaction(const DBQueryItemTrade& data)
{
	char query[512];

	if (!dbConn_.BeginTransaction())
	{
		printf("[HandleItemTrade-TXN] BEGIN 실패, Error: %s\n",
			mysql_error(dbConn_.GetConnection()));
		return;
	}

	// 구매자 돈 차감
	sprintf_s(query, sizeof(query),
		"UPDATE `player` SET `money` = `money` - %d WHERE `account_no` = %lld",
		data.tradeMoney, data.toAccountNo);
	if (!ExecuteCount(query))
	{
		printf("[HandleItemTrade-TXN] 구매자 돈 차감 실패, Error: %s\n",
			mysql_error(dbConn_.GetConnection()));
		dbConn_.Rollback();
		return;
	}

	// 판매자 돈 증가
	sprintf_s(query, sizeof(query),
		"UPDATE `player` SET `money` = `money` + %d WHERE `account_no` = %lld",
		data.tradeMoney, data.fromAccountNo);
	if (!ExecuteCount(query))
	{
		printf("[HandleItemTrade-TXN] 판매자 돈 증가 실패, Error: %s\n",
			mysql_error(dbConn_.GetConnection()));
		dbConn_.Rollback();
		return;
	}

	// 구매자 아이템 삽입
	sprintf_s(query, sizeof(query),
		"INSERT INTO `inventory` (`account_no`, `item_id`, `slot`, `price`) "
		"VALUES (%lld, %d, %lld, %d)",
		data.toAccountNo, 1, data.toItemSlot, data.tradeMoney);
	if (!ExecuteCount(query))
	{
		printf("[HandleItemTrade-TXN] 아이템 삽입 실패, Error: %s\n",
			mysql_error(dbConn_.GetConnection()));
		dbConn_.Rollback();
		return;
	}

	// 판매자 아이템 삭제
	sprintf_s(query, sizeof(query),
		"DELETE FROM `inventory` WHERE `account_no` = %lld AND `slot` = %lld",
		data.fromAccountNo, data.fromItemSlot);
	if (!ExecuteCount(query))
	{
		printf("[HandleItemTrade-TXN] 아이템 삭제 실패, Error: %s\n",
			mysql_error(dbConn_.GetConnection()));
		dbConn_.Rollback();
		return;
	}

	if (!dbConn_.Commit())
	{
		printf("[HandleItemTrade-TXN] COMMIT 실패, Error: %s\n",
			mysql_error(dbConn_.GetConnection()));
		dbConn_.Rollback();
		return;
	}
}

void DBServer::HandleItemTradeNoTransaction(const DBQueryItemTrade& data)
{
	char query[512];

	// 구매자 돈 차감
	sprintf_s(query, sizeof(query),
		"UPDATE `player` SET `money` = `money` - %d WHERE `account_no` = %lld",
		data.tradeMoney, data.toAccountNo);
	if (!ExecuteCount(query))
		printf("[HandleItemTrade-RAW] 구매자 돈 차감 실패, Error: %s\n",
			mysql_error(dbConn_.GetConnection()));

	// 판매자 돈 증가
	sprintf_s(query, sizeof(query),
		"UPDATE `player` SET `money` = `money` + %d WHERE `account_no` = %lld",
		data.tradeMoney, data.fromAccountNo);
	if (!ExecuteCount(query))
		printf("[HandleItemTrade-RAW] 판매자 돈 증가 실패, Error: %s\n",
			mysql_error(dbConn_.GetConnection()));

	// 구매자 아이템 삽입
	sprintf_s(query, sizeof(query),
		"INSERT INTO `inventory` (`account_no`, `item_id`, `slot`, `price`) "
		"VALUES (%lld, %d, %lld, %d)",
		data.toAccountNo, 1, data.toItemSlot, data.tradeMoney);
	if (!ExecuteCount(query))
		printf("[HandleItemTrade-RAW] 아이템 삽입 실패, Error: %s\n",
			mysql_error(dbConn_.GetConnection()));

	// 판매자 아이템 삭제
	sprintf_s(query, sizeof(query),
		"DELETE FROM `inventory` WHERE `account_no` = %lld AND `slot` = %lld",
		data.fromAccountNo, data.fromItemSlot);
	if (!ExecuteCount(query))
		printf("[HandleItemTrade-RAW] 아이템 삭제 실패, Error: %s\n",
			mysql_error(dbConn_.GetConnection()));
}
