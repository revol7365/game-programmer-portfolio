#pragma once

#include <vector>
#include <queue>
#include <unordered_map>

#include "../Utils/CommonProtocol.h"
#include "../NetLib/NetServer.h"
#include "../NetLib/Define.h"
#include "../DBConnect/RedisConnecter.h"
#include "Player.h"
#include "Sector.h"
#include "MonitorClient.h"

class ChatServer : public NetServer
{
	std::unordered_map<uint64_t, Player*> mPlayerMap;
	SRWLOCK mPlayerMapLock;
	std::atomic<int> mPlayerCount{ 0 }; // 모니터링용 (Lock 없이 조회)

	// 모니터링 서버 클라이언트
	MonitorClient mMonitorClient;
	RedisConnecter mRedis;

	// 월드맵 캐릭터 섹터
	Sector mSector[SECTOR_CNT_Y][SECTOR_CNT_X];

	SectorAround mSectorAroundTable[SECTOR_CNT_Y][SECTOR_CNT_X]; // 섹터의 주변 섹터

	bool OnConnectionRequest(int ip, int port) override; // Accept 이후 호출
	void OnClientJoin(uint64_t sessionID) override; // Accept 후 접속처리 완료 후 호출.
	void OnClientLeave(uint64_t sessionID) override; // Release 후 호출
	void OnRecv(uint64_t sessionID, Packet* packet) override; // 패킷 수신 완료 후
	void OnSend(uint64_t sessionID, int sendsize) override; // 패킷 송신 완료 후
	void OnError(int errorCode, const wchar_t* errorMessage) override;

	// 패킷 처리
	bool packetProc(Player& player, Packet* packet);
	bool login(Player& player, Packet* packet);
	bool sectorMove(Player& player, Packet* packet);
	bool sendChatMessage(Player& player, Packet* packet);
	bool heartBeat(Player& player, Packet* packet);

	// 섹터테이블 설정
	void movePlayerSector(Player* player, WORD newSectorX, WORD newSectorY);
	void erasePlayerFromSector(Player* player);
	void reserveDeletePlayer(Player* player, DisconnectReason reason = DisconnectReason::InvalidPacket);
	Player* findPlayer(uint64_t sessionId);

	// Send
	void broadcast(Player& player, Packet* pPacket);

	// Utils
	void initSectorAroundTable();

	// TimeOut Check
	void timeoutThread();
	void CheckTimeOut();

public:
	ChatServer();

	int GetPlayerCount() const { return mPlayerCount.load(); }
	void SendMonitorData() { mMonitorClient.SendMonitorData(); }
	void UpdateMonitorTPS() { mMonitorClient.UpdateTPS(); }

	void PrintContentMonitor() override
	{
		ConsolePrintLine(L"  Pool [Player] Use:%-8ld Alloc:%-8ld Free:%-8ld", Player::playerMemoryPool.GetUseCount(), Player::playerMemoryPool.GetAllocCount(), Player::playerMemoryPool.GetFreeCount());
		ConsolePrintLine(L"  PlayerMap Total : %-10d", mPlayerCount.load());
		ConsolePrintLine(L"  Monitor Server  : %-14s Send TPS : %-10ld", mMonitorClient.IsConnected() ? L"Connected" : L"Disconnected", mMonitorClient.GetSendTPS());
		ConsolePrintLine(L"  Redis Server    : %-14s", mRedis.IsConnected() ? L"Connected" : L"Disconnected");
	}

	void LogContentMonitor() override
	{
		LOG(L"[Monitor] Pool [Player] Use: %ld Alloc: %ld", Player::playerMemoryPool.GetUseCount(), Player::playerMemoryPool.GetAllocCount());
		LOG(L"[Monitor] PlayerMap Total: %d, MonitorServer: %s", mPlayerCount.load(), mMonitorClient.IsConnected() ? L"Connected" : L"Disconnected");
	}

	virtual ~ChatServer();
};