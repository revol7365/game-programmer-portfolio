#include "DBServer.h"

#pragma comment(lib, "winmm.lib")

int main()
{
    printf("========== [TEST 1] Transaction Mode ==========\n");
    {
        DBServer server(true);
        // UpdateThread, DBWriterThread가 생성자에서 시작됨
        // 완료 대기
        for (auto& t : server.mThreads) {
            if (t.joinable()) t.join();
        }
        printf("Total: %ld\n\n", server.lDBWriteTotal);
    }

    printf("========== [TEST 2] No Transaction Mode ==========\n");
    {
        DBServer server(false);
        for (auto& t : server.mThreads) {
            if (t.joinable()) t.join();
        }
        printf("Total: %ld\n\n", server.lDBWriteTotal);
    }

    return 0;
}
