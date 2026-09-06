#include "LanClient.h"

LanClient::LanClient(const char* ip, int port, int workerThreadCount)
	: mPort(port), mWorkerThreadCount(workerThreadCount)
{
	strcpy_s(mIP, sizeof(mIP), ip);

	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	// IOCP 생성
	mHcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (mHcp == NULL) {
		LOG(L"[LanClient] CreateIoCompletionPort Failed: %d\n", GetLastError());
		return;
	}

	// 워커 스레드 생성
	for (int i = 0; i < mWorkerThreadCount; i++) {
		mWorkerThreads.emplace_back(&LanClient::workerThread, this);
	}
}

LanClient::~LanClient()
{
	Disconnect();

	mTerminate = true;

	// 워커 스레드 종료 신호
	for (int i = 0; i < mWorkerThreadCount; i++) {
		PostQueuedCompletionStatus(mHcp, 0, 0, nullptr);
	}

	for (std::thread& t : mWorkerThreads) {
		if (t.joinable())
			t.join();
	}
	mWorkerThreads.clear();

	if (mHcp != NULL) {
		CloseHandle(mHcp);
		mHcp = NULL;
	}

	WSACleanup();
}

bool LanClient::Connect()
{
	if (mIsConnected.load()) return false;

	// 이전 연결의 잔여 소켓 정리 (비정상 종료 후 남은 소켓)
	if (mSocket != INVALID_SOCKET) {
		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
	}

	// 소켓 생성
	mSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (mSocket == INVALID_SOCKET) {
		LOG(L"[LanClient] socket() Failed: %d\n", WSAGetLastError());
		return false;
	}

	// Nagle OFF
	BOOL nodelay = TRUE;
	setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

	// LINGER 설정
	LINGER optval{};
	optval.l_onoff = 1;
	optval.l_linger = 0;
	setsockopt(mSocket, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));

	// 서버 주소 설정
	SOCKADDR_IN serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(mPort);
	inet_pton(AF_INET, mIP, &serverAddr.sin_addr);

	// 접속
	int ret = connect(mSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
	if (ret == SOCKET_ERROR) {
		int err = WSAGetLastError();
		LOG(L"[LanClient] connect() Failed: %d (IP: %hs, Port: %d)\n", err, mIP, mPort);
		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
		return false;
	}

	// 세션 초기화
	mSession.Init();
	mSession.socket = mSocket;
	mSession.id = 1;
	mSession.state = ESessionState::Connected;

	// IOCP 등록
	if (CreateIoCompletionPort((HANDLE)mSocket, mHcp, (ULONG_PTR)&mSession, 0) == NULL) {
		LOG(L"[LanClient] IOCP Associate Failed: %d\n", GetLastError());
		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
		return false;
	}

	// 이전 releaseSession이 워커 스레드에서 비동기로 control을 덮어쓸 수 있으므로,
	// IOCP 등록 후 postRecv 직전에 control을 확정적으로 초기화
	mSession.control.store(1); // ioCount=1, releaseFlag=0 (postRecv용 IoCount 선점)

	mIsConnected.store(true);

	LOG(L"[LanClient] Connected to %hs:%d\n", mIP, mPort);
	OnConnect();

	// 최초 Recv 걸기 (IoCount는 이미 1로 설정됨)
	if (!postRecvInitial()) {
		LOG(L"[LanClient] Initial postRecv Failed\n");
		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
		mSession.socket = INVALID_SOCKET;
		mSession.control.store(0);
		mIsConnected.store(false);
		return false;
	}

	return true;
}

void LanClient::Disconnect()
{
	if (!mIsConnected.exchange(false)) return;

	requestRelease();
}

bool LanClient::SendPacket(Packet* packet)
{
	if (!mIsConnected.load()) return false;
	if (!mSession.IncreaseIoCount()) {
		LOG(L"[LanClient] SendPacket - IncreaseIoCount Failed (control: %llu)\n", mSession.control.load());
		return false;
	}

	// LAN 헤더 설정 (암호화 없음, 단순 길이 헤더)
	{
		int payloadSize = packet->GetDataSize() - sizeof(LAN_HEADER);
		LAN_HEADER* pHeader = (LAN_HEADER*)packet->GetBufferPtr();
		pHeader->Len = (unsigned short)payloadSize;
	}

	// 링버퍼에 패킷 삽입
	packet->AddRef();
	EnterCriticalSection(&mSession.sessionSendRingBuffer_cs);
	bool enqueued = mSession.sendRingBuffer.EnqueueItem(packet);
	if (!enqueued) {
		LOG(L"[LanClient] sendRingBuffer FULL!\n");
		packet->SubRef();
	}
	LeaveCriticalSection(&mSession.sessionSendRingBuffer_cs);

	bool isSuccess = postSend();

	decreaseIoCount();

	if (isSuccess) {
		OnSend(packet->GetDataSize());
	}

	return isSuccess;
}

// --- Private ---

bool LanClient::postRecvInitial()
{
	// Connect()에서 ioCount를 이미 1로 설정한 상태에서 호출
	// IncreaseIoCount를 거치지 않으므로 releaseFlag 레이스 영향 없음
	DWORD recvBytes, flags = 0;

	ZeroMemory(&mSession.recvOverlapped.overlapped, sizeof(OVERLAPPED));
	int dwBufferCount = mSession.recvRingBuffer.GetEnqueueSegments(mSession.recvOverlapped.wsaBuf);
	if (dwBufferCount == 0) {
		LOG(L"[LanClient] RecvRingBuffer Full\n");
		return false;
	}

	int ret = WSARecv(mSession.socket, mSession.recvOverlapped.wsaBuf, dwBufferCount, &recvBytes, &flags, &mSession.recvOverlapped.overlapped, NULL);
	int err_code = WSAGetLastError();
	if (ret == SOCKET_ERROR && err_code != ERROR_IO_PENDING) {
		if (err_code != 10054)
		{
			LOG(L"[LanClient] WSARecv Error: %d\n", err_code);
		}
		return false;
	}
	return true;
}

bool LanClient::postRecv()
{
	DWORD recvBytes, flags = 0;
	if (!mSession.IncreaseIoCount()) {
		return false;
	}

	ZeroMemory(&mSession.recvOverlapped.overlapped, sizeof(OVERLAPPED));
	int dwBufferCount = mSession.recvRingBuffer.GetEnqueueSegments(mSession.recvOverlapped.wsaBuf);
	if (dwBufferCount == 0) {
		LOG(L"[LanClient] RecvRingBuffer Full\n");
		requestRelease();
		decreaseIoCount();
		return false;
	}

	int ret = WSARecv(mSession.socket, mSession.recvOverlapped.wsaBuf, dwBufferCount, &recvBytes, &flags, &mSession.recvOverlapped.overlapped, NULL);
	int err_code = WSAGetLastError();
	if (ret == SOCKET_ERROR && err_code != ERROR_IO_PENDING) {
		if (err_code != 10054)
		{
			LOG(L"[LanClient] WSARecv Error: %d\n", err_code);
		}
		requestRelease();
		decreaseIoCount();
		return false;
	}
	return true;
}

bool LanClient::postSend()
{
	if (mSession.sendFlag.exchange(1) != 0) return true;

	EnterCriticalSection(&mSession.sessionSendRingBuffer_cs);

	DWORD dwBufferCount = 0;
	mSession.sendOverlapped.packetCount = 0;

	while (mSession.sendRingBuffer.GetUseSize() > 0 && dwBufferCount < MAX_SEND_PACKET)
	{
		Packet* packet = nullptr;
		mSession.sendRingBuffer.DequeueItem(&packet);
		if (packet == nullptr) break;

		mSession.sendOverlapped.wsaBuf[dwBufferCount].buf = packet->GetBufferPtr();
		mSession.sendOverlapped.wsaBuf[dwBufferCount].len = packet->GetDataSize();
		mSession.sendOverlapped.sentPackets[dwBufferCount] = packet;
		dwBufferCount++;
	}
	mSession.sendOverlapped.packetCount = dwBufferCount;
	LeaveCriticalSection(&mSession.sessionSendRingBuffer_cs);

	if (dwBufferCount == 0) {
		mSession.sendFlag.store(0);
		return true;
	}

	if (!mSession.IncreaseIoCount()) {
		for (DWORD i = 0; i < dwBufferCount; ++i) mSession.sendOverlapped.sentPackets[i]->SubRef();
		mSession.sendOverlapped.packetCount = 0;
		mSession.sendFlag.store(0);
		return false;
	}

	ZeroMemory(&mSession.sendOverlapped.overlapped, sizeof(OVERLAPPED));

	int ret = WSASend(mSession.socket, mSession.sendOverlapped.wsaBuf,
		dwBufferCount, NULL, 0,
		&mSession.sendOverlapped.overlapped, NULL);

	if (ret == SOCKET_ERROR) {
		int err = WSAGetLastError();
		if (err == WSA_IO_PENDING) return true;

		for (int i = 0; i < mSession.sendOverlapped.packetCount; ++i) {
			mSession.sendOverlapped.sentPackets[i]->SubRef();
		}
		mSession.sendOverlapped.packetCount = 0;

		decreaseIoCount();
		mSession.sendFlag.store(0);
		if (err != 10054) {
			LOG(L"[LanClient] WSASend Error: %d\n", err);
		}
		return false;
	}
	return true;
}

void LanClient::decreaseIoCount()
{
	uint64_t prevFull = mSession.control.fetch_sub(1);
	SessionControl prev{};
	prev.full = prevFull;

	if (prev.ioCount == 0) {
		LOG(L"[LanClient] ioCount UNDERFLOW! (prev control: %llu)\n", prevFull);
		mSession.control.fetch_add(1); // 언더플로우 복구
		return;
	}

	if (prev.ioCount == 1 && prev.releaseFlag == 1) {
		PostQueuedCompletionStatus(mHcp, 0,
			(ULONG_PTR)&mSession, &mSession.releaseOverlapped.overlapped);
	}
}

void LanClient::requestRelease()
{
	uint32_t before = 0;
	if (!mSession.TrySetReleaseFlag(before)) return;

	if (before == 0) {
		PostQueuedCompletionStatus(mHcp, 0,
			(ULONG_PTR)&mSession, &mSession.releaseOverlapped.overlapped);
	}
}

void LanClient::releaseSession()
{
	uint64_t expectedFull = mSession.control.load();
	while (true)
	{
		SessionControl exp{}; exp.full = expectedFull;

		if (exp.ioCount != 0) return;
		if ((exp.releaseFlag & FLAG_RELEASE) == 0) return;
		if (exp.releaseFlag & FLAG_CLEANUP) return;

		SessionControl des = exp;
		des.releaseFlag |= FLAG_CLEANUP;

		if (mSession.control.compare_exchange_weak(expectedFull, des.full))
			break;
	}

	// 소켓 닫기
	if (mSession.socket != INVALID_SOCKET) {
		closesocket(mSession.socket);
		mSession.socket = INVALID_SOCKET;
		mSocket = INVALID_SOCKET;
	}

	// 미전송 패킷 정리
	EnterCriticalSection(&mSession.sessionSendRingBuffer_cs);
	while (mSession.sendRingBuffer.GetUseSize() > 0) {
		Packet* packet = nullptr;
		if (mSession.sendRingBuffer.DequeueItem(&packet) && packet) {
			packet->SubRef();
		}
	}
	LeaveCriticalSection(&mSession.sessionSendRingBuffer_cs);

	// 버퍼 초기화
	mSession.recvRingBuffer.ClearBuffer();
	mSession.sendRingBuffer.ClearBuffer();
	mSession.state = ESessionState::Free;
	mSession.sendFlag.store(0);

	mIsConnected.store(false);

	OnDisconnect();
}

void LanClient::workerThread()
{
	DWORD cbTransferred;
	Session* session = nullptr;
	LPOVERLAPPED pOverlapped = nullptr;

	while (true) {
		BOOL ret = GetQueuedCompletionStatus(mHcp, &cbTransferred, (PULONG_PTR)&session, &pOverlapped, INFINITE);

		// 종료 신호
		if (pOverlapped == nullptr && session == nullptr) {
			break;
		}

		if (session == nullptr) {
			LOG(L"[LanClient] WorkerThread Error\n");
			continue;
		}

		OverlappedBase* pOv = CONTAINING_RECORD(pOverlapped, OverlappedBase, overlapped);

		// GQCS 실패
		if (ret == FALSE) {
			LOG(L"[LanClient] GQCS Failed (type: %d, err: %d, control: %llu)\n",
				(int)pOv->type, GetLastError(), mSession.control.load());
			requestRelease();
			decreaseIoCount();
			continue;
		}

		// 상대방 연결 종료
		if (pOv->type == EOperation::RECV && cbTransferred == 0) {
			LOG(L"[LanClient] Server disconnected (FIN, control: %llu)\n", mSession.control.load());
			requestRelease();
			decreaseIoCount();
			continue;
		}

		switch (pOv->type)
		{
		case EOperation::RECV:
		{
			mSession.recvRingBuffer.MoveRear(cbTransferred);

			bool parseError = false;

			while (true) {
				int headerSize = sizeof(LAN_HEADER);
				if (mSession.recvRingBuffer.GetUseSize() < headerSize) break;

				LAN_HEADER lanHeader;
				mSession.recvRingBuffer.Peek((char*)&lanHeader, headerSize);

				int totalPacketSize = headerSize + lanHeader.Len;
				if (mSession.recvRingBuffer.GetUseSize() < totalPacketSize) break;

				mSession.recvRingBuffer.MoveFront(headerSize);

				Packet* pPacket = Packet::packetMemoryPool.Alloc();
				if (pPacket == nullptr) {
					OnError(-1, L"PacketMemoryPool Error");
					parseError = true;
					break;
				}
				pPacket->Init();
				pPacket->SetSessionID(mSession.id);

				// 페이로드를 헤더 크기 뒤 위치에 넣고, ReadPos도 그 위치부터 시작
				char* pPacketBuf = pPacket->GetBufferPtr();
				mSession.recvRingBuffer.Dequeue(pPacketBuf + headerSize, lanHeader.Len);
				pPacket->SetDataSize(headerSize + lanHeader.Len);
				pPacket->MoveReadPos(headerSize);

				OnRecv(pPacket);
			}

			if (parseError) {
				requestRelease();
			}
			else {
				if (!postRecv()) {
					requestRelease();
				}
			}
			break;
		}
		case EOperation::SEND:
		{
			auto* sendOv = CONTAINING_RECORD(pOverlapped, SendOverlapped, overlapped);
			for (int i = 0; i < sendOv->packetCount; i++) {
				sendOv->sentPackets[i]->SubRef();
			}
			sendOv->packetCount = 0;

			mSession.sendFlag.store(0);
			postSend();
			break;
		}
		case EOperation::RELEASE:
		{
			releaseSession();
			continue;
		}
		}

		decreaseIoCount();
	}
}
