#pragma once

#include <atomic>
#include <queue>
#include <thread>
#include <vector>
#include <stdexcept>
#include <windows.h>

#include "DBQuery.h"
#include "DBConnector.h"
#include "RuntimeConfig.h"

class DBServer {
public:
    explicit DBServer(bool useTransaction);
    ~DBServer();

    std::vector<std::thread> mThreads;
    const bool bUseTransaction;
    volatile long lTradeCount = 0;
    volatile long lOtherCount = 0;
    DWORD dwStartTime = 0;
    std::atomic<int> mFinishedUpdateThreads{0};
    std::atomic<bool> bTerminate{false};
    volatile long lDBWriteCount = 0;
    volatile long lDBWriteTotal = 0;
    volatile long lQueryCount = 0;
    volatile long lQueryTotal = 0;

private:
    void UpdateThread();
    void DBWriterThread();
    void MonitorThread();
    void Enqueue(DBQueryMessage msg);
    bool ExecuteCount(const char* query);
    void HandleLevelUp(const DBQueryLevelUp& data);
    void HandleMoneyAdd(const DBQueryMoneyAdd& data);
    void HandleQuestComplete(const DBQueryQuestComplete& data);
    void HandleItemBuy(const DBQueryItemBuy& data);
    void HandleItemTrade(const DBQueryItemTrade& data);
    void HandleItemTradeNoTransaction(const DBQueryItemTrade& data);
    void HandleItemTradeTransaction(const DBQueryItemTrade& data);

    HANDLE g_hEvent = NULL;
    std::queue<DBQueryMessage> DBQueue;
    CRITICAL_SECTION DBQueueCS;
    DBConnector dbConn_;
};
