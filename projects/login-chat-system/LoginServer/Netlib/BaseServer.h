#pragma once

#include <winsock2.h>

#include <cstring>
#include <queue>
#include <thread>
#include <stack>
#include <atomic>

#include "Define.h"
#include "Log.h"
#include "ProcessMonitor.h"
#include "PerfProfiler.h"
#include "Session.h"
#include "Packet.h"

#pragma comment(lib, "ws2_32.lib")

static DWORD dwServerStartTime = timeGetTime();

class BaseServer {

public:
	BaseServer(int port = DEFAULT_PORT, int workerThreadCount = 0, int concurrentThreadCount = 0);
	virtual ~BaseServer();

	bool Start();
	void Stop();

	bool DisConnect(const uint64_t sessionID);
	bool SendPacket(const uint64_t sessionID, Packet* packet);

	virtual bool OnConnectionRequest(int ip, int port) = 0; // false: 거부, true: 허용
	virtual void OnClientJoin(uint64_t sessionID) = 0; // Accept 후 접속처리 완료 후 호출.
	virtual void OnClientLeave(uint64_t sessionID) = 0; // Release 후 호출
	virtual void OnRecv(uint64_t sessionID, Packet* packet) = 0; // 패킷 수신 완료 후
	virtual void OnSend(uint64_t sessionID, int sendsize) = 0; // 패킷 송신 완료 후
	virtual void OnError(int errorCode, const wchar_t* errorMessage) = 0;

	//virtual void OnWorkerThreadBegin() = 0; // 워커스레드 GQCS 바로 하단에서 호출
	//virtual void OnWorkerThreadEnd() = 0; // 워커스레드 1루프 종료 후

	// 통계
	void UpdateTPS();
	bool GetTerminate() const { return bTerminate; }
	void ServerControll();
	void PrintMonitor();

	enum class DisconnectReason : uint8_t
	{
		None = 0,
		PeerClose,      // cbTransferred == 0
		GqcsFail,		// GQCS ret == FALSE
		RecvError,
		SendError,
		RingBufferFull,
		ProtocolError,
		Kick,
		ServerShutdown,
		Logic,
		PacketError,
		SendBufferFull,
		InternalError,
		Timeout,
		COUNT
	};

	void LogMonitor();

	// 모니터링 데이터 Getter
	int GetSessionCount() const { return mSessionCount.load(); }
	long GetAcceptTPS() const { return mAcceptTPS; }
	long GetRecvTPS() const { return mRecvTPS; }
	long GetSendTPS() const { return mSendTPS; }
	const ProcessMonitor& GetProcessMonitor() const { return mProcessMonitor; }

private:
	void init();
	bool listenSocketInit();

	void acceptThread();
	void workerThread();

	Session* createSession();
	Session* findSession(uint64_t sessionId);

	bool postRecv(Session* session);
	bool postSend(Session* session);
	void decreaseSessionIoCount(Session* session);
	void requestRelease(Session* session, DisconnectReason reason);
	void releaseSession(Session* session);

	// 통계
	void printUpTime();
	void printInfo();

	// 서버 기본 설정
	int mPort;
	int mWorkerThreadCount = DEFAULT_WORKER_THREAD;
	int mConcurrentThreadCount = DEFAULT_CONCURRENT;
	bool bNagle = DEFAULT_NAGLE;
	int mMaxConnection = DEFAULT_MAX_CONNECTION;

	SOCKET mListenSock = INVALID_SOCKET;
	HANDLE mWorkerThreadHandle = NULL;

	// Session 관련
	Session* mSessionListHead = nullptr; // Session 배열
	std::stack<uint64_t> mSessionEmptyIndexStack{};
	CRITICAL_SECTION mSessionEmptyStackCs;

	std::atomic<uint64_t> mSessionID = 0; // sessionID

	// TPS 계산용 (1초마다 리셋)
	long mAcceptTPS = 0;
	long mRecvTPS = 0;
	long mSendTPS = 0;

	// 메모리 모니터링
	ProcessMonitor mProcessMonitor;

protected:
	// contentThread: 선택적 오버라이드
	// NetServer에서는 순수 가상으로 재선언하여 하위 클래스에 구현을 강제
	// LanServer에서는 기본 빈 구현 사용 (worker 스레드에서 직접 처리)
	virtual void contentThread(int threadIdx) {}

	// 하위 클래스가 모니터에 추가 정보를 출력할 수 있도록
	virtual void PrintContentMonitor() {}
	virtual void LogContentMonitor() {}

	volatile bool bTerminate = false;
	std::vector<std::thread> mThreads;

	// 통계용 변수
	std::atomic<long> mTotalAcceptCount{ 0 };
	std::atomic<long> mAcceptCount{ 0 };
	std::atomic<long> mRecvCount{ 0 };
	std::atomic<long> mSendCount{ 0 };
	std::atomic<int> mSessionCount{ 0 };

	std::atomic<long> mJobCount{ 0 };
	std::atomic<long> mCheckHeartCount{ 0 };
	std::atomic<long> mDisconnectCount[static_cast<int>(DisconnectReason::COUNT)]{};

};
