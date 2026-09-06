#include "LoginServer.h"
#include "PacketGenerator.h"
#include "../NetLib/PerfProfiler.h"

LoginServer::LoginServer() : NetServer(), mMonitorClient(this)
{
	// Redis 연결
	if (!mRedis.Connect(REDIS_HOST, REDIS_PORT))
	{
		LOG(L"[LoginServer] Redis 연결 실패!");
	}

	for (int i = 0; i < CONTENT_THREAD_CNT; ++i)
	{
		// DB 연결
		if (!mDBConn[i].Connect(DB_HOST, DB_USER, DB_PASSWORD, DB_NAME, DB_PORT))
		{
			LOG(L"[LoginServer] mDBConn[%d] 연결 실패!", i);
		}

		// 커널 IOCP 핸들 생성 (로직 전용 큐)
		mContentocpHandles[i] = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);

		// NetServer에서 상속받은 mThreads에 로직 스레드 등록 및 실행
		mThreads.emplace_back(&LoginServer::contentThread, this, i);
	}
}

LoginServer::~LoginServer()
{
	for (int i = 0; i < CONTENT_THREAD_CNT; ++i)
	{
		if (mContentocpHandles[i] != INVALID_HANDLE_VALUE)
			CloseHandle(mContentocpHandles[i]);
	}
}

bool LoginServer::OnConnectionRequest(int ip, int port)
{
	// Accept 직후
	// LOG(L"[OnConnectionRequest]\n");
	return true;
}

void LoginServer::OnClientJoin(uint64_t sessionID)
{
	// Accept 후 접속처리 후
// LOG(L"[OnClientJoin] %d \n", sessionID);

	int tid = GetTargetThread(sessionID);
	Job* job = Job::jobMemoryPool.Alloc();
	job->SetType(EJobType::CLIENT_JOIN);
	job->SetSessionID(sessionID);

	PostQueuedCompletionStatus(mContentocpHandles[tid], 0, 0, (LPOVERLAPPED)job);
}

void LoginServer::OnClientLeave(uint64_t sessionID)
{
	// LOG(L"[OnClientLeave] %d \n", sessionID);
	int tid = GetTargetThread(sessionID);

	Job* job = Job::jobMemoryPool.Alloc();
	job->SetType(EJobType::CLIENT_LEAVE);
	job->SetSessionID(sessionID);
	PostQueuedCompletionStatus(mContentocpHandles[tid], 0, 0, (LPOVERLAPPED)job);
}

void LoginServer::OnRecv(uint64_t sessionID, Packet* packet)
{
	mRecvCount++;
	mJobCount.fetch_add(1);

	int tid = GetTargetThread(sessionID);

	// Packet을 ContentQueue에 넣음
	Job* job = Job::jobMemoryPool.Alloc();
	job->SetType(EJobType::PACKET);
	job->SetSessionID(sessionID);
	job->SetPacket(packet);
	PostQueuedCompletionStatus(mContentocpHandles[tid], 0, 0, (LPOVERLAPPED)job);
}

void LoginServer::OnSend(uint64_t sessionID, int sendsize)
{
	// LOG(L"[OnSend] \n", );
	mSendCount++;
}

void LoginServer::OnError(int errorCode, const wchar_t* errorMessage)
{
	// 에러 코드 Print
	// std::wcout << L"[OnError] " << errorCode << L": " << errorMessage << std::endl;
}

void LoginServer::contentThread(int threadIdx)
{
	while (!bTerminate)
	{
		// 1. 이벤트 대기
		DWORD transferred = 0;
		ULONG_PTR completionKey = 0;
		LPOVERLAPPED overlapped = nullptr;

		// 커널 큐(IOCP)에서 일감을 기다림
		BOOL ret = GetQueuedCompletionStatus(
			mContentocpHandles[threadIdx],
			&transferred,
			&completionKey,
			&overlapped,
			0
		);
		if (ret && overlapped)
		{
			Job* job = reinterpret_cast<Job*>(overlapped);
			switch (job->GetType())
			{
			case EJobType::CLIENT_JOIN:
			{
				mJobCount.fetch_add(1);
				// Player 생성
				// Alloc 은 생성자를 호출하지 않으므로(Pool 재사용) 명시적으로 Clear 필요
				Player* player = Player::playerMemoryPool.Alloc();
				player->Clear();
				player->SetSessionID(job->GetSessionID());
				player->OnConnect();
				int tid = GetTargetThread(job->GetSessionID());

				auto [it, inserted] = mPlayerMap[tid].insert({ job->GetSessionID(), player });
				if (!inserted) {
					LOG(L"[CLIENT_JOIN] DUPLICATE sessionID=%llu oldPlayer=%p newPlayer=%p",
						job->GetSessionID(), it->second, player);
				}

				// LOG(L"[CLIENT_JOIN] sessionId: %llu", job->GetSessionID());
				break;
			}
			case EJobType::CLIENT_LEAVE:
			{
				// Player 삭제
				// LOG(L"[CLIENT_LEAVE] sessionId: %llu", job->GetSessionID());
				Player* player = findPlayer(job->GetSessionID());
				if (player != nullptr) {
					removePlayer(player);
				}
				break;
			}
			case EJobType::PACKET:
			{
				mJobCount.fetch_add(1);
				Player* player = findPlayer(job->GetSessionID());
				if (player != nullptr) {
					if (!packetProc(threadIdx, *player, job->GetPacket())) {
						reserveDeletePlayer(player);  // 실패 시 연결 해제
					}
				}
				else {
					WORD headerType;
					*job->GetPacket() >> headerType;
					LOG(L"[PACKET] Player not found! sessionID: %llu, packetType: %d", job->GetSessionID(), headerType);
				}
				job->GetPacket()->SubRef();  // 추후 고민하기
				break;
			}
			case EJobType::CHECK_HEAR_BEAT:
			{
				mCheckHeartCount.fetch_add(1);
				for (auto& [sid, player] : mPlayerMap[threadIdx])
				{
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
				break;
			}
			}
			Job::jobMemoryPool.Free(job);
		}

	}
};

bool LoginServer::packetProc(int threadIdx, Player& player, Packet* packet)
{

	WORD headerType;
	*packet >> headerType;
	// LOG(L"[PacketProc] headerType: %d \n", headerType);

	switch (headerType) {
	case en_PACKET_CS_LOGIN_REQ_LOGIN:
		return LoginServer::login(threadIdx, player, packet);
	default:
		LOG(L"[ATTACK] Unknown opcode sessionID:%llu type:0x%x", player.GetSessionID(), headerType);
		return false;
	}
	return false;
}

bool LoginServer::login(int threadIdx, Player& player, Packet* packet)
{
	uint64_t accountNo;
	char sessionKey[64]{};
	*packet >> accountNo;
	packet->GetData(sessionKey, sizeof(sessionKey));

	// LOG(L"[Login] accountNo: %lld", accountNo);

	BYTE status = dfLOGIN_STATUS_FAIL;
	WCHAR userId[20]{};
	WCHAR userNick[20]{};

	// DB 미연결 시 FAIL 응답만 내려보내고 종료 (mysql_real_escape_string에 NULL 넘기면 크래시)
	if (!mDBConn[threadIdx].IsConnected())
	{
		LOG(L"[Login] DB not connected. accountNo=%llu", accountNo);
	}
	else
	{
		// 1. null 종료 보장
		char sessionKeyStr[129]{};
		memcpy(sessionKeyStr, sessionKey, 64);
		sessionKeyStr[64] = '\0';

		// 2. 특수문자 이스케이프
		char escapedKey[257]{};
		mysql_real_escape_string(mDBConn[threadIdx].GetConnection(), escapedKey, sessionKeyStr, (unsigned long)strlen(sessionKeyStr));

		// 3. DB 조회
		char query[512];
		sprintf_s(query, sizeof(query),
			"SELECT `accountno`, `userid`, `usernick` "
			"FROM `account` WHERE `accountno` = %lld",
			accountNo);

		MYSQL_RES* result = mDBConn[threadIdx].Query(query);
		if (result != nullptr)
		{
			MYSQL_ROW row = mysql_fetch_row(result);
			if (row != nullptr)
			{
				status = dfLOGIN_STATUS_OK;
				// userid, usernick을 WCHAR로 변환
				MultiByteToWideChar(CP_UTF8, 0, row[1], -1, userId, 20);
				MultiByteToWideChar(CP_UTF8, 0, row[2], -1, userNick, 20);

				player.SetAccountNo(accountNo);
				player.SetID(userId);
				player.SetNickName(userNick);
				player.OnLogin();

				// Redis에 accountNo -> sessionKey 저장 (002 미로그인 타임아웃 50초에 맞춤)
				mRedis.Set(std::to_string(accountNo), sessionKeyStr, 60);
			}
			mysql_free_result(result);
		}
	}

	// 4. 응답 패킷 전송
	Packet* resPacket = Packet::packetMemoryPool.Alloc();
	resPacket->Init();

	ChatPacket::MakeLoginResLogin(resPacket, accountNo, status,
		userId, userNick,
		GAME_SERVER_IP, GAME_SERVER_PORT,
		CHAT_SERVER_IP, CHAT_SERVER_PORT);
	SendPacket(player.GetSessionID(), resPacket);
	resPacket->SubRef();

	return true;
}

// 플레이어 삭제 (리스트 + 섹터)
void LoginServer::removePlayer(Player* player)
{
	DisConnect(player->GetSessionID());

	// 2. 플레이어 리스트에서 제거
	uint64_t sessionId = player->GetSessionID();
	int tid = GetTargetThread(sessionId);
	if (mPlayerMap[tid].erase(sessionId) > 0) {
		// LOG(L"[Logout] accountNo: %llu, sessionID: %llu", player->GetAccountNo(), sessionId);
	}

	player->SetIsLoggedout();
	Player::playerMemoryPool.Free(player);	// ObjectPool에 반납
}

// 삭제 예약: 플래그 마킹 + 실제 연결 해제 요청
// (DisConnect 호출이 없으면 악성 클라이언트가 세션을 계속 점유)
void LoginServer::reserveDeletePlayer(Player* player)
{
	if (player->GetIsWillDelete()) return;
	player->SetWillDelete();
	DisConnect(player->GetSessionID());
}

// 플레이어 검색
Player* LoginServer::findPlayer(uint64_t sessionId)
{
	int tid = GetTargetThread(sessionId);
	auto it = mPlayerMap[tid].find(sessionId);
	if (it != mPlayerMap[tid].end()) {
		return it->second;
	}
	return nullptr;
}