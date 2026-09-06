#include <ws2tcpip.h>
#include <cstdint>
#include <string>
#include <conio.h>
#include <intrin.h>

#include "BaseServer.h"
#include "Encryption.h"
#include "CrashDump.h"
#include "PerfProfiler.h"

BaseServer* BaseServer::_Instance = nullptr;

BaseServer::BaseServer(int port, int workerThreadCount, int concurrentThreadCount) : mPort(port)
{
    if (workerThreadCount > 0) mWorkerThreadCount = workerThreadCount;
    if (concurrentThreadCount > 0) mConcurrentThreadCount = concurrentThreadCount;
    init();

    _Instance = this;
    CrashDump::SetCrashInfoCallback(CrashInfoCallback);
}

void BaseServer::CrashInfoCallback(char* outBuf, int bufSize)
{
    if (!_Instance) {
        sprintf_s(outBuf, bufSize, "Server instance not available");
        return;
    }
    BaseServer* s = _Instance;

    // LogMonitor 호출하여 로그 파일에도 기록
    s->LogMonitor();

    sprintf_s(outBuf, bufSize,
        "```\\n"
        "Sessions: %d | TotalAccept: %ld\\n"
        "Accept:%ld Recv:%ld Send:%ld\\n"
        "Memory Private:%.2fMB WS:%.2fMB Avail:%.2fMB NP:%lldKB\\n"
        "Pool[Packet] Use:%ld Alloc:%ld\\n"
        "DC(Client) PeerClose:%ld ForceClose:%ld\\n"
        "DC(Server) Internal:%ld RecvErr:%ld SendErr:%ld RingBufFull:%ld PktErr:%ld SendBufFull:%ld Timeout:%ld DupLogin:%ld InvalidPkt:%ld\\n"
        "SessionFull: %ld\\n"
        "```",
        s->mSessionCount.load(), s->mTotalAcceptCount.load(),
        s->mAcceptTPS, s->mRecvTPS, s->mSendTPS,
        s->mProcessMonitor.GetPrivateMB(), s->mProcessMonitor.GetWorkingSetMB(),
        s->mProcessMonitor.GetSystemAvailableMB(), s->mProcessMonitor.GetProcessNonPagedKB(),
        Packet::packetMemoryPool.GetUseCount(), Packet::packetMemoryPool.GetAllocCount(),
        s->mDisconnectCount[static_cast<int>(DisconnectReason::PeerClose)].load(),
        s->mDisconnectCount[static_cast<int>(DisconnectReason::ClientForceClose)].load(),
        s->mDisconnectCount[static_cast<int>(DisconnectReason::InternalError)].load(),
        s->mDisconnectCount[static_cast<int>(DisconnectReason::RecvError)].load(),
        s->mDisconnectCount[static_cast<int>(DisconnectReason::SendError)].load(),
        s->mDisconnectCount[static_cast<int>(DisconnectReason::RingBufferFull)].load(),
        s->mDisconnectCount[static_cast<int>(DisconnectReason::PacketError)].load(),
        s->mDisconnectCount[static_cast<int>(DisconnectReason::SendBufferFull)].load(),
        s->mDisconnectCount[static_cast<int>(DisconnectReason::Timeout)].load(),
        s->mDisconnectCount[static_cast<int>(DisconnectReason::DuplicateLogin)].load(),
        s->mDisconnectCount[static_cast<int>(DisconnectReason::InvalidPacket)].load(),
        s->mSessionFullCount.load());
}

BaseServer::~BaseServer()
{

    // accpet, content Thread 종료 Flag
    bTerminate = true;

    // 스레드 종료
    for (int i = 0; i < mWorkerThreadCount; i++)
    {
        PostQueuedCompletionStatus(mWorkerThreadHandle, 0, 0, nullptr);
    }

    // 소켓 해제
    if (mListenSock != INVALID_SOCKET)
    {
        closesocket(mListenSock);
        mListenSock = INVALID_SOCKET;
    }

    // 스레드 종료 대기
    for (std::thread& t : mThreads)
    {
        if (t.joinable())
            t.join();
    }
    // 스레드 리스트 정리
    mThreads.clear();


    // 동기화 객체 해제
    DeleteCriticalSection(&mSessionEmptyStackCs);

    // 커널 객체 해제
    if (mWorkerThreadHandle != NULL)
    {
        CloseHandle(mWorkerThreadHandle);
        mWorkerThreadHandle = NULL;
    }

    WSACleanup();
}

void BaseServer::init()
{
    LOG(L"[BaseServer] Init\n");
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    // 생성자에서 설정되지 않았으면(0) 실행 환경 CPU 기반으로 자동 설정
    int numProcessors = static_cast<int>(si.dwNumberOfProcessors);
    if (mWorkerThreadCount == 0) {
        mWorkerThreadCount = max(numProcessors / 4, 2);
    }
    if (mConcurrentThreadCount == 0) {
        mConcurrentThreadCount = max(numProcessors / 8, 1);
    }


    // Worker Thread Handle 초기화
    mWorkerThreadHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, mConcurrentThreadCount);
    if (mWorkerThreadHandle == NULL) return;

    // 동기화객체 초기화
    InitializeCriticalSection(&mSessionEmptyStackCs);

    // session 배열 생성
    mSessionListHead = new Session[DEFAULT_MAX_CONNECTION];
    for (size_t i = 0; i < mMaxConnection; i++) {
        mSessionEmptyIndexStack.push(i);
    }

    // Socket Listen
    int socketInitRet = listenSocketInit();
    if (!socketInitRet) return;

    // Thread 실행
    // accept
    mThreads.emplace_back(&BaseServer::acceptThread, this);
    // worker
    for (int i = 0; i < mWorkerThreadCount; i++)
    {
        mThreads.emplace_back(&BaseServer::workerThread, this);
    }
}

bool BaseServer::Start()
{
    return false;
}

void BaseServer::Stop()
{

}

bool BaseServer::DisConnect(const uint64_t sessionID, DisconnectReason reason)
{
    // 1. 세션 찾기 (여기서 ID 검증이 이루어짐)
    Session* session = BaseServer::findSession(sessionID);
    if (session == nullptr) return false;

    // Disconnect에서도 IOcount 추가
    if (!session->IncreaseIoCount()) return false;
    if (session->id != sessionID) {
        decreaseSessionIoCount(session);
        return false;
    }

    requestRelease(session, reason);
    decreaseSessionIoCount(session);
    return true;
}

bool BaseServer::SendPacket(const uint64_t sessionID, Packet* packet)
{
    PERF_TIMER(sendPacket);

    Session* session = BaseServer::findSession(sessionID);
    if (session == nullptr) return false;

    // releaseFlag==0일 때만 ref 획득
    if (!session->IncreaseIoCount()) return false;

    // findSession ~ acquire 사이 레이스 방지용 재검증
    if (session->id != sessionID || session->state.load() != ESessionState::Connected) {
        decreaseSessionIoCount(session);
        return false;
    }

    // 2. DCLP 암호화 로직 (Perfect)
    if (!packet->GetIsEncoding())
    {
        packet->Lock();
        if (!packet->GetIsEncoding())
        {
            PERF_TIMER(encrypt);

            int payloadSize = packet->GetDataSize() - 5;
            char* pBuffer = packet->GetBufferPtr();
            PACKET_HEADER* pHeader = (PACKET_HEADER*)pBuffer;

            // 성능 - rand의 lock 제거
            thread_local uint32_t s_rng = (uint32_t)(__rdtsc());
            s_rng ^= s_rng << 13;
            s_rng ^= s_rng >> 17;
            s_rng ^= s_rng << 5;
            unsigned char dynamicKey = (unsigned char)(s_rng & 0xFF);

            unsigned char checkSum = PacketEncryption::CalculateCheckSum(pBuffer + 5, payloadSize);

            pHeader->CheckSum = checkSum;
            PacketEncryption::Encode(dynamicKey, pBuffer + 4, 1 + payloadSize);

            pHeader->Code = PACKET_CODE;
            pHeader->Len = (unsigned short)payloadSize;
            pHeader->RandKey = dynamicKey;

            packet->SetIsEncoding(true);
        }
        packet->Unlock();
    }

    // 3. 링버퍼 투입 및 전송
    packet->AddRef();
    EnterCriticalSection(&session->sessionSendRingBuffer_cs);
    bool enqueued = session->sendRingBuffer.EnqueueItem(packet);
    if (!enqueued) {
        LOG(L"[SendPacket] sendRingBuffer FULL! sessionID: %llu", sessionID);
        packet->SubRef();  // AddRef 롤백
        LeaveCriticalSection(&session->sessionSendRingBuffer_cs);
        requestRelease(session, DisconnectReason::SendBufferFull);
        decreaseSessionIoCount(session);
        return false;
    }
    LeaveCriticalSection(&session->sessionSendRingBuffer_cs);

    bool isSuccess = postSend(session);

    // 4. 참조 해제 (여기서 세션이 파괴될 수도 있음)
    BaseServer::decreaseSessionIoCount(session);

    if (isSuccess) {
        OnSend(sessionID, packet->GetDataSize());
    }

    return isSuccess;
}

// ListenSocket 설정
bool BaseServer::listenSocketInit()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    mListenSock = socket(AF_INET, SOCK_STREAM, 0);

    SOCKADDR_IN serveraddr;
    ZeroMemory(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serveraddr.sin_port = htons(mPort);

    int bindRet = bind(mListenSock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
    if (bindRet == SOCKET_ERROR) {
        closesocket(mListenSock);
        WSACleanup();
        LOG(L"IOCP Server Socket Binding Fail Port: %d, ErrorCode: %d \n", mPort, WSAGetLastError());
        return false;
    }

    int listenRet = listen(mListenSock, SOMAXCONN);
    if (listenRet == SOCKET_ERROR) {
        closesocket(mListenSock);
        WSACleanup();
        LOG(L"IOCP Server Socket Listen Fail %d, ErrorCode: %d \n", mPort, WSAGetLastError());
        return false;
    }
    LOG(L"IOCP Server Started on Port %d...\n", mPort);
    return true;
}

Session* BaseServer::createSession()
{
    // 1. Session 배열 index 추출
    EnterCriticalSection(&mSessionEmptyStackCs);

    if (mSessionEmptyIndexStack.empty())
    {
        LeaveCriticalSection(&mSessionEmptyStackCs);
        return nullptr;
    }

    size_t index = mSessionEmptyIndexStack.top();
    Session& NewSession = mSessionListHead[index];
    mSessionEmptyIndexStack.pop();

    bool initRet = NewSession.Init();
    if (!initRet)
    {
        mSessionEmptyIndexStack.push(index);
        LeaveCriticalSection(&mSessionEmptyStackCs);
        return nullptr;
    }
    LeaveCriticalSection(&mSessionEmptyStackCs);

    // 2. ID 설정 및 생성
    // 상위 16비트는 index, 하위 48비트는 고유번호(++mSessionID)
    uint64_t unique = mSessionID.fetch_add(1);
    uint64_t sessionId = ((uint64_t)index << 48) | (unique & 0x0000FFFFFFFFFFFFULL);
    NewSession.id = sessionId;
    NewSession.state = ESessionState::Connected;

    // LOG(L"[createSession] sessionId: %llu, index: %zu", sessionId, index);
    mSessionCount.fetch_add(1);

    return &NewSession;
}

Session* BaseServer::findSession(uint64_t sessionId)
{
    // 상위 16비트는 index 하위  48비트는 고유번호(++mSessionID)
    size_t index = (size_t)(sessionId >> 48);

    // index 범위검사
    if (index >= (size_t)mMaxConnection) return nullptr;
    Session& session = mSessionListHead[index];
    if (session.id != sessionId) {
        // LOG(L"[findSession] session.id: %llu, sessionid: %llu", session.id, sessionId);
        // __debugbreak();
        return nullptr;
    }
    if (session.state != ESessionState::Connected) return nullptr;
    return &session;
}

// Disconnect는 다 밖에서 하게 수정
bool BaseServer::postRecv(Session* session)
{

    DWORD recvBytes, flags = 0;
    if (!session->IncreaseIoCount()) return false;

    // overlapped 초기화
    ZeroMemory(&session->recvOverlapped.overlapped, sizeof(OVERLAPPED));
    // recvOverlapped에 recv ringbuffer 설정
    int dwBufferCount = session->recvRingBuffer.GetEnqueueSegments(session->recvOverlapped.wsaBuf);
    if (dwBufferCount == 0) {
        // 링버퍼에 자리가 없음
        LOG(L"[PostRecv] RingBuffer is Full");
        requestRelease(session, DisconnectReason::RingBufferFull);
        decreaseSessionIoCount(session);
        return false;
    }

    // wsabuffer가 2개가 들어가야 함
    int ret = WSARecv(session->socket, session->recvOverlapped.wsaBuf, dwBufferCount, &recvBytes, &flags, &session->recvOverlapped.overlapped, NULL);
    int err_code = WSAGetLastError();
    if (ret == SOCKET_ERROR && err_code != ERROR_IO_PENDING) {
        if (err_code != 10054)
        {
            LOG(L"WSARecv Error: %d socketID: %d \n", err_code, (int)session->socket);
        }
        requestRelease(session, DisconnectReason::RecvError);
        decreaseSessionIoCount(session);
        return false;
    }
    return true;
}

// 사용 전에 sessonLock을 잡음
bool BaseServer::postSend(Session* session)
{
    PERF_TIMER(postSend);

    if (session->sendFlag.exchange(1) != 0) return true;

    EnterCriticalSection(&session->sessionSendRingBuffer_cs);

    DWORD dwBufferCount = 0;
    session->sendOverlapped.packetCount = 0;

    // 패킷 꺼내면서 sentPackets에 보관
    while (session->sendRingBuffer.GetUseSize() > 0 && dwBufferCount < MAX_SEND_PACKET)
    {
        Packet* packet = nullptr;
        session->sendRingBuffer.DequeueItem(&packet);

        if (packet == nullptr) break;

        session->sendOverlapped.wsaBuf[dwBufferCount].buf = packet->GetBufferPtr();
        session->sendOverlapped.wsaBuf[dwBufferCount].len = packet->GetDataSize();
        session->sendOverlapped.sentPackets[dwBufferCount] = packet;
        dwBufferCount++;
    }
    session->sendOverlapped.packetCount = dwBufferCount;
    LeaveCriticalSection(&session->sessionSendRingBuffer_cs);

    if (dwBufferCount == 0) {
        session->sendFlag.store(0);
        return true;
    }

    // releaseFlag==0일 때만 "SEND I/O ref" 획득
    if (!session->IncreaseIoCount()) {
        // 꺼낸 패킷 되돌리기(SubRef)
        for (DWORD i = 0; i < dwBufferCount; ++i) session->sendOverlapped.sentPackets[i]->SubRef();
        session->sendOverlapped.packetCount = 0;
        session->sendFlag.store(0);
        return false;
    }

    ZeroMemory(&session->sendOverlapped.overlapped, sizeof(OVERLAPPED));

    int ret = WSASend(session->socket, session->sendOverlapped.wsaBuf,
        dwBufferCount, NULL, 0,
        &session->sendOverlapped.overlapped, NULL);

    if (ret == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        if (err == WSA_IO_PENDING) {
            return true;
        }

        // 패킷 정리
        for (int i = 0; i < session->sendOverlapped.packetCount; ++i) {
            session->sendOverlapped.sentPackets[i]->SubRef();
        }
        session->sendOverlapped.packetCount = 0;

        if (err != 10054)
        {
            LOG(L"[postSend] WSASend Failed - requesting release. err: %d, sessionID: %llu\n", err, session->id);
        }
        requestRelease(session, DisconnectReason::SendError);
        decreaseSessionIoCount(session);
        session->sendFlag.store(0);
        return false;
    }
    return true;
}

void BaseServer::decreaseSessionIoCount(Session* session)
{
    uint64_t prevFull = session->control.fetch_sub(1);
    SessionControl prev{};
    prev.full = prevFull;

    if (prev.ioCount == 1 && prev.releaseFlag == 1) {
        // 직접 releaseSession 호출 시 호출자가 Lock을 잡고 있으면 데드락 발생
        // IOCP에 포스팅하여 별도 워커 스레드에서 안전하게 처리
        PostQueuedCompletionStatus(mWorkerThreadHandle, 0,
            (ULONG_PTR)session, &session->releaseOverlapped.overlapped);
    }
}

void BaseServer::requestRelease(Session* session, DisconnectReason reason)
{
    if (!session) return;

    uint32_t before = 0;

    if (!session->TrySetReleaseFlag(before))
        return;

    mDisconnectCount[static_cast<int>(reason)].fetch_add(1);

    // pending IO(WSARecv/WSASend)를 취소하여 ioCount가 0으로 떨어지게 함
    // closesocket이 아닌 CancelIoEx로 소켓은 유지하고 IO만 취소
    CancelIoEx((HANDLE)session->socket, NULL);

    if (before == 0) {
        // ioCount가 이미 0인 상태 → IOCP에 포스팅하여 안전하게 릴리즈
        PostQueuedCompletionStatus(mWorkerThreadHandle, 0,
            (ULONG_PTR)session, &session->releaseOverlapped.overlapped);
    }
}

void BaseServer::releaseSession(Session* session)
{
    if (!session) return;

    uint64_t expectedFull = session->control.load();
    while (true)
    {
        SessionControl exp{}; exp.full = expectedFull;

        if (exp.ioCount != 0) return;
        if ((exp.releaseFlag & FLAG_RELEASE) == 0) return;

        if (exp.releaseFlag & FLAG_CLEANUP) return; // 이미 누가 담당

        SessionControl des = exp;
        des.releaseFlag |= FLAG_CLEANUP; // 담당자 획득 (RELEASE는 유지)

        if (session->control.compare_exchange_weak(expectedFull, des.full))
            break;
    }

    uint64_t sessionID = session->id;

    // 1. 소켓 즉시 차단 (추가적인 I/O 유입 방지)
    if (session->socket != INVALID_SOCKET) {
        closesocket(session->socket);
        session->socket = INVALID_SOCKET;
    }

    // 2. 상위 어플리케이션에 알림
    // 세션 인덱스가 풀에 돌아가기 전에 어플리케이션 정리를 끝내야 안전
    OnClientLeave(sessionID);
    int prevCount = mSessionCount.fetch_sub(1);

    if (prevCount <= 0)
    {
        __debugbreak();
    }

    // 3. 버퍼 및 미전송 패킷 정리
    EnterCriticalSection(&session->sessionSendRingBuffer_cs);
    while (session->sendRingBuffer.GetUseSize() > 0) {
        Packet* packet = nullptr;
        if (session->sendRingBuffer.DequeueItem(&packet) && packet) {
            packet->SubRef();
        }
    }
    LeaveCriticalSection(&session->sessionSendRingBuffer_cs);

    // 4. 세션 데이터 초기화
    session->recvRingBuffer.ClearBuffer();
    session->sendRingBuffer.ClearBuffer();
    session->id = 0;
    session->state = ESessionState::Free;
    session->sendFlag.store(0);

    // 5. 마지막에 인덱스 반납 (이제 재사용 가능)
    int index = (int)(sessionID >> 48);
    EnterCriticalSection(&mSessionEmptyStackCs);
    mSessionEmptyIndexStack.push(index);
    LeaveCriticalSection(&mSessionEmptyStackCs);
}

void BaseServer::acceptThread()
{
    SOCKET clientSock;
    SOCKADDR_IN clientAddr{};
    int addrlen = sizeof(clientAddr);
    DWORD recvbytes{};

    while (!bTerminate) {
        clientSock = accept(mListenSock, (SOCKADDR*)&clientAddr, &addrlen);
        if (clientSock == INVALID_SOCKET)
        {
            LOG(L"Failed to accept socket.\n");
            continue;
        }

        if (!OnConnectionRequest(ntohl(clientAddr.sin_addr.s_addr), ntohs(clientAddr.sin_port))) {
            closesocket(clientSock);
            continue;
        }

        // 1. 세션 생성
        LINGER optval{};
        optval.l_onoff = 1;
        optval.l_linger = 0;
        int ret = setsockopt(clientSock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
        if (ret == SOCKET_ERROR) {
            LOG(L"[Accept] setsockopt SO_LINGER Failed, socket leaked prevention - closing socket. err: %d\n", WSAGetLastError());
            closesocket(clientSock);
            continue;
        }

        // Nagle 알고리즘 비활성화 (즉시 전송)
        if (!bNagle) {
            BOOL nodelay = TRUE;
            setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
        }

        Session* newSession = createSession();

        if (newSession == nullptr)
        {
            mSessionFullCount.fetch_add(1);
            LOG(L"[SessionFull] Session pool exhausted, connection rejected. total:%ld", mSessionFullCount.load());
            closesocket(clientSock);
            continue;
        }
        newSession->socket = clientSock;
        mAcceptCount++;
        mTotalAcceptCount++;

        char ipStr[INET_ADDRSTRLEN];

        // 2. inet_ntop 함수로 주소를 문자열로 변환합니다.
        inet_ntop(AF_INET,           // 주소 체계 (IPv4)
            &clientAddr.sin_addr, // 변환할 주소 구조체 포인터
            ipStr,               // 결과를 저장할 버퍼
            INET_ADDRSTRLEN);    // 버퍼의 크기

        // 2. IOCP 등록 (Key를 Session 포인터로 지정)
        if (CreateIoCompletionPort((HANDLE)clientSock, mWorkerThreadHandle, (ULONG_PTR)newSession, 0) == NULL)
        {
            LOG(L"CreateIoCompletionPort failed: %d \n", WSAGetLastError());
            requestRelease(newSession, DisconnectReason::InternalError);
            continue;
        }

        OnClientJoin(newSession->id);

        // 3. 최초 Recv 요청 (Session 내부 함수로 대체)
        if (!postRecv(newSession))
        {
            LOG(L"[Accept] postRecv FAILED - releasing session. sessionID: %llu\n", newSession->id);
            requestRelease(newSession, DisconnectReason::RecvError);
        }

        // Accept 속도 조절 - backlog에 쌓이게 하여 부하 분산
        // 현재 세션이 충분하면 잠시 양보
        if (mSessionCount.load() >= (mMaxConnection - 100)) {
            Sleep(1);
        }
    }
}

void BaseServer::workerThread()
{
    DWORD cbTransferred;
    Session* session = nullptr; // CompletionKey (Session 포인터)
    LPOVERLAPPED pOverlapped = nullptr;

    while (1) {
        BOOL ret = GetQueuedCompletionStatus(mWorkerThreadHandle, &cbTransferred, (PULONG_PTR)&session, &pOverlapped, INFINITE);

        // 종료 신호 체크: PostQueuedCompletionStatus(hcp, 0, 0, nullptr)
        if (pOverlapped == nullptr && session == nullptr) {
            break;
        }

        if (session == nullptr) {
            // (hcp가 닫혔거나 비정상 종료)
            LOG(L"WorkerThread HcP Error or Exit.\n");
            continue;
        }

        OverlappedBase* pOv = CONTAINING_RECORD(pOverlapped, OverlappedBase, overlapped);

        // 1. 에러 체크
        if (ret == FALSE) {
            int err = GetLastError();
            // LOG(L"[GQCS] Client force closed sessionID:%llu err:%d", session->id, err);
            requestRelease(session, DisconnectReason::ClientForceClose);
            decreaseSessionIoCount(session);
            continue;
        }

        // 2. 정상 종료 체크 (RECV에서 cbTransferred == 0)
        if (pOv->type == EOperation::RECV && cbTransferred == 0) {
            // LOG(L"[GQCS FIN] Client disconnected\n");
            requestRelease(session, DisconnectReason::PeerClose);
            decreaseSessionIoCount(session);
            continue;
        }

        switch (pOv->type)
        {
        case EOperation::RECV:
        {
            PERF_TIMER(recvParse);

            session->recvRingBuffer.MoveRear(cbTransferred);

            bool parseError = false;  //에러 플래그 추가

            while (true) {
                if (session->recvRingBuffer.GetUseSize() < 3) {
                    break;
                }

                char header[3];
                session->recvRingBuffer.Peek(header, 3);
                unsigned char dataCode = (unsigned char)header[0];
                unsigned short dataLen = *reinterpret_cast<unsigned short*>(&header[1]);

                if (dataCode != PACKET_CODE) {
                    OnError(-1, L"Invalid Packet Code");
                    parseError = true;
                    break;
                }

                if (dataLen == 0) {
                    LOG(L"[ATTACK] Zero-length payload sessionID:%llu", session->id);
                    parseError = true;
                    break;
                }
                if (dataLen > Packet::BUFFER_DEFAULT - 5) {
                    LOG(L"[ATTACK] Payload exceeds packet buffer sessionID:%llu dataLen:%d max:%d", session->id, dataLen, Packet::BUFFER_DEFAULT - 5);
                    parseError = true;
                    break;
                }

                int totalPacketSize = 3 + 1 + 1 + dataLen;
                if (totalPacketSize > session->recvRingBuffer.GetBufferSize()) {
                    LOG(L"[ATTACK] Oversize packet sessionID:%llu totalSize:%d bufferSize:%d", session->id, totalPacketSize, session->recvRingBuffer.GetBufferSize());
                    parseError = true;
                    break;
                }
                if (session->recvRingBuffer.GetUseSize() < totalPacketSize) {
                    break;
                }

                session->recvRingBuffer.MoveFront(3);

                unsigned char randKey = 0;
                session->recvRingBuffer.Dequeue((char*)&randKey, 1);

                Packet* pPacket = Packet::packetMemoryPool.Alloc();
                if (pPacket == nullptr) {
                    OnError(-1, L"PacketMemoryPool Error");
                    parseError = true;
                    break;
                }
                pPacket->Init();
                pPacket->SetSessionID(session->id);

                int encryptedLen = 1 + dataLen;
                char* pPacketBuf = pPacket->GetBufferPtr();
                session->recvRingBuffer.Dequeue(pPacketBuf + 4, encryptedLen);

                PacketEncryption::Decode(randKey, pPacketBuf + 4, encryptedLen);

                unsigned char receivedCheckSum = (unsigned char)pPacketBuf[4];
                unsigned char calcCheckSum = PacketEncryption::CalculateCheckSum(pPacketBuf + 5, dataLen);

                if (receivedCheckSum != calcCheckSum) {
                    pPacket->SubRef();
                    OnError(-1, L"CheckSum Error");
                    parseError = true;
                    break;
                }

                pPacket->SetDataSize(5 + dataLen);
                pPacket->MoveReadPos(5);

                OnRecv(pPacket->GetSessionID(), pPacket);
            }

            // 파싱 에러 시 세션 끊기
            if (parseError) {
                requestRelease(session, DisconnectReason::PacketError);
            }
            else {
                if (!postRecv(session)) {
                    requestRelease(session, DisconnectReason::RecvError);
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

            static_assert(offsetof(SendOverlapped, overlapped) == 0);

            // 2. sendFlag off → 보낼 게 남아있으면 다시 send
            session->sendFlag.store(0);
            postSend(session);

            break;
        }
        case EOperation::RELEASE:
        {
            // IOCP에 포스팅된 릴리즈 요청 처리
            // 별도 워커 스레드에서 실행되므로 Lock 데드락 없음
            releaseSession(session);
            continue; // ioCount는 이미 0이므로 decreaseSessionIoCount 스킵
        }
        }
        // 3. 완료된 I/O 작업에 대한 참조 카운트 감소
        decreaseSessionIoCount(session);
    }
}

// 통계
void BaseServer::ServerControll()
{
    // 키보드 컨트롤 잠금 상태 (false: 잠금, true: 허용)
    static bool bControlMode = false;

    if (_kbhit())
    {
        WCHAR ControlKey = _getwch(); // 대문자 전환
        ControlKey = towupper(ControlKey);

        // [L] 키보드 제어 잠금/해제 토글 (항상 작동)
        if (ControlKey == L'L')
        {
            bControlMode = !bControlMode; // 상태 반전 (True <-> False)

            if (bControlMode)
            {
                wprintf(L"\n[System] Control UNLOCKED. (Press 'H' for Help)\n");
            }
            else
            {
                wprintf(L"\n[System] Control LOCKED.\n");
            }
            return; // L키 처리 후 종료
        }

        // 잠금 상태라면 L키 이외의 명령어는 무시
        if (bControlMode == false)
        {
            return;
        }

        // 제어 허용 상태일 때의 명령어 처리
        switch (ControlKey)
        {
            // [Q] 서버 종료
        case L'Q':
            printUpTime();
            wprintf(L"[System] Server Shutdown Initiated...\n");
            CrashDump::WriteShutdownDump(L"Q-key shutdown");
            bTerminate = true;
            break;

            // [I] 정보 확인 (Map Size)
        case L'I':
            printInfo();
            break;

            // [U] 현재 시간 및 업타임 확인
        case L'U':
        {
            printUpTime();
        }
        break;

        // [H] 도움말 출력
        case L'H':
            wprintf(L"\n========== [ Command List ] ==========\n");
            wprintf(L" [L] Toggle Lock/Unlock Control\n");
            wprintf(L" [I] Show Session/Character Info\n");
            wprintf(L" [U] Show Time & Uptime\n");
            wprintf(L" [Q] Quit Server\n");
            wprintf(L"======================================\n");
            break;

        default:
            break;
        }
    }
}

void BaseServer::printUpTime()
{
    // 1. 현재 시간 구하기
    time_t rawtime;
    struct tm timeinfo;
    wchar_t buffer[80];

    time(&rawtime);
    localtime_s(&timeinfo, &rawtime);
    wcsftime(buffer, 80, L"%Y-%m-%d %H:%M:%S", &timeinfo);

    // 2. 업타임(가동 시간) 구하기
    DWORD dwCurrentTime = timeGetTime();
    DWORD dwUpTime = dwCurrentTime - dwServerStartTime; // 흐른 시간 (ms)

    int nSeconds = (dwUpTime / 1000) % 60;
    int nMinutes = (dwUpTime / (1000 * 60)) % 60;
    int nHours = (dwUpTime / (1000 * 60 * 60));

    wprintf(L"\n====================================== \n");
    wprintf(L" Current Time : %s\n", buffer);
    wprintf(L" Server Uptime: %02d hours %02d mins %02d secs\n", nHours, nMinutes, nSeconds);
    wprintf(L"====================================== \n");

}

// TPS 갱신 (1초마다 호출)
void BaseServer::UpdateTPS() {
    mAcceptTPS = mAcceptCount.exchange(0);
    mRecvTPS = mRecvCount.exchange(0);
    mSendTPS = mSendCount.exchange(0);
    mProcessMonitor.Update();
}

void BaseServer::PrintMonitor()
{
    // 커서를 고정 위치(0,0)로 이동하여 덮어쓰기
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    static bool initDone = false;
    if (!initDone) {
        COORD bufSize = { 120, 60 };
        SetConsoleScreenBufferSize(hConsole, bufSize);
        initDone = true;
        CONSOLE_CURSOR_INFO ci;
        GetConsoleCursorInfo(hConsole, &ci);
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(hConsole, &ci);
        initDone = true;
    }

    COORD pos = { 0, 0 };
    SetConsoleCursorPosition(hConsole, pos);

    // 현재 시간
    time_t rawtime;
    struct tm timeinfo;
    wchar_t buffer[80];
    time(&rawtime);
    localtime_s(&timeinfo, &rawtime);
    wcsftime(buffer, 80, L"%Y-%m-%d %H:%M:%S", &timeinfo);

    // 업타임
    DWORD dwUpTime = timeGetTime() - dwServerStartTime;
    int nHours = (dwUpTime / (1000 * 60 * 60));
    int nMinutes = (dwUpTime / (1000 * 60)) % 60;
    int nSeconds = (dwUpTime / 1000) % 60;

    ConsolePrintLine(L"======================= CHATING SERVER %d ========================", DEFAULT_PORT);
    ConsolePrintLine(L"  Time    : %-30s", buffer);
    ConsolePrintLine(L"  Uptime  : %02d h %02d m %02d s", nHours, nMinutes, nSeconds);
    ConsolePrintLine(L"-------------------------------------------------------------------");
    ConsolePrintLine(L"  Sessions    : %-10d  Total Accept: %-10ld", mSessionCount.load(), mTotalAcceptCount.load());
    ConsolePrintLine(L"  Accept TPS  : %-10ld  Recv TPS : %-10ld  Send TPS : %-10ld", mAcceptTPS, mRecvTPS, mSendTPS);
    ConsolePrintLine(L"-------------------------------------------------------------------");
    ConsolePrintLine(L"  Job Total : %-10ld  HeartBeat Total : %-10ld", mJobCount.load(), mCheckHeartCount.load());
    ConsolePrintLine(L"-------------------------------------------------------------------");
    ConsolePrintLine(L"  Private  : %-8.2f MB   WorkingSet : %-8.2f MB   Avail : %-8.2f MB",
        mProcessMonitor.GetPrivateMB(), mProcessMonitor.GetWorkingSetMB(), mProcessMonitor.GetSystemAvailableMB());
    ConsolePrintLine(L"  NP(Proc) : %-6lld KB     NP(System) : %-6lld KB",
        mProcessMonitor.GetProcessNonPagedKB(), mProcessMonitor.GetSystemNonPagedKB());
    ConsolePrintLine(L"-------------------------------------------------------------------");
    ConsolePrintLine(L"  Pool [Packet] Use:%-8ld Alloc:%-8ld Free:%-8ld", Packet::packetMemoryPool.GetUseCount(), Packet::packetMemoryPool.GetAllocCount(), Packet::packetMemoryPool.GetFreeCount());
    PrintContentMonitor();
    ConsolePrintLine(L"-------------------------------------------------------------------");
    g_perf.PrintAll();
    ConsolePrintLine(L"===================================================================");
    ConsolePrintLine(L"  [L] Unlock Control  [H] Help");

    // 남은 줄을 공백으로 채워 이전 프레임 잔상 제거
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    DWORD remaining = csbi.dwSize.X * (csbi.dwSize.Y - csbi.dwCursorPosition.Y);
    DWORD written;
    FillConsoleOutputCharacter(hConsole, L' ', remaining, csbi.dwCursorPosition, &written);
    FillConsoleOutputAttribute(hConsole, csbi.wAttributes, remaining, csbi.dwCursorPosition, &written);
}

void BaseServer::printInfo() {
    wprintf(L"\n======================================\n");
    wprintf(L" Connected Sessions : %d\n", mSessionCount.load());
    wprintf(L" Total Accept       : %ld\n", mTotalAcceptCount.load());
    wprintf(L" Accept TPS         : %ld\n", mAcceptTPS);
    wprintf(L" Recv TPS           : %ld\n", mRecvTPS);
    wprintf(L" Send TPS           : %ld\n", mSendTPS);
    wprintf(L"======================================\n");
}

void BaseServer::LogMonitor()
{
    DWORD dwUpTime = timeGetTime() - dwServerStartTime;
    int nHours = (dwUpTime / (1000 * 60 * 60));
    int nMinutes = (dwUpTime / (1000 * 60)) % 60;
    int nSeconds = (dwUpTime / 1000) % 60;

    LOG(L"[Monitor] Uptime: %02dh %02dm %02ds | Sessions: %d | TotalAccept: %ld | Accept: %ld | Recv: %ld | Send: %ld",
        nHours, nMinutes, nSeconds,
        mSessionCount.load(), mTotalAcceptCount.load(),
        mAcceptTPS, mRecvTPS, mSendTPS);

    LOG(L"[Monitor] Memory Private: %.2f MB | WorkingSet: %.2f MB | Avail: %.2f MB | NP(Proc): %lld KB | NP(Sys): %lld KB",
        mProcessMonitor.GetPrivateMB(), mProcessMonitor.GetWorkingSetMB(),
        mProcessMonitor.GetSystemAvailableMB(),
        mProcessMonitor.GetProcessNonPagedKB(), mProcessMonitor.GetSystemNonPagedKB());

    LOG(L"[Monitor] Pool [Packet] Use: %ld Alloc: %ld",
        Packet::packetMemoryPool.GetUseCount(), Packet::packetMemoryPool.GetAllocCount());

    // DisconnectReason별 누적 카운터 로그 (클라이언트 끊김)
    LOG(L"[Monitor] DC(Client) | PeerClose:%ld ForceClose:%ld",
        mDisconnectCount[static_cast<int>(DisconnectReason::PeerClose)].load(),
        mDisconnectCount[static_cast<int>(DisconnectReason::ClientForceClose)].load());
    // DisconnectReason별 누적 카운터 로그 (서버 끊김)
    LOG(L"[Monitor] DC(Server) | Internal:%ld RecvErr:%ld SendErr:%ld RingBufFull:%ld PktErr:%ld SendBufFull:%ld Timeout:%ld DupLogin:%ld InvalidPkt:%ld",
        mDisconnectCount[static_cast<int>(DisconnectReason::InternalError)].load(),
        mDisconnectCount[static_cast<int>(DisconnectReason::RecvError)].load(),
        mDisconnectCount[static_cast<int>(DisconnectReason::SendError)].load(),
        mDisconnectCount[static_cast<int>(DisconnectReason::RingBufferFull)].load(),
        mDisconnectCount[static_cast<int>(DisconnectReason::PacketError)].load(),
        mDisconnectCount[static_cast<int>(DisconnectReason::SendBufferFull)].load(),
        mDisconnectCount[static_cast<int>(DisconnectReason::Timeout)].load(),
        mDisconnectCount[static_cast<int>(DisconnectReason::DuplicateLogin)].load(),
        mDisconnectCount[static_cast<int>(DisconnectReason::InvalidPacket)].load());
    LOG(L"[Monitor] SessionFull(Rejected): %ld", mSessionFullCount.load());

    // 하위 클래스 추가 로그
    LogContentMonitor();
    g_perf.LogAll();
}
