#include <conio.h>

#include "LoginServer/LoginServer.h"
#include "NetLib/CrashDump.h"
#include "NetLib/Log.h"

#pragma comment(lib, "winmm.lib")
int main() {

	timeBeginPeriod(1);

	DWORD lastControlTick = timeGetTime();
	DWORD lastSecondTick = timeGetTime();
	DWORD lastSaveDBTick = timeGetTime();
	DWORD lastMonitorLogTick = timeGetTime();  // 30분 로그용
	DWORD lastHeartbeatTick = timeGetTime();
	DWORD lastShrinkTick = timeGetTime();

	CrashDump crashHandler;

	LoginServer server;
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

		// 5초마다 타임아웃 체크
		if (now - lastHeartbeatTick >= 5000)
		{
			server.PostHeartBeatCheck();
			lastHeartbeatTick += 5000;
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