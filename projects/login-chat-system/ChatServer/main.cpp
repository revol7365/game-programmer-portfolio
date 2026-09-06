#include <conio.h>

#include "ChatingServer/ChatServer.h"
#include "NetLib/CrashDump.h"

#pragma comment(lib, "winmm.lib")
int main() {

	timeBeginPeriod(1);

	DWORD lastControlTick = timeGetTime();
	DWORD lastSecondTick = timeGetTime();
	DWORD lastMonitorLogTick = timeGetTime() - 1800000;  // 시작 즉시 첫 로그 출력
	DWORD lastShrinkTick = timeGetTime();

	CrashDump crashHandler;

	ChatServer server;
	LOG(L"Server initialized and running.");

	while (!server.GetTerminate())
	{
		DWORD now = timeGetTime();
		if (now - lastControlTick >= 500)
		{
			/* verification: keyboard polling disabled */
			lastControlTick = now;
		}

		if (now - lastSecondTick >= 1000)
		{
			server.UpdateTPS();
			server.UpdateMonitorTPS();
			/* verification: console dashboard disabled */
			/* verification: monitor connection disabled */
			lastSecondTick += 1000;
		}

		// 2분(120000ms)마다 메모리풀 Shrink
		if (now - lastShrinkTick >= 120000)
		{
			Packet::packetMemoryPool.Shrink();
			lastShrinkTick += 120000;
		}

		// 30분(1800000ms)마다 모니터 정보를 로그에 기록
		if (now - lastMonitorLogTick >= 1800000)
		{
			server.LogMonitor();
			lastMonitorLogTick += 1800000;
		}
	}

	timeEndPeriod(1);

	return 0;
}
