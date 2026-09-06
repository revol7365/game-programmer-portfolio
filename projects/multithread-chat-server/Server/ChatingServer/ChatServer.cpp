#include <algorithm>
#include "ChatServer.h"
#include "ChatPacketGenerator.h"
#include "../NetLib/PerfProfiler.h"

ChatServer::ChatServer() : NetServer(), mMonitorClient(this)
{
	// Redis 연결
	if constexpr (USE_REDIS_AUTH)
	{
		if (!mRedis.Connect(REDIS_HOST, REDIS_PORT))
		{
			LOG(L"[ChatServer] Redis 연결 실패!");
		}
	}

	InitializeSRWLock(&mPlayerMapLock);
	initSectorAroundTable();

	for (int y = 0; y < SECTOR_CNT_Y; y++)
		for (int x = 0; x < SECTOR_CNT_X; x++)
			InitializeSRWLock(&mSector[y][x].lock);

	// 타임아웃 체크 스레드
	mThreads.emplace_back(&ChatServer::timeoutThread, this);
}

ChatServer::~ChatServer()
{
}

bool ChatServer::OnConnectionRequest(int ip, int port)
{
	// Accept 직후
	// LOG(L"[OnConnectionRequest]\n");
	return true;
}

void ChatServer::OnClientJoin(uint64_t sessionID)
{
	mJobCount.fetch_add(1);

	Player* player = Player::playerMemoryPool.Alloc();
	player->Clear();
	player->SetSessionID(sessionID);
	player->OnConnect();

	AcquireSRWLockExclusive(&mPlayerMapLock);
	auto [it, inserted] = mPlayerMap.insert({ sessionID, player });
	ReleaseSRWLockExclusive(&mPlayerMapLock);

	if (!inserted) {
		LOG(L"[CLIENT_JOIN] DUPLICATE sessionID=%llu oldPlayer=%p newPlayer=%p",
			sessionID, it->second, player);
		Player::playerMemoryPool.Free(player);
	}
	else {
		mPlayerCount.fetch_add(1);
	}
}

void ChatServer::OnClientLeave(uint64_t sessionID)
{
	Player* player = nullptr;

	// Exclusive 보유 시간 최소화: map에서 제거만 수행
	AcquireSRWLockExclusive(&mPlayerMapLock);
	auto it = mPlayerMap.find(sessionID);
	if (it != mPlayerMap.end()) {
		player = it->second;
		player->SetWillDelete();
		mPlayerMap.erase(it);
		mPlayerCount.fetch_sub(1);
	}
	ReleaseSRWLockExclusive(&mPlayerMapLock);

	// Lock 밖에서 sector 정리 및 메모리 반환
	// (broadcast는 WillDelete 체크로 이 플레이어를 스킵)
	if (player != nullptr) {
		erasePlayerFromSector(player);
		player->SetIsLoggedout();
		Player::playerMemoryPool.Free(player);
	}
}

void ChatServer::OnRecv(uint64_t sessionID, Packet* packet)
{
	mRecvCount++;
	mJobCount.fetch_add(1);

	AcquireSRWLockShared(&mPlayerMapLock);
	Player* player = findPlayer(sessionID);
	if (player != nullptr) {
		if (!packetProc(*player, packet)) {
			reserveDeletePlayer(player);
		}
	}
	else {
		WORD headerType;
		*packet >> headerType;
		LOG(L"[PACKET] Player not found! sessionID: %llu, packetType: %d", sessionID, headerType);
		DisConnect(sessionID, DisconnectReason::InvalidPacket);
	}
	ReleaseSRWLockShared(&mPlayerMapLock);

	packet->SubRef();
}

void ChatServer::OnSend(uint64_t sessionID, int sendsize)
{
	// LOG(L"[OnSend] \n", );
	mSendCount++;
}

void ChatServer::OnError(int errorCode, const wchar_t* errorMessage)
{
	// 에러 코드 Print
	// std::wcout << L"[OnError] " << errorCode << L": " << errorMessage << std::endl;
}


bool ChatServer::packetProc(Player& player, Packet* packet)
{

	WORD headerType;
	*packet >> headerType;
	// LOG(L"[PacketProc] headerType: %d \n", headerType);

	if (!player.GetIsLoginedIn() && headerType != en_PACKET_CS_CHAT_REQ_LOGIN) {
		LOG(L"[ATTACK] Unauthorized packet sessionID:%llu type:0x%x", player.GetSessionID(), headerType);
		reserveDeletePlayer(&player);
		return false;
	}

	// headerType(2) 읽은 뒤 남은 payload 크기
	int payloadSize = packet->GetDataSize() - 5 - sizeof(WORD);

	switch (headerType) {
	case en_PACKET_CS_CHAT_REQ_LOGIN:
		if (payloadSize != 152) {
			LOG(L"[ATTACK] LOGIN invalid payload sessionID:%llu size:%d", player.GetSessionID(), payloadSize);
			return false;
		}
		return ChatServer::login(player, packet);

	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		if (payloadSize != 12) {
			LOG(L"[ATTACK] SECTOR_MOVE invalid payload sessionID:%llu size:%d", player.GetSessionID(), payloadSize);
			return false;
		}
		return ChatServer::sectorMove(player, packet);

	case en_PACKET_CS_CHAT_REQ_MESSAGE:
		if (payloadSize < 10) {
			LOG(L"[ATTACK] MESSAGE invalid payload sessionID:%llu size:%d", player.GetSessionID(), payloadSize);
			return false;
		}
		return ChatServer::sendChatMessage(player, packet);

	case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		if (payloadSize != 0) {
			LOG(L"[ATTACK] HEARTBEAT invalid payload sessionID:%llu size:%d", player.GetSessionID(), payloadSize);
			return false;
		}
		return ChatServer::heartBeat(player, packet);

	default:
		LOG(L"Packet Type error 0x%x\n", headerType);
		return false;
	}
	return false;
}

// 패킷 처리 로직
bool ChatServer::login(Player& player, Packet* packet)
{
	if (player.GetIsLoginedIn()) {
		LOG(L"[ATTACK] Duplicate login attempt sessionID:%llu accountNo:%llu", player.GetSessionID(), player.GetAccountNo());
		return false;
	}

	uint64_t accountNo;
	WCHAR id[20]{};
	WCHAR nickName[20]{};
	char sessionKey[64]{};

	*packet >> accountNo;
	packet->GetData((char*)id, sizeof(id));
	packet->GetData((char*)nickName, sizeof(nickName));
	packet->GetData(sessionKey, sizeof(sessionKey));

	if (std::find(std::begin(id), std::end(id), L'\0') == std::end(id)) {
		LOG(L"[ATTACK] LOGIN id missing null terminator sessionID:%llu accountNo:%llu", player.GetSessionID(), accountNo);
		return false;
	}

	if (std::find(std::begin(nickName), std::end(nickName), L'\0') == std::end(nickName)) {
		LOG(L"[ATTACK] LOGIN nickName missing null terminator sessionID:%llu accountNo:%llu", player.GetSessionID(), accountNo);
		return false;
	}

	Packet* resPacket = Packet::packetMemoryPool.Alloc();
	if (resPacket == nullptr) {
		OnError(-1, L"PacketMemoryPool Error");
		return false;
	}
	resPacket->Init();

	// Redis에서 AccountNo가 키인 value와 비교해서 일치하면 원자적으로 삭제
	if constexpr (USE_REDIS_AUTH)
	{
		if (!mRedis.CompareAndDel(std::to_string(accountNo), std::string(sessionKey, 64)))
		{
			LOG(L"[Login FAIL] accountNo:%llu, SessionKey is not same", accountNo);
			resPacket->SubRef();
			return false;
		}
	}

	for (auto& [sid, p] : mPlayerMap) {
		if (p->GetIsLoginedIn() && p->GetAccountNo() == accountNo && sid != player.GetSessionID())
		{
			LOG(L"[Duplicate Login] accountNo:%llu existSession:%llu disconnected, newSession:%llu", accountNo, sid, player.GetSessionID());
			DisConnect(sid, DisconnectReason::DuplicateLogin);
			break;
		}
	}


	// Player 정보 설정
	player.SetAccountNo(accountNo);
	player.SetID(id);
	player.SetNickName(nickName);
	player.SetSessionKey(sessionKey);
	player.OnLogin();

	/*
	if (player.GetAccountNo() != accountNo)
	{
		__debugbreak();
	}
	*/

	// 로그인 응답을 보내야 한다
	ChatPacket::MakeChatResLogin(resPacket, true, player.GetAccountNo());

	// LOG(L"[SEND_MESSAGE LOGIN] sessionID:%llu, Packet-accountNo:%llu, Player-accountNo:%llu", player.GetSessionID(), accountNo, player.GetAccountNo());
	if (!SendPacket(player.GetSessionID(), resPacket)) {
		LOG(L"[Login FAIL] accountNo:%llu, SendPacket Fail", accountNo);
		resPacket->SubRef();
		return false;
	}
	resPacket->SubRef();
	return true;
}

bool ChatServer::sectorMove(Player& player, Packet* packet)
{
	// LOG(L"[sectorMove] \n");

	uint64_t accountNo;
	WORD	SectorX;
	WORD	SectorY;

	*packet >> accountNo;
	*packet >> SectorX;
	*packet >> SectorY;

	if (player.GetAccountNo() != accountNo) {
		LOG(L"[ATTACK] SECTOR_MOVE accountNo mismatch sessionID:%llu player:%llu packet:%llu", player.GetSessionID(), player.GetAccountNo(), accountNo);
		return false;
	}

	if (SectorX >= SECTOR_CNT_X || SectorY >= SECTOR_CNT_Y) {
		LOG(L"[ATTACK] SECTOR_MOVE out of range sessionID:%llu accountNo:%llu sector:(%d,%d)", player.GetSessionID(), accountNo, SectorX, SectorY);
		return false;
	}

	// Player 섹터 이동하기
	movePlayerSector(&player, SectorX, SectorY);

	Packet* resPacket = Packet::packetMemoryPool.Alloc();
	if (resPacket == nullptr) {
		OnError(-1, L"PacketMemoryPool Error");
		return false;
	}
	resPacket->Init();
	// 섹터 이동 결과 보내기
	ChatPacket::MakeChatSectorMove(resPacket, player.GetAccountNo(), SectorX, SectorY);
	// LOG(L"[SEND_MESSAGE SectorMove] sessionID:%llu, Packet-accountNo:%llu, Player-accountNo:%llu", player.GetSessionID(), accountNo, player.GetAccountNo());
	if (!SendPacket(player.GetSessionID(), resPacket))
	{
		LOG(L"[sectorMove] accountNo:%llu, SendPacket Fail", accountNo);
		resPacket->SubRef();
		return false;
	}
	resPacket->SubRef();
	return true;
}

bool ChatServer::sendChatMessage(Player& player, Packet* packet)
{
	// LOG(L"[sendChatMessage] \n");
	if (!player.GetIsLoginedIn())
	{
		return false;
	}

	// 변수 선언
	uint64_t accountNo;
	WORD  messageLen;
	WCHAR message[512] = { 0, }; // 최대 512자 제한 (버퍼 넉넉히)

	// 데이터 추출
	*packet >> accountNo;   // AccountNo (8바이트)
	*packet >> messageLen;  // MessageLen (2바이트, 바이트 단위)

	if (player.GetAccountNo() != accountNo) {
		LOG(L"[ATTACK] MESSAGE accountNo mismatch sessionID:%llu player:%llu packet:%llu", player.GetSessionID(), player.GetAccountNo(), accountNo);
		return false;
	}

	if (messageLen > 1024 || messageLen <= 0) {
		LOG(L"[ATTACK] MESSAGE invalid messageLen sessionID:%llu accountNo:%llu len:%d", player.GetSessionID(), accountNo, messageLen);
		return false;
	}

	if (messageLen % sizeof(WCHAR) != 0) {
		LOG(L"[ATTACK] MESSAGE not wchar aligned sessionID:%llu accountNo:%llu len:%d", player.GetSessionID(), accountNo, messageLen);
		return false;
	}

	// messageLen과 실제 남은 payload가 정확히 일치하는지 검증
	int remainPayload = packet->GetDataSize() - 5 - sizeof(WORD) - sizeof(uint64_t) - sizeof(WORD);
	if (remainPayload != messageLen) {
		LOG(L"[ATTACK] MESSAGE payload mismatch sessionID:%llu accountNo:%llu messageLen:%d remain:%d", player.GetSessionID(), accountNo, messageLen, remainPayload);
		return false;
	}

	packet->GetData(message, messageLen);

	// 섹터 미배정 상태에서 채팅 시도 시 끊기
	WORD sx = player.GetSectorX();
	WORD sy = player.GetSectorY();
	if (sx >= SECTOR_CNT_X || sy >= SECTOR_CNT_Y) {
		LOG(L"[ATTACK] MESSAGE no sector sessionID:%llu accountNo:%llu sector:(%d,%d)", player.GetSessionID(), accountNo, sx, sy);
		return false;
	}

	Packet* resPacket = Packet::packetMemoryPool.Alloc();
	if (resPacket == nullptr) {
		OnError(-1, L"PacketMemoryPool Error");
		return false;
	}
	resPacket->Init();
	// Packet 생성
	ChatPacket::MakeChatResMessage(resPacket, player.GetAccountNo(), player.GetID(),
		player.GetNickName(), messageLen, message);

	broadcast(player, resPacket);
	resPacket->SubRef();

	return true;
}

bool ChatServer::heartBeat(Player& player, Packet* packet)
{
	player.UpdateHeartbeat();
	return true;
}

void ChatServer::initSectorAroundTable()
{
	for (int y = 0; y < SECTOR_CNT_Y; ++y)
	{
		for (int x = 0; x < SECTOR_CNT_X; ++x)
		{
			// 기존 로직을 사용하여 계산
			SectorAround& around = mSectorAroundTable[y][x];
			around.iCount = 0;

			int iStartX = (std::max)(0, x - 1);
			int iEndX = (std::min)(SECTOR_CNT_X - 1, x + 1);
			int iStartY = (std::max)(0, y - 1);
			int iEndY = (std::min)(SECTOR_CNT_Y - 1, y + 1);

			for (int sy = iStartY; sy <= iEndY; ++sy)
			{
				for (int sx = iStartX; sx <= iEndX; ++sx)
				{
					around.Around[around.iCount].iX = sx;
					around.Around[around.iCount].iY = sy;
					around.iCount++;
				}
			}
		}
	}
}

// 플레이어 삭제 - OnClientLeave에서 직접 처리하므로 미사용
// void ChatServer::removePlayer(Player* player) {}

// 섹터 이동
void ChatServer::movePlayerSector(Player* player, WORD newSectorX, WORD newSectorY)
{
	if (newSectorX >= SECTOR_CNT_X || newSectorY >= SECTOR_CNT_Y) return;

	WORD oldSectorX = player->GetSectorX();
	WORD oldSectorY = player->GetSectorY();

	if (oldSectorX == newSectorX && oldSectorY == newSectorY) return;

	// 기존 섹터에서 제거
	erasePlayerFromSector(player);

	// 새 섹터에 추가 (좌표 변경도 Lock 안에서 수행하여 race 방지)
	AcquireSRWLockExclusive(&mSector[newSectorY][newSectorX].lock);
	player->SetSector(newSectorX, newSectorY);
	mSector[newSectorY][newSectorX].players.push_back(player);
	ReleaseSRWLockExclusive(&mSector[newSectorY][newSectorX].lock);
}

void ChatServer::erasePlayerFromSector(Player* player)
{
	WORD x = player->GetSectorX();
	WORD y = player->GetSectorY();

	if (x >= SECTOR_CNT_X || y >= SECTOR_CNT_Y) return;

	AcquireSRWLockExclusive(&mSector[y][x].lock);
	auto& sectorPlayers = mSector[y][x].players;
	auto it = std::find(sectorPlayers.begin(), sectorPlayers.end(), player);

	if (it != sectorPlayers.end()) {
		*it = sectorPlayers.back();
		sectorPlayers.pop_back();
	}
	ReleaseSRWLockExclusive(&mSector[y][x].lock);
}

// 삭제 예약
void ChatServer::reserveDeletePlayer(Player* player, DisconnectReason reason)
{
	if (player->GetIsWillDelete()) return;
	player->SetWillDelete();
	DisConnect(player->GetSessionID(), reason);
}

// 플레이어 검색
// 호출 전에 mPlayerMapLock Shared 또는 Exclusive 획득 필요
Player* ChatServer::findPlayer(uint64_t sessionId)
{
	auto it = mPlayerMap.find(sessionId);
	if (it != mPlayerMap.end()) {
		return it->second;
	}
	return nullptr;
}

// Send
void ChatServer::broadcast(Player& player, Packet* pPacket) {
	PERF_TIMER(broadcast);

	// 섹터 범위 유효성 체크
	int sectorX = player.GetSectorX();
	int sectorY = player.GetSectorY();

	if (sectorX < 0 || sectorX >= SECTOR_CNT_X || sectorY < 0 || sectorY >= SECTOR_CNT_Y) {
		return;
	}

	// Lock 안에서는 세션 ID만 수집 (Lock 보유 시간 최소화)
	thread_local std::vector<uint64_t> targets;
	targets.clear();

	const SectorAround& around = mSectorAroundTable[sectorY][sectorX];

	for (int i = 0; i < around.iCount; ++i) {
		int tx = around.Around[i].iX;
		int ty = around.Around[i].iY;

		{
			PERF_TIMER(sectorLock);
			AcquireSRWLockShared(&mSector[ty][tx].lock);
		}

		for (Player* pTarget : mSector[ty][tx].players) {
			if (pTarget->GetIsWillDelete()) continue;
			if (!pTarget->GetIsLoginedIn()) continue;
			targets.push_back(pTarget->GetSessionID());
		}

		ReleaseSRWLockShared(&mSector[ty][tx].lock);
	}

	// Lock 밖에서 SendPacket (Lock 중첩 방지)
	{
		PERF_TIMER(sendLoop);
		for (uint64_t sid : targets) {
			SendPacket(sid, pPacket);
		}
	}
}

void ChatServer::timeoutThread() {
	while (!GetTerminate()) {
		CheckTimeOut();
		Sleep(5000); // 5초 주기로 검사
	}
}

// TimeOut 체크
void ChatServer::CheckTimeOut()
{
	mCheckHeartCount.fetch_add(1);

	AcquireSRWLockShared(&mPlayerMapLock);
	for (auto it = mPlayerMap.begin(); it != mPlayerMap.end(); ++it) {
		Player* player = it->second;
		if (player->GetIsWillDelete()) continue;
		auto snap = player->CheckTimeOutSnapshot();
		if (snap.expired && snap.firstReport) {
			// 감지/로깅/카운트만 수행, disconnect 는 하지 않음.
			// heartbeat/OnLogin/OnConnect 에서 플래그가 리셋되므로
			// 유휴 상태가 재시작되면 다음 진입 시 다시 1회 보고됨.
			mDisconnectCount[static_cast<int>(DisconnectReason::Timeout)].fetch_add(1);
			LOG(L"[Timeout] sessionID:%llu accountNo:%llu loggedIn:%d idle:%llums",
				player->GetSessionID(), player->GetAccountNo(),
				snap.loggedIn ? 1 : 0, snap.idleMs);
		}
	}
	ReleaseSRWLockShared(&mPlayerMapLock);
}
