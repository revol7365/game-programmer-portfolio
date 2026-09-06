#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <thread>
#include <atomic>
#include <vector>

#include "Define.h"
#include "Session.h"
#include "Packet.h"
#include "Log.h"

#pragma pack(push, 1)
struct LAN_HEADER {
	unsigned short Len; // Payload 크기
};
#pragma pack(pop)

#pragma comment(lib, "ws2_32.lib")

class LanClient {

public:
	LanClient(const char* ip = LAN_CLIENT_IP, int port = LAN_CLIENT_PORT, int workerThreadCount = LAN_CLIENT_WORKER_THREAD);
	virtual ~LanClient();

	bool Connect();
	void Disconnect();
	bool SendPacket(Packet* packet);
	bool IsConnected() const { return mIsConnected; }

	// 콜백 (하위 클래스에서 구현)
	virtual void OnConnect() = 0;
	virtual void OnDisconnect() = 0;
	virtual void OnRecv(Packet* packet) = 0;
	virtual void OnSend(int sendsize) = 0;
	virtual void OnError(int errorCode, const wchar_t* errorMessage) = 0;

private:
	void workerThread();

	bool postRecvInitial(); // Connect 시 ioCount를 직접 설정 후 사용
	bool postRecv();
	bool postSend();
	void decreaseIoCount();
	void requestRelease();
	void releaseSession();

	// 접속 정보
	char mIP[64];
	int mPort;
	int mWorkerThreadCount;

	// 네트워크
	SOCKET mSocket = INVALID_SOCKET;
	HANDLE mHcp = NULL;
	Session mSession;

	std::vector<std::thread> mWorkerThreads;
	volatile bool mTerminate = false;
	std::atomic<bool> mIsConnected{ false };
};
