#pragma once

#include <vector>
#include <queue>
#include <unordered_map>

#include "../Utils/CommonProtocol.h"
#include "../NetLib/NetServer.h"
#include "../NetLib/Define.h"
#include "../DBConnect/DBConnecter.h"
#include "../DBConnect/RedisConnecter.h"
#include "Player.h"
#include "MonitorClient.h"
#include "Job.h"

class LoginServer : public NetServer
{
	std::unordered_map<uint64_t, Player*> mPlayerMap[CONTENT_THREAD_CNT];
	DBConnecter mDBConn[CONTENT_THREAD_CNT];
	RedisConnecter mRedis;

	HANDLE mContentocpHandles[CONTENT_THREAD_CNT];
	HANDLE hTimer;
	HANDLE ContentQueueEvent;

	std::atomic<int> mPlayerCount{ 0 }; // 모니터링용 (Lock 없이 조회)

	// 모니터링 서버 클라이언트
	MonitorClient mMonitorClient;

	bool OnConnectionRequest(int ip, int port) override; // Accept 이후 호출
	void OnClientJoin(uint64_t sessionID) override; // Accept 후 접속처리 완료 후 호출.
	void OnClientLeave(uint64_t sessionID) override; // Release 후 호출
	void OnRecv(uint64_t sessionID, Packet* packet) override; // 패킷 수신 완료 후
	void OnSend(uint64_t sessionID, int sendsize) override; // 패킷 송신 완료 후
	void OnError(int errorCode, const wchar_t* errorMessage) override;
	void contentThread(int threadIdx) override;

	// 패킷 처리
	bool packetProc(int threadIdx, Player& player, Packet* packet);
	bool login(int threadIdx, Player& player, Packet* packet);

	// 플레이어 관리
	void removePlayer(Player* player);
	void reserveDeletePlayer(Player* player);
	Player* findPlayer(uint64_t sessionId);

	// Utils
	int GetTargetThread(uint64_t sessionID) { return (int)(sessionID % CONTENT_THREAD_CNT); }


public:
	LoginServer();

	int GetPlayerCount() const { return mPlayerCount.load(); }
	void SendMonitorData() { mMonitorClient.SendMonitorData(); }
	void UpdateMonitorTPS() { mMonitorClient.UpdateTPS(); }

	void PostHeartBeatCheck() {
		for (int i = 0; i < CONTENT_THREAD_CNT; ++i) {
			Job* job = Job::jobMemoryPool.Alloc();
			job->SetType(EJobType::CHECK_HEAR_BEAT);
			PostQueuedCompletionStatus(mContentocpHandles[i], 0, 0, (LPOVERLAPPED)job);
		}
	}

	void PrintContentMonitor() override
	{
		ConsolePrintLine(L"  PlayerMap Total : %-10d", mPlayerCount.load());
		ConsolePrintLine(L"  Monitor Server  : %-14s Send TPS : %-10ld", mMonitorClient.IsConnected() ? L"Connected" : L"Disconnected", mMonitorClient.GetSendTPS());
		ConsolePrintLine(L"  Redis Server    : %-14s", mRedis.IsConnected() ? L"Connected" : L"Disconnected");
	}

	void LogContentMonitor() override
	{
		LOG(L"[Monitor] PlayerMap Total: %d, MonitorServer: %s", mPlayerCount.load(), mMonitorClient.IsConnected() ? L"Connected" : L"Disconnected");
	}

	virtual ~LoginServer();
};